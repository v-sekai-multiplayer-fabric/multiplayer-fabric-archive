/*
 * Is FoundationDB a pubsub bus? Measure, do not guess.
 *
 *   1. Commit latency          -- the floor for "publish" via FDB.
 *   2. Watch fire latency      -- publish to subscriber wakeup.
 *   3. Watch limit             -- how many subscribers before it breaks.
 *   4. Versionstamp append     -- the durable event-log pattern.
 *
 * Compared against the measured AF_UNIX number from rfd/0096 (8910 ns
 * on Fly) and against a 15.6 ms ZoneTick at 64 Hz.
 *
 * This runs against a local single-memory FDB, not Fly. Commit latency
 * on a real multi-node cluster is HIGHER, so every number here is a
 * lower bound for production.
 */
#define FDB_API_VERSION 730
#include <foundationdb/fdb_c.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define COMMITS 300
#define WATCHES_TRY 12000

static FDBDatabase *db;

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}
static void report(const char *n, uint64_t *s, int c)
{
	if (c <= 0) { printf("FDBPUB %-26s NO DATA\n", n); return; }
	qsort(s, c, sizeof(*s), cmp_u64);
	printf("FDBPUB %-26s median=%8.1f us  p99=%9.1f us  n=%d\n",
	       n, s[c / 2] / 1000.0, s[(int)(c * 0.99)] / 1000.0, c);
	fflush(stdout);
}

static void *netthread(void *a) { (void)a; fdb_run_network(); return NULL; }

static int commit_kv(const char *k, int klen, const char *v, int vlen, uint64_t *dt)
{
	FDBTransaction *tr;
	if (fdb_database_create_transaction(db, &tr)) return -1;
	fdb_transaction_set(tr, (const uint8_t *)k, klen, (const uint8_t *)v, vlen);
	uint64_t t0 = now_ns();
	FDBFuture *f = fdb_transaction_commit(tr);
	fdb_future_block_until_ready(f);
	fdb_error_t e = fdb_future_get_error(f);
	if (dt) *dt = now_ns() - t0;
	fdb_future_destroy(f);
	fdb_transaction_destroy(tr);
	return e ? -1 : 0;
}

int main(void)
{
	fdb_select_api_version(FDB_API_VERSION);
	fdb_setup_network();
	pthread_t th;
	pthread_create(&th, NULL, netthread, NULL);
	usleep(300000);
	if (fdb_create_database(NULL, &db)) { printf("FDBPUB no database\n"); return 1; }

	printf("FDBPUB ==== FoundationDB as a pubsub bus ====\n");
	fflush(stdout);

	/* 1. commit latency: the floor for publishing through FDB */
	uint64_t *s = malloc(sizeof(uint64_t) * COMMITS);
	int n = 0;
	for (int i = 0; i < COMMITS; i++) {
		char v[64];
		int vl = snprintf(v, sizeof(v), "tick-%d", i);
		uint64_t dt;
		if (commit_kv("zf/pub/topic", 12, v, vl, &dt) == 0) s[n++] = dt;
	}
	report("commit (publish)", s, n);

	/* 2. watch fire latency: publish -> subscriber wakeup */
	int wn = 0;
	for (int i = 0; i < 100; i++) {
		FDBTransaction *wt;
		if (fdb_database_create_transaction(db, &wt)) break;
		FDBFuture *w = fdb_transaction_watch(wt, (const uint8_t *)"zf/pub/topic", 12);
		FDBFuture *wc = fdb_transaction_commit(wt);
		fdb_future_block_until_ready(wc);
		fdb_future_destroy(wc);
		fdb_transaction_destroy(wt);

		char v[64];
		int vl = snprintf(v, sizeof(v), "fire-%d", i);
		uint64_t t0 = now_ns();
		if (commit_kv("zf/pub/topic", 12, v, vl, NULL) != 0) { fdb_future_destroy(w); break; }
		fdb_future_block_until_ready(w);
		s[wn++] = now_ns() - t0;
		fdb_future_destroy(w);
	}
	report("watch fire (pub->sub)", s, wn);

	/* 3. how many concurrent watches before FDB refuses */
	FDBTransaction *bt;
	fdb_database_create_transaction(db, &bt);
	FDBFuture **ws = malloc(sizeof(FDBFuture *) * WATCHES_TRY);
	int held = 0;
	fdb_error_t last = 0;
	for (int i = 0; i < WATCHES_TRY; i++) {
		char k[64];
		int kl = snprintf(k, sizeof(k), "zf/pub/sub/%06d", i);
		FDBTransaction *t2;
		if (fdb_database_create_transaction(db, &t2)) break;
		FDBFuture *w = fdb_transaction_watch(t2, (const uint8_t *)k, kl);
		FDBFuture *c = fdb_transaction_commit(t2);
		fdb_future_block_until_ready(c);
		fdb_error_t ce = fdb_future_get_error(c);
		fdb_future_destroy(c);
		fdb_transaction_destroy(t2);
		if (ce) { last = ce; fdb_future_destroy(w); break; }
		if (fdb_future_is_ready(w)) {
			fdb_error_t we = fdb_future_get_error(w);
			if (we) { last = we; fdb_future_destroy(w); break; }
		}
		ws[held++] = w;
	}
	printf("FDBPUB concurrent watches held = %d, stopped at error %d (%s)\n",
	       held, last, last ? fdb_get_error(last) : "no error, hit the try cap");
	fflush(stdout);
	for (int i = 0; i < held; i++) fdb_future_destroy(ws[i]);
	fdb_transaction_destroy(bt);

	/* 4. versionstamp event-log append: the durable queue pattern */
	n = 0;
	for (int i = 0; i < COMMITS; i++) {
		FDBTransaction *tr;
		if (fdb_database_create_transaction(db, &tr)) break;
		uint8_t key[64];
		memcpy(key, "zf/pub/log/", 11);
		memset(key + 11, 0, 10);            /* versionstamp placeholder */
		uint32_t pos = 11;
		memcpy(key + 21, &pos, 4);          /* offset trailer, API 520+ */
		char v[32];
		int vl = snprintf(v, sizeof(v), "ev-%d", i);
		fdb_transaction_atomic_op(tr, key, 25, (const uint8_t *)v, vl,
		                          FDB_MUTATION_TYPE_SET_VERSIONSTAMPED_KEY);
		uint64_t t0 = now_ns();
		FDBFuture *f = fdb_transaction_commit(tr);
		fdb_future_block_until_ready(f);
		fdb_error_t e = fdb_future_get_error(f);
		if (!e) s[n++] = now_ns() - t0;
		fdb_future_destroy(f);
		fdb_transaction_destroy(tr);
	}
	report("versionstamp log append", s, n);

	printf("FDBPUB ==== done ====\n");
	fflush(stdout);
	return 0;
}
