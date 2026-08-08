/*
 * Fly.io capability + latency probe for the zone guest transport choice.
 *
 * Answers, on the real machine rather than from documentation:
 *   1. Is io_uring reachable?           io_uring_setup(2)
 *   2. Does memfd_create work?          needed for SCM_RIGHTS bulk path
 *   3. AF_UNIX SOCK_SEQPACKET RTT       the issue #31 default
 *   4. Busy-polled SPSC shm ring RTT    the "270 ns" claim, on shared-cpu-1x
 *   5. Does binding in6addr_any UDP differ from fly-global-services?
 *
 * Every number is a round trip between two processes on this machine.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ITERS 20000

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

static void report(const char *name, uint64_t *s, int n)
{
	qsort(s, n, sizeof(*s), cmp_u64);
	printf("PROBE %-28s median=%6llu ns  p99=%8llu ns  min=%6llu ns\n",
	       name, (unsigned long long)s[n / 2],
	       (unsigned long long)s[(int)(n * 0.99)],
	       (unsigned long long)s[0]);
	fflush(stdout);
}

/* --- 1. io_uring ------------------------------------------------------ */
struct io_uring_params_stub { uint32_t pad[16]; uint64_t pad2[8]; };

static void probe_io_uring(void)
{
	char buf[8] = {0};
	int fd = open("/proc/sys/kernel/io_uring_disabled", O_RDONLY);
	if (fd >= 0) {
		ssize_t n = read(fd, buf, sizeof(buf) - 1);
		if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = 0;
		close(fd);
		printf("PROBE io_uring_disabled sysctl = %s\n", buf);
	} else {
		printf("PROBE io_uring_disabled sysctl = ABSENT (errno %d)\n", errno);
	}

	struct io_uring_params_stub p;
	memset(&p, 0, sizeof(p));
	long r = syscall(425 /* io_uring_setup */, 8, &p);
	if (r >= 0) {
		printf("PROBE io_uring_setup           = OK (fd %ld) -- REACHABLE\n", r);
		close((int)r);
	} else {
		printf("PROBE io_uring_setup           = FAIL errno=%d (%s)\n",
		       errno, strerror(errno));
	}
	fflush(stdout);
}

/* --- 2. memfd --------------------------------------------------------- */
static void probe_memfd(void)
{
	int fd = (int)syscall(SYS_memfd_create, "probe", 0);
	if (fd >= 0) {
		printf("PROBE memfd_create             = OK -- SCM_RIGHTS bulk path viable\n");
		close(fd);
	} else {
		printf("PROBE memfd_create             = FAIL errno=%d (%s)\n",
		       errno, strerror(errno));
	}
	fflush(stdout);
}

/* --- 3. AF_UNIX SOCK_SEQPACKET round trip ----------------------------- */
static void probe_unix(int type, const char *label)
{
	int sv[2];
	if (socketpair(AF_UNIX, type, 0, sv) < 0) {
		printf("PROBE %-28s FAIL socketpair errno=%d\n", label, errno);
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		close(sv[0]);
		char b[32];
		for (int i = 0; i < ITERS; i++) {
			if (read(sv[1], b, sizeof(b)) <= 0) _exit(1);
			if (write(sv[1], b, 32) < 0) _exit(1);
		}
		_exit(0);
	}
	close(sv[1]);
	uint64_t *s = malloc(sizeof(uint64_t) * ITERS);
	char b[32] = {0};
	for (int i = 0; i < ITERS; i++) {
		uint64_t t0 = now_ns();
		if (write(sv[0], b, 32) < 0) break;
		if (read(sv[0], b, sizeof(b)) <= 0) break;
		s[i] = now_ns() - t0;
	}
	report(label, s, ITERS);
	free(s);
	close(sv[0]);
	waitpid(pid, NULL, 0);
}

/* --- 4. busy-polled SPSC shared-memory ring --------------------------- */
struct ring {
	atomic_uint_least64_t req;
	atomic_uint_least64_t rsp;
	char payload[32];
};

static void probe_shm_ring(void)
{
	struct ring *r = mmap(NULL, sizeof(*r), PROT_READ | PROT_WRITE,
	                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (r == MAP_FAILED) {
		printf("PROBE shm ring                 FAIL mmap errno=%d\n", errno);
		return;
	}
	atomic_store(&r->req, 0);
	atomic_store(&r->rsp, 0);

	pid_t pid = fork();
	if (pid == 0) {
		uint64_t seen = 0;
		for (;;) {
			while (atomic_load_explicit(&r->req, memory_order_acquire) == seen) {
				/* spin */
			}
			seen = atomic_load_explicit(&r->req, memory_order_acquire);
			if (seen == UINT64_MAX) _exit(0);
			atomic_store_explicit(&r->rsp, seen, memory_order_release);
		}
	}
	uint64_t *s = malloc(sizeof(uint64_t) * ITERS);
	for (int i = 1; i <= ITERS; i++) {
		uint64_t t0 = now_ns();
		atomic_store_explicit(&r->req, (uint64_t)i, memory_order_release);
		while (atomic_load_explicit(&r->rsp, memory_order_acquire) != (uint64_t)i) {
			/* spin */
		}
		s[i - 1] = now_ns() - t0;
	}
	atomic_store_explicit(&r->req, UINT64_MAX, memory_order_release);
	report("shm ring (busy-poll)", s, ITERS);
	free(s);
	waitpid(pid, NULL, 0);
	munmap(r, sizeof(*r));
}

/* --- 5. UDP bind behaviour ------------------------------------------- */
static void probe_udp(void)
{
	int fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd < 0) { printf("PROBE udp socket FAIL errno=%d\n", errno); return; }
	int off = 0;
	setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
	struct sockaddr_in6 a;
	memset(&a, 0, sizeof(a));
	a.sin6_family = AF_INET6;
	a.sin6_port = htons(7443);
	a.sin6_addr = in6addr_any;
	printf("PROBE udp bind in6addr_any     = %s\n",
	       bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0 ? "OK (binds, but Fly says replies use the wrong source)" : "FAIL");
	close(fd);

	struct addrinfo hints, *res = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	int rc = getaddrinfo("fly-global-services", "7443", &hints, &res);
	if (rc != 0 || !res) {
		printf("PROBE fly-global-services      = UNRESOLVABLE (%s)\n", gai_strerror(rc));
		return;
	}
	char host[NI_MAXHOST] = {0};
	getnameinfo(res->ai_addr, res->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST);
	int f2 = socket(res->ai_family, SOCK_DGRAM, 0);
	int ok = f2 >= 0 && bind(f2, res->ai_addr, res->ai_addrlen) == 0;
	printf("PROBE fly-global-services      = %s, family=%s, bind=%s\n",
	       host, res->ai_family == AF_INET6 ? "AF_INET6" : "AF_INET",
	       ok ? "OK" : "FAIL");
	if (f2 >= 0) close(f2);
	freeaddrinfo(res);
	fflush(stdout);
}

int main(void)
{
	printf("PROBE ==== zone transport probe on Fly ====\n");
	fflush(stdout);
	probe_io_uring();
	probe_memfd();
	probe_udp();
	probe_unix(SOCK_SEQPACKET, "AF_UNIX SOCK_SEQPACKET");
	probe_unix(SOCK_STREAM, "AF_UNIX SOCK_STREAM");
	probe_shm_ring();
	printf("PROBE ==== done ====\n");
	fflush(stdout);
	return 0;
}
