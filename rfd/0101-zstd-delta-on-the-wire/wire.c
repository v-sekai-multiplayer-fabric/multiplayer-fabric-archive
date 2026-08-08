/*
 * Does zstd dictionary delta make the wire format cheap enough?
 *
 * rfd/0100 left an open question: a rotation-only bone carries 24 bytes
 * of unused position in xr_grid_entity_packet_t, and the fix was either
 * a second packet type or a flag.
 *
 * Neither, if delta compression works. Unused position is IDENTICAL on
 * every tick, so a delta against a baseline should cost almost nothing
 * for those bytes. This measures whether that holds.
 *
 * The precedent is godotengine/godot#112011, which shipped zstd
 * --patch-from delta encoding in Godot 4.6 after it beat bsdiff+zstd.
 * casync already uses zstd here too.
 *
 * The unreliable-transport catch, and why baseline choice matters:
 * a datagram can be lost. Delta against the PREVIOUS tick breaks the
 * moment one is dropped. Delta against the last ACKED tick survives it,
 * at the cost of a larger delta. Both are measured.
 *
 * Layout follows src/gen/xr_grid_entity_packet.h: XR_PACKET_SIZE 100,
 * int64 position micrometers, int16 velocity, int16 swing-twist
 * rotation. 56 entities per avatar per rfd/0100: 1 root, 1 hips, 54
 * rotation-only bones.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zstd.h>

#define PACKET   100
#define BONES    56
#define AVATARS  8
#define ENTITIES (BONES * AVATARS)
#define FRAME    (ENTITIES * PACKET)
#define TICKS    400
#define SEND_HZ  20

static uint64_t now_ns(void)
{
	struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
static int cmp(const void *a, const void *b)
{ uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y); }

static void put64(uint8_t *p, int64_t v){ memcpy(p, &v, 8); }
static void put16(uint8_t *p, int16_t v){ memcpy(p, &v, 2); }
static void put32(uint8_t *p, uint32_t v){ memcpy(p, &v, 4); }

/*
 * Build one tick of realistic-ish state.
 *  - root and hips translate, walking forward with a vertical bob.
 *  - the other 54 bones rotate only, and their position stays FIXED,
 *    which is exactly the redundancy delta compression should remove.
 */
static void build_frame(uint8_t *buf, int tick)
{
	memset(buf, 0, FRAME);
	for (int a = 0; a < AVATARS; a++) {
		for (int b = 0; b < BONES; b++) {
			uint8_t *p = buf + (a * BONES + b) * PACKET;
			put32(p + 0, (uint32_t)(a * BONES + b));       /* global_id */
			int is_root = (b == 0), is_hips = (b == 1);
			if (is_root || is_hips) {
				/* walking, so position genuinely changes */
				put64(p + 4,  (int64_t)tick * 12000 + a * 2000000);
				put64(p + 12, (int64_t)(is_hips ? 900000 + (tick % 8) * 1500 : 0));
				put64(p + 20, (int64_t)a * 1500000);
				put16(p + 28, 400); put16(p + 30, 0); put16(p + 32, 0);
			} else {
				/* rotation-only bone: position never changes.
				 * These 24 bytes are the waste rfd/0100 named. */
				put64(p + 4, 0); put64(p + 12, 0); put64(p + 20, 0);
			}
			/* swing-twist rotation, smooth motion per bone */
			int ph = tick * 3 + b * 7 + a * 11;
			put16(p + 34, (int16_t)(3000 * ((ph % 64) - 32) / 32));
			put16(p + 36, (int16_t)(1500 * ((ph % 48) - 24) / 24));
			put16(p + 38, (int16_t)(800  * ((ph % 32) - 16) / 16));
			put32(p + 40, (uint32_t)((tick << 8) | (b & 0xff))); /* hlc */
			put32(p + 44, (uint32_t)((1u << 24) | (uint32_t)a));  /* class_owner */
			put32(p + 48, (uint32_t)b);                           /* sub_index */
		}
	}
}

static void report(const char *name, uint64_t *s, int n, size_t bytes, int frames)
{
	qsort(s, n, sizeof(uint64_t), cmp);
	double per_tick = (double)bytes / frames;
	double kbps = per_tick * SEND_HZ * 8.0 / 1000.0;
	printf("WIRE %-34s %8.0f B/tick  %7.1f kbps  cpu_median=%6.1f us\n",
	       name, per_tick, kbps, s[n / 2] / 1000.0);
	fflush(stdout);
}

