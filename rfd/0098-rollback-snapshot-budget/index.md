---
title: "RFD 0098: The rollback snapshot budget, measured"
rfd: "0098"
state: discussion
scope: zone-server-h2o, zone-guest-godot
---

## Problem

`rfd/0095` puts hybrid rollback-kinematic networking on libriscv. The
stated reason is that `serialize_to` (`machine.hpp:431`) and forking
(`machine.hpp:61`) give bit-exact snapshot and replay, and that a Linux
process cannot.

That reason is correct and it was never measured. This RFD measures it.

The measurement matters because rollback has a hard budget. One tick at
64 Hz is 15625 us. Lag compensation of 100 ms rewinds about 7 ticks.
So the host takes one snapshot every tick and holds about 7 of them.

If a snapshot costs more than a tick, rollback is impossible. If 7
snapshots do not fit in memory, rollback is impossible. Neither number
was known.

## Prior art

This approach is not new, and the reason to use a virtual machine is
well understood.

Glenn Fiedler rejects deterministic lockstep for a first-person shooter
on two grounds. Float determinism across compilers, operating systems
and instruction sets is nearly impossible. Also, every player waits for
the most lagged player.

A virtual machine answers the first objection, because the guest
executes the same instructions everywhere. Rollback answers the second,
because rollback predicts and re-simulates instead of waiting.

WebAssembly is the common choice for this. `bevy_ggrs` runs rollback
under WASM, and Easel builds a language that guarantees determinism.
The requirement is the same in every case: snapshot the state, restore
it, and re-simulate.

No prior art was found for RISC-V as the determinism boundary. The
technique is established. The choice of libriscv rather than WASM is
what `rfd/0095` decides, and what this RFD tests.

## Method

The probe loads a guest ELF, runs 2 million instructions to warm it,
then measures `serialize_to`, `deserialize_from`, and fork construction
200 times each. It reports the median and the 99th percentile.

It also replays the same snapshot twice and hashes all 32 integer
registers plus the program counter. Rollback is worthless if two
replays differ.

Two guests, chosen as the two ends of the range:

- `kv_smoke.elf`, a 4016 byte script guest.
- `godot_instance_harness`, an 84982776 byte engine guest.

Both ran with a 64 MB arena.

## Finding 0: serialize_to needs the fast path turned off

`serialize_to` refuses to run against libriscv's default memory
configuration. `serialize.cpp:99` throws `FEATURE_DISABLED`, with the
message `Serialize is incompatible with flat read-write arena`.

So `MachineOptions::use_memory_arena` must be false to snapshot at all.
The flat arena is the fast memory path. Snapshotting costs it.

Every number below therefore describes a machine that already gave up
the arena.

## Data

| Measure                   | Script guest, 4016 B | Engine guest, 84982776 B  |
| ------------------------- | -------------------- | ------------------------- |
| Snapshot size             | 41864 B (0.04 MB)    | 67148424 B (**64.04 MB**) |
| 7 live snapshots          | 0.28 MB              | **448.26 MB**             |
| `serialize_to` median     | 0.8 us               | **12370.2 us**            |
| `serialize_to` p99        | 1.2 us               | **17063.0 us**            |
| `deserialize_from` median | 1.9 us               | 15227.0 us                |
| Fork construct median     | 0.3 us               | **384.2 us**              |
| Determinism, 2 replays    | IDENTICAL            | IDENTICAL                 |

## Decisions

### 1. Determinism holds, and that was the core claim

Two replays of the same snapshot produced identical register state and
program counter on both guests. `rfd/0095`'s central argument survives
measurement.

### 2. `serialize_to` cannot snapshot an engine guest

The engine guest takes 12370 us to serialize. One tick is 15625 us. So
a single snapshot consumes 79 percent of a tick at the median.

At the 99th percentile it takes 17063 us, which is longer than the
whole tick. The snapshot alone misses the deadline.

Memory disqualifies it a second time. Seven live snapshots need 448.26
MB. A Fly machine in `fly/fly.toml` has 256 MB, and `rfd/0096` measured
212188 kB of that as usable. The rewind window does not fit.

### 3. Snapshot cost tracks the working set, so cap the working set

The script guest snapshots to 41864 bytes from the same 64 MB arena
that produced 64.04 MB for the engine guest. Size follows what the
guest touches, not what it may address.

Both figures divide out near memory bandwidth, at roughly 5 to 50 GB
per second. Snapshot cost is a memory copy, so it is predictable:

    snapshot_us ~= working_set_bytes / 5 GB per second

Turned into a budget: keep snapshot cost under 10 percent of a tick,
which is 1562 us. That allows a working set of approximately 8 MB.

**8 MB is the rollback guest budget.** A program on the rollback path
must fit it. That is a design constraint on the guest, and it is not a
tuning parameter of the host.

### 4. Prefer forking, and treat `serialize_to` as the fallback

Fork construction costs 384.2 us for the engine guest, against 12370.2
us to serialize the same machine. That is roughly 32 times faster,
because copy-on-write does not copy untouched pages.

For the script guest it is 0.3 us against 0.8 us.

Fork is therefore the mechanism for the rollback window. `serialize_to`
stays useful for a durable snapshot that must outlive the process, and
for moving a machine between hosts.

### 5. Engine guests do not roll back, and that is already the design

`rfd/0095` puts engine guests under Bubblewrap as separate processes.
They were never on the rollback path.

This measurement confirms that boundary rather than moving it. An
engine-sized guest cannot meet the rollback budget under libriscv
either. The split was right, and the reason is now numeric.

## Open question: a rolling window of forks

`machine.hpp:61` states that the main machine must outlive its forks,
and that it must not be modified while a fork runs.

A rollback window needs about 7 snapshots at different ticks. Whether
that is expressible as a chain of forks, or needs one frozen parent per
tick, is not established here. It was not measured.

Resolve this before building the rollback window, because decision 4
depends on it.

## Consequences

The rollback path carries an explicit size limit of approximately 8 MB
of working set. State that limit in the guest ABI documentation, so a
guest author learns it before writing a program that cannot roll back.

`use_memory_arena` must be false on any machine that will snapshot.
Measure the cost of losing the arena before enabling snapshots on a hot
path.

`rfd/0095` claimed `serialize_to` and forking as equivalent evidence for
the same argument. They are not equivalent. Forking is roughly 32 times
faster on an engine-sized machine, and `serialize_to` cannot meet the
tick deadline there at all.

## Sources

Probe source is `rollback.cpp` beside this RFD.

- [Floating Point Determinism, Gaffer On Games](https://gafferongames.com/post/floating_point_determinism/)
- [Deterministic Lockstep, Gaffer On Games](https://gafferongames.com/post/deterministic_lockstep/)
- [mas-bandwidth/fps](https://github.com/mas-bandwidth/fps)
- [Extreme Bevy, rollback in Rust and WASM](https://johanhelsing.studio/posts/extreme-bevy)
- [Easel, deterministic rollback netcode](https://easel.games/docs/learn/multiplayer/rollback-netcode)
