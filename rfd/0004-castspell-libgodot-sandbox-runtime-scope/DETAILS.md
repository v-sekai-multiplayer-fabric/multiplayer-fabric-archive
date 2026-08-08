## Summary

`rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps/index.md`, item 6, commits to
scoping how much of `libriscv`/`godot-sandbox`'s API surface CastSpell
effects actually need. This RFD is the detail that item pointed to. It
resolves the scoping question and decides to embed a real, headless
`libgodot` instance per zone instead of reimplementing
`godot-sandbox`'s narrow API. It also records a real spike that proved
the approach boots correctly.

## Background

`godot-sandbox`'s guest-side API (`program/cpp/docker/api/`) is not a
local math library. Reading it directly settles this. Most of that
API, `Vector3`'s own named methods (`abs`, `bezier_interpolate`,
`lerp`, `slerp`, and similar), all of `Array`, `Dictionary`, `String`,
and everything under `node.hpp`/`node2d.hpp`/`node3d.hpp`/`object.hpp`,
expands through the `METHOD`/`VMETHOD` macros. Those macros build a
named, string-dispatched syscall (`operator()`) that calls back into a
live, running Godot process on the host side to actually execute.

Confirmed directly in `program/cpp/docker/api/vector.cpp`:
`Vector3::dot()`, `cross()`, `length()`, and similar are hand-written
inline RISC-V assembly issuing `ecall` (syscall `ECALL_VEC3_OPS`), not
local computation. `Basis`, `Quaternion`, and `Transform3D` are
entirely syscall-based too, with zero locally-computed math at all.
Every method routes through a `MAKE_SYSCALL(...)`-generated call
carrying an opaque host-side registry index. This confirms these
types are thin remote handles in `godot-sandbox`'s own design, not
real local value types.

`zone-server-h2o` runs no live Godot process outside the sandbox
itself, so it cannot answer these syscalls the way a real Godot host
does.

## Motivation

CastSpell effects never touch a live Node or scene tree in the
first place. They only read and write zonefabric's own entity data:
position, velocity, health. Nothing here needs
`Node`/`Node2D`/`Node3D`/`Object`/`Callable`/`Timer` at all. This
narrows what any runtime choice below actually needs to cover.

## Proposal

### Embed `libgodot` per zone

Decide the runtime this way. Embed a real, headless Godot engine
instance, via `libgodot` (`core/extension/libgodot.h`,
`GodotInstance`, merged in Godot 4.6), inside each CastSpell
`libriscv` sandbox `Machine`, one instance per zone. This matches
`libriscv`'s Linux syscall emulation ABI rather than
`godot-sandbox`'s own narrow API. `GodotInstance`'s documented
"only one per process" constraint maps cleanly onto one `libriscv`
`Machine` per zone, since each sandboxed guest is already its own
isolated process-like boundary.

Build this from `fabric-godot-core`'s own pinned release tag,
`v2026.06.27.1907-multiplayer-fabric` (Godot 4.7.0-beta per that tag's
own `version.py`), not upstream `godotengine/godot` generically. Reading
directly at that exact tag confirms all three load-bearing pieces this
decision needs are present: `core/extension/libgodot.h`, `rv64` in
`platform/linuxbsd/detect.py`'s `supported_arches`, and
`modules/sandbox` (vendored as its own subrepo). None of this is a
"does our fork have this yet" open question. It already does.

### Build configuration

Build headless (`DisplayServerHeadless`, a real, shipped dummy
display backend, removing the X11/Wayland/D-Bus/fontconfig/
speech-dispatcher dependencies `platform/linuxbsd` otherwise pulls in
via `dlopen`), for `arch=rv64`. `rv64` is already an existing Godot
Linux architecture target in this exact pinned tag, not a new
platform port. `supported_arches` already lists `"rv64"` alongside
`x86_32`/`x86_64`/`arm32`/`arm64`/`ppc64`/`loongarch64`, with real
RISC-V compiler flags (`-march=rv64gc`) already wired in. This is the
same `platform/linuxbsd` code every other Linux architecture uses. It
needs building the existing, already-supported headless target for
`arch=rv64`, not designing a new platform backend.

Disable `modules/sandbox` for this one embedded-build target
specifically, even though the fork's normal client build keeps it
enabled. That module lets a normally-running Godot client host further
`libriscv`-sandboxed guest programs. The embedded CastSpell instance
already runs as a `libriscv` guest itself. Compiling `modules/sandbox`
into it would nest a second sandboxing layer inside the first, adding
binary size and surface for no requirement that calls for it. Exclude
it via SCons `disable_modules=sandbox` or an
equivalent `custom.py`/`modules.cfg` entry for this target only.

Build with `threads=no`, Godot's own single-thread build mode. The
Web/WASM export already proves this mode in production. `libriscv`'s
thread model needs it too. `lib/libriscv/threads.hpp` and both
`native_threads.cpp` and the fuller `posix/threads.cpp` confirm the
model is single-host-thread, cooperative, register-swapping
microthreading in both cases, not real concurrent host threads.
Godot's default build assumes genuine parallel execution instead
(`WorkerThreadPool` behind physics and rendering).

