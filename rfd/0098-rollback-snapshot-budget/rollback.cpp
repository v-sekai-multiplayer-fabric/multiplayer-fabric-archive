/*
 * Does libriscv snapshot fast enough for 64 Hz rollback?
 *
 * rfd/0095 says libriscv holds the rollback path because serialize_to
 * (machine.hpp:431) and forking (machine.hpp:61) give bit-exact
 * snapshot and replay. That claim was never measured. This measures it.
 *
 * The rollback budget, stated up front:
 *   - One tick at 64 Hz is 15625 us.
 *   - Compensating 100 ms means rewinding about 7 ticks.
 *   - So about 7 snapshots stay live, and one is taken every tick.
 *
 * Reported per guest:
 *   1. serialize_to time and snapshot SIZE
 *   2. deserialize_from time (the rewind)
 *   3. fork construction time (the copy-on-write alternative)
 *   4. re-simulation cost for a 7-tick rewind
 *   5. DETERMINISM: same input twice must give identical register state
 */
#include <libriscv/machine.hpp>

#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <vector>
#include <fstream>

using namespace riscv;
using Mach = Machine<RISCV64>;

static uint64_t now_ns()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<nanoseconds>(
	           steady_clock::now().time_since_epoch()).count();
}

static void report(const char *name, std::vector<uint64_t> &s)
{
	if (s.empty()) { printf("RB %-30s NO DATA\n", name); return; }
	std::sort(s.begin(), s.end());
	printf("RB %-30s median=%9.1f us  p99=%9.1f us  n=%zu\n",
	       name, s[s.size() / 2] / 1000.0,
	       s[(size_t)(s.size() * 0.99)] / 1000.0, s.size());
	fflush(stdout);
}

static std::vector<uint8_t> load(const char *path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
	                             std::istreambuf_iterator<char>());
}

static void measure(const char *label, const char *path, uint64_t mem_mb)
{
	auto elf = load(path);
	if (elf.empty()) { printf("RB %s: cannot read %s\n", label, path); return; }

	printf("RB ---- %s (%zu byte ELF, %llu MB arena) ----\n",
	       label, elf.size(), (unsigned long long)mem_mb);
	fflush(stdout);

	MachineOptions<RISCV64> opts;
	opts.memory_max = mem_mb << 20;
	/* serialize_to REFUSES to run with libriscv's default flat
	 * read-write arena: serialize.cpp:99 throws FEATURE_DISABLED,
	 * "Serialize is incompatible with flat read-write arena". So
	 * snapshotting costs the arena, and the arena is the fast path.
	 * That trade is itself a finding. Measure both. */
	opts.use_memory_arena = false;
	Mach m(elf, opts);
	m.setup_minimal_syscalls();
	m.setup_linux({"guest"}, {"LC_ALL=C"});
	/* Every ecall this guest makes is unimplemented here. Answer -ENOSYS
	 * and keep running: we are measuring snapshots, not guest logic. */
	Mach::on_unhandled_syscall = [](Mach &mm, size_t) { mm.set_result(-38); };

	/* Warm the machine so the snapshot covers touched pages, not a
	 * pristine arena. A snapshot of an unstarted machine flatters the
	 * result. */
	try { m.simulate(2'000'000); } catch (...) {}

	std::vector<uint64_t> ser, deser, forks;
	std::vector<uint8_t> snap;
	size_t snap_size = 0;

	for (int i = 0; i < 200; i++) {
		snap.clear();
		uint64_t t0 = now_ns();
		snap_size = m.serialize_to(snap);
		ser.push_back(now_ns() - t0);
	}
	printf("RB %-30s SIZE = %zu bytes (%.2f MB)\n",
	       "snapshot", snap_size, snap_size / 1048576.0);
	printf("RB %-30s 7 live snapshots = %.2f MB\n",
	       "rewind window", 7.0 * snap_size / 1048576.0);
	fflush(stdout);
	report("serialize_to", ser);

	for (int i = 0; i < 200; i++) {
		uint64_t t0 = now_ns();
		int rc = m.deserialize_from(snap);
		deser.push_back(now_ns() - t0);
		if (rc != 0) { printf("RB deserialize_from returned %d\n", rc); break; }
	}
	report("deserialize_from (rewind)", deser);

	for (int i = 0; i < 200; i++) {
		uint64_t t0 = now_ns();
		{
			Mach fork(m, opts);
			forks.push_back(now_ns() - t0);
		}
	}
	report("fork construct (CoW)", forks);

	/* 4. What a 7-tick rewind costs: restore, then re-simulate 7 ticks
	 *    of instructions. 1 M instructions per tick is a guess for a
	 *    kinematic step, and it is the unit, not a claim. */
	std::vector<uint64_t> rewind;
	for (int i = 0; i < 50; i++) {
		uint64_t t0 = now_ns();
		m.deserialize_from(snap);
		for (int t = 0; t < 7; t++) {
			try { m.simulate(1'000'000); } catch (...) {}
		}
		rewind.push_back(now_ns() - t0);
	}
	report("rewind 7 ticks @1M instr", rewind);

	/* 5. Determinism. Restore the same snapshot twice, run the same
	 *    instructions, and compare every integer register. Rollback is
	 *    worthless if this differs. */
	auto run_and_hash = [&]() {
		m.deserialize_from(snap);
		try { m.simulate(3'000'000); } catch (...) {}
		uint64_t h = 1469598103934665603ull;
		for (int r = 0; r < 32; r++) {
			uint64_t v = m.cpu.reg(r);
			h = (h ^ v) * 1099511628211ull;
		}
		h = (h ^ m.cpu.pc()) * 1099511628211ull;
		return h;
	};
	uint64_t h1 = run_and_hash();
	uint64_t h2 = run_and_hash();
	printf("RB %-30s %s (%016llx vs %016llx)\n", "determinism (2 replays)",
	       h1 == h2 ? "IDENTICAL" : "DIVERGED",
	       (unsigned long long)h1, (unsigned long long)h2);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	printf("RB ==== libriscv snapshot cost against a 15625 us tick ====\n");
	fflush(stdout);
	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			uint64_t mb = (argc > 2 && i == 2) ? 512 : 64;
			measure(argv[i], argv[i], mb);
		}
	}
	printf("RB ==== done ====\n");
	return 0;
}