int main(void)
{
	printf("WIRE ==== %d avatars x %d entities x %d B = %d B raw per tick, sent at %d Hz ====\n",
	       AVATARS, BONES, PACKET, FRAME, SEND_HZ);
	fflush(stdout);

	uint8_t *cur = malloc(FRAME), *base = malloc(FRAME);
	size_t cap = ZSTD_compressBound(FRAME);
	uint8_t *out = malloc(cap);
	uint64_t *t = malloc(sizeof(uint64_t) * TICKS);

	/* 0. raw, no compression at all */
	printf("WIRE %-34s %8d B/tick  %7.1f kbps  (baseline)\n", "raw, no compression",
	       FRAME, (double)FRAME * SEND_HZ * 8.0 / 1000.0);

	/* 1. zstd alone, no dictionary */
	for (int lvl = 1; lvl <= 3; lvl += 2) {
		size_t total = 0; int n = 0;
		ZSTD_CCtx *c = ZSTD_createCCtx();
		for (int i = 0; i < TICKS; i++) {
			build_frame(cur, i);
			uint64_t t0 = now_ns();
			size_t r = ZSTD_compressCCtx(c, out, cap, cur, FRAME, lvl);
			t[n++] = now_ns() - t0;
			total += r;
		}
		char nm[64]; snprintf(nm, sizeof(nm), "zstd level %d, no dictionary", lvl);
		report(nm, t, n, total, TICKS);
		ZSTD_freeCCtx(c);
	}

	/* 2. delta against the PREVIOUS tick (breaks on packet loss) */
	for (int lvl = 1; lvl <= 3; lvl += 2) {
		size_t total = 0; int n = 0;
		ZSTD_CCtx *c = ZSTD_createCCtx();
		build_frame(base, 0);
		for (int i = 1; i < TICKS; i++) {
			build_frame(cur, i);
			uint64_t t0 = now_ns();
			ZSTD_CCtx_reset(c, ZSTD_reset_session_only);
			ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, lvl);
			ZSTD_CCtx_refPrefix(c, base, FRAME);
			size_t r = ZSTD_compress2(c, out, cap, cur, FRAME);
			t[n++] = now_ns() - t0;
			total += r;
			memcpy(base, cur, FRAME);
		}
		char nm[64]; snprintf(nm, sizeof(nm), "zstd L%d, prefix = prev tick", lvl);
		report(nm, t, n, total, TICKS - 1);
		ZSTD_freeCCtx(c);
	}

	/* 3. delta against the last ACKED tick. Survives loss.
	 *    Modelled as a baseline that lags by N ticks. */
	for (int lag = 2; lag <= 8; lag *= 2) {
		size_t total = 0; int n = 0;
		ZSTD_CCtx *c = ZSTD_createCCtx();
		for (int i = lag; i < TICKS; i++) {
			build_frame(base, i - lag);
			build_frame(cur, i);
			uint64_t t0 = now_ns();
			ZSTD_CCtx_reset(c, ZSTD_reset_session_only);
			ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, 1);
			ZSTD_CCtx_refPrefix(c, base, FRAME);
			size_t r = ZSTD_compress2(c, out, cap, cur, FRAME);
			t[n++] = now_ns() - t0;
			total += r;
		}
		char nm[64]; snprintf(nm, sizeof(nm), "zstd L1, prefix = acked -%d ticks", lag);
		report(nm, t, n, total, TICKS - lag);
		ZSTD_freeCCtx(c);
	}

	/* 4. Decompression cost, which is the client's problem. */
	{
		ZSTD_CCtx *c = ZSTD_createCCtx();
		ZSTD_DCtx *d = ZSTD_createDCtx();
		uint8_t *back = malloc(FRAME);
		build_frame(base, 100); build_frame(cur, 101);
		ZSTD_CCtx_refPrefix(c, base, FRAME);
		size_t r = ZSTD_compress2(c, out, cap, cur, FRAME);
		int n = 0;
		for (int i = 0; i < TICKS; i++) {
			uint64_t t0 = now_ns();
			ZSTD_DCtx_reset(d, ZSTD_reset_session_only);
			ZSTD_DCtx_refPrefix(d, base, FRAME);
			size_t got = ZSTD_decompressDCtx(d, back, FRAME, out, r);
			t[n++] = now_ns() - t0;
			if (got != FRAME) { printf("WIRE decompress MISMATCH\n"); break; }
		}
		qsort(t, n, sizeof(uint64_t), cmp);
		printf("WIRE %-34s decompress cpu_median=%6.1f us, verify=%s\n",
		       "client side", t[n / 2] / 1000.0,
		       memcmp(back, cur, FRAME) == 0 ? "EXACT" : "CORRUPT");
		ZSTD_freeCCtx(c); ZSTD_freeDCtx(d); free(back);
	}

	printf("WIRE ==== done ====\n");
	return 0;
}