Run real audio headless, not silence. Reading
`servers/audio/audio_driver_dummy.cpp` directly confirms
`AudioDriverDummy` is not a stub. Its `mix_audio()` calls the real
`audio_server_process()` pipeline, genuine bus mixing, effects,
panning, producing real PCM. The driver already exposes
`set_use_threads(bool)`. When threading is off, `mix_audio()` is the
documented, guarded synchronous path
(`ERR_FAIL_COND(use_threads == true)` inside it). The embedding host
should poll that path directly, not run it on a background thread.

Call `set_use_threads(false)`, and poll `mix_audio()` from the
same tick loop that calls `iteration()`, to get real mixed audio
headless. The Web export's documented audio glitching traces to that
platform's real-time `AudioWorklet` output driver. It does not carry
over to this already-synchronous `Dummy` driver path.

Virtualize filesystem access, and stub or virtualize networking,
through `libriscv`'s own broad Linux syscall-emulation tier,
`Machine<W>::setup_linux_syscalls(filesystem, sockets)`
(`lib/libriscv/linux/system_calls.cpp`), not through
`godot-sandbox`'s own narrow API. This gives real, host-mediated
`openat`/`read`/`mmap`-backed file access (with host-side path
whitelisting) and full socket calls, already proven against a real
statically linked `riscv64-unknown-linux-gnu` binary in
`libriscv/libriscv`'s own `binaries/linux64/` example.

Avoid runtime `dlopen`. `libriscv`'s own docs state dynamic library
loading needs extra host-side whitelisting, not out-of-the-box
support. Statically link every GDExtension a CastSpell instance needs
at guest-build time instead.

### The spike: part 1 (boot and run) is done

The spike this item gates on ran, on real hardware, against real
toolchains, not just source-reading. Part (1), boot and run correctly
under `libriscv`, stands resolved and achieved. A minimal host
program handled it: it calls `libgodot_create_godot_instance()`
directly, casts the result to a real `GodotInstance*`, then runs
`start()` and a manual loop of `iteration()` calls. This exactly
matches the shape this RFD already commits to (`zone-server-h2o`'s
own tick loop driving the engine, not Godot's own `main()`). It
booted the pinned `fabric-godot-core` tag for real inside `libriscv`'s
actual sandbox (`rvlinux`, not `qemu-riscv64`). The run produced real
`_ready()`/`_process()` script output, five real `iteration()` calls,
and a clean shutdown, confirmed byte for byte via a host-level
`strace -f` capture.

Getting there needed two changes: switching the embedded engine's own
guest libc from glibc to musl, and seven fixes to `libriscv` itself.
All seven fixes are real and upstream-worthy. None of them change
Godot's own code.

A genuine glibc `tcache` heap-consistency defect blocked every
attempt under glibc specifically. Disassembly down to the exact
faulting instruction confirmed this defect. It is independent of
`threads=yes` versus `threads=no`. Musl has no comparable `tcache`
mechanism, so switching to it sidesteps that defect entirely. The cost
is that musl still needs to reach real POSIX-completeness parity with
glibc as CastSpell's actual requirements grow.

The `libriscv` fixes cover four areas:

- Guest pipes forced non-blocking. A real host-level block on one
  syscall would otherwise stall `libriscv`'s single-threaded
  cooperative scheduler indefinitely.
- The real host fds 0/1/2 permanently reserved at startup. A
  coincidental fd-number collision was silently swallowing
  `libriscv`'s own crash reports at exactly the moment they mattered.
- `mkdirat`/`chdir` implemented, previously entirely missing.
  Godot's own `user://` data-directory setup needs both calls.
- A real, load-bearing bug fixed in `libriscv`'s own fd-translation
  layer. That bug collapsed the `AT_FDCWD` sentinel to an invalid
  `-1` instead of passing it through, breaking every relative-path
  `*at()` call while leaving absolute-path calls (the only ones
  exercised until this spike) unaffected.

One of `libriscv`'s own open, unresolved upstream issues,
`libriscv/libriscv#296`, independently reports the same underlying
symptom class this spike's fixes resolve.

Full detail, the reproducible spike script, and the `libriscv` patch
itself live at `v-sekai-multiplayer-fabric/godot-riscv-spike`, a
separate repository this RFD points to rather than duplicates. Audio
(`AudioDriverDummy`'s polled `mix_audio()` path) was not exercised in
this specific spike, since the harness used a
headless-with-no-audio-driver-needed test scene. That remains to
confirm separately once real implementation starts.

### The spike: part 2 (performance) is not done

Part (2), performance measurement against the real 10Hz/200-entity/
many-zones budget, is not yet done. No performance numbers exist yet
for this exact configuration, in Godot's own docs, in `libriscv`'s own
benchmarks (which measure tiny guest calls, not an engine-sized
binary), or from this spike (which validated correctness, not speed,
under an interpreted, non-JIT `libriscv` configuration throughout).
This stays the actual gate before real implementation work starts.

### Maintainer feedback

`fwsgonzo` (`libriscv`/`godot-sandbox`'s maintainer) confirmed this
direction directly. It is technically achievable, and open to a real
PR, conditioned on care and an explicit disable option, matching "the
ethos of `godot-sandbox`."

Three concrete implications follow for the eventual implementation,
not yet built:

- Virtual filesystem access should default-deny, scoped per
  `Sandbox` instance. It should build on `libriscv`'s existing
  `-A/--allow <file>` allowlist and its `sandbox_libdir`/`real_libdir`
  restriction pattern (`emulator/src/main.cpp`), not a broad proxy to
  the real host filesystem.
- Networking should stay off by default, reachable only through an
  explicit host-side opt-in via `ADD_API_FUNCTION`-style host-mediated
  calls. This matches `godot-sandbox`'s own existing pattern, not raw
  guest-visible socket syscalls.
- The embedded-engine mode should be a distinct, explicitly opt-in
  mode layered on top of `godot-sandbox`'s existing narrow API, not a
  change to that default. This gives it a real off switch.

Signal handling already has real, working coverage (`sigaction`,
`sigaltstack`, `tkill`, `tgkill`, confirmed via direct source read).
It is stronger than expected going in, and worth a focused review
rather than a rewrite. This spike's own `mkdirat`/`chdir` fixes above
are real host-filesystem passthrough, the opposite of this
default-deny direction. Expect them to become virtualized,
allowlist-gated calls in the real implementation, not to stay a
direct proxy to the real host filesystem.

### Fallback options, ranked below the chosen one

1. Fall back to vendoring `godot-cpp`'s (`godotengine/godot-cpp`, MIT)
   `Vector3`/`Basis`/`Quaternion`/`Transform3D` and relevant `Variant`
   scalar sources, hand-written and purely local (no engine
   round-trip), `REAL_T_IS_DOUBLE`-compatible, if the spike's
   threading or performance gate fails. `godot-sandbox`'s existing
   `add_sandbox_library` CMake helper confirms this is buildable.
   It also needs materially less integration work than vendoring raw
   Godot engine `core/math/` source directly. `godot-cpp`'s dependency
   graph already terminates at a handful of small, self-contained
   headers rather than the full `core/` monolith.
2. Run an actual Godot engine process alongside `zone-server-h2o`
   itself, as a separate host process, backing the full
   `godot-sandbox` syscall surface the way a real Godot client does.
   This directly contradicts the minimal-components driver above. It
   adds the entire engine, GDExtension host included, as a dependency
   to a project built specifically to avoid exactly that. Reject.
3. Hand-reimplement the full `godot-sandbox` API surface,
   `Node`/`Object`/`Callable`/ClassDB reflection included, from
   scratch, version-matched, with no live engine and no vendored
   source to check against. This is the largest and most drift-prone
   of the four options. Reject.

## Recommendation and next steps

1. Build the pinned `fabric-godot-core` `libgodot` target for
   `arch=rv64`, headless, `threads=no`, `modules/sandbox` disabled,
   matching the build configuration above.
2. Wire the embedding host program (`libgodot_create_godot_instance()`
   plus a manual `iteration()` loop) into the real CastSpell sandbox
   path, replacing the spike's minimal standalone harness.
3. Run part (2)'s performance measurement against the real
   10Hz/200-entity/many-zones budget before committing further
   implementation work.
4. Apply the four `libriscv` fixes from the spike upstream, per
   `fwsgonzo`'s stated openness to a real PR.
5. Design the default-deny filesystem and networking gating the
   maintainer feedback above calls for, before any package outside
   this project's own trusted builds can load.

## Open questions and verification

Resolved: which `Vector3`/`Basis`/`Transform3D`/`Quaternion`
operations compute locally in `godot-sandbox`'s guest versus which route
through the `METHOD`/`VMETHOD` syscall proxy. Reading
`program/cpp/docker/api/vector.cpp` directly confirmed this with
certainty. `Vector3`'s named math methods are hand-written inline
RISC-V assembly issuing `ecall`. `Basis`/`Quaternion`/`Transform3D` have
zero local computation at all. This settles the question the prior draft
of `rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps/index.md` flagged as
suggestive, not conclusive.

Resolved, part (1) of the `libgodot`-in-sandbox spike. A real
host program called `libgodot_create_godot_instance()` directly and drove
`GodotInstance::iteration()` in a manual loop. It booted the pinned
`fabric-godot-core` tag for real inside `libriscv`'s actual sandbox
(`rvlinux`), not just `qemu-riscv64`. The run produced real script
output, five real `iteration()` calls, and a clean shutdown, confirmed
via `strace -f`.

Open, part (2): measure real boot time and per-`iteration()`
cost against the actual budget (10Hz, 200 entities/zone, many
zones/process). Confirm the one-`GodotInstance`-per-`libriscv`-
`Machine`-per-zone shape holds up at that cadence. No one measures
either yet. This spike validated correctness under an interpreted, non-JIT
`libriscv` configuration, not speed. Fall back to vendoring `godot-cpp`'s
math/`Variant` subset if this gate fails.
