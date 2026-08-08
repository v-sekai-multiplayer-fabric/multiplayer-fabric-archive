## Context

`godot-riscv-spike` (RFD 0001 item 6) proved a real, full Godot engine
instance boots and ticks inside `libriscv`'s own sandbox (`rvlinux`).
This holds for the real sandbox, not just `qemu-riscv64`. Five real
`libriscv` fixes made this work. Host-level `strace -f` confirmed the
result. `syscalls.jsonld`'s own audit already named the target use
case. The `socket` syscall's own note reads: "Relevant to CastSpell
effects that talk to zone-server-h2o."

This session raised and closed two designs before this one.

1. `godot-sandbox`'s own `Sandbox` Node and `vmcall` API needs its
   own full native Godot host process, a separate engine instance
   from the one `godot-riscv-spike` already proved working.
   `taskweft/taskweft`'s own RFD 0004 independently rejects this
   option for the identical reason. Its own words: "bound to a live
   Godot process." This confirms the concern is real, not unique to
   this project.
2. **Linking the sandboxed guest directly into `zone-server-h2o`'s
   own binary.** `zone-server-h2o` is a stock-clang CMake build. The
   guest needs `fabric-godot-core`'s own SCons, musl, and `rv64`
   cross-compile pipeline. Combining them ties two unrelated build
   systems together for no real benefit.

## Design detail

- **`zone-server-h2o`** (unchanged build) spawns a new
  **`sandbox-orchestrator`** binary. This is new, and separate, and
  it links raw `libriscv`. It connects over a `socketpair()`,
  registered with `h2o`'s own event loop the same way
  `udp_fd`/`timer_fd` already are. It is not a blocking thread bolted
  on the side.
- The **guest ELF** builds from `godot-riscv-spike`'s own proven
  `godot_instance_harness.cpp`. It exposes two functions callable by
  name via `vmcall()`. `godot_boot(cbor_config)` runs once:
  `initialize()`/`start()`, the exact sequence `FINDINGS.md` already
  verified. `godot_tick(input_addr, output_addr)` runs once per real
  tick, at 64 Hz (`ZONE_TICK_HZ`).
- The guest runs **offline**. No socket syscalls run inside the
  sandbox. `libriscv` already has them implemented for a different,
  later need, confirmed real, not stubs, per `syscalls.jsonld`. This
  integration does not use them. All real networking stays in
  `zone-server-h2o`'s own process.
- The data format is CBOR, with JSON-LD framing for anything
  self-describing. This covers `godot_boot`'s one-time config and any
  other control message. One real hot path stays bitpacked instead:
  the per-tick entity buffer that crosses the boundary on every
  `GodotInstance::iteration()` call. It reuses
  `xr_grid_entity_packet_t`'s existing 100-byte wire format verbatim.
  It addresses entities by RFD 0002's own slotmap index, not a flat
  scan. This shares one addressing scheme with RFD 0002's own,
  still-deferred, `zone_state` batch, instead of inventing a second
  one.

## Related prior art

`taskweft/taskweft` RFD 0004 covers "KHR_interactivity Tier 2," which
embeds `libriscv` and compiles behavior graphs to riscv64. It
independently reached the same conclusion: prefer `libriscv` over
`godot-sandbox`. It set two precedents this decision reuses directly.
First, values cross the ABI as JSON (`TwValue` (de)serialization),
the same reasoning behind the CBOR choice here. Second, `libriscv`'s
own machine-snapshot/resume capability is a real alternative to
repeated `vmcall()`s for state persistence between ticks. This is
worth a direct comparison during implementation.

## Consequences

Good: this reuses `godot-riscv-spike`'s own proven,
`strace`-confirmed boot sequence and five real fixes as they stand.
`zone-server-h2o`'s own build, transport, and FDB layers stay
untouched. One process boundary needs reasoning about, not a merged
build.

Bad: this is a genuinely new subsystem. Guest-side exported
functions, a new host orchestrator binary, and `h2o` evloop wiring
are all real, unstarted implementation work, not just documentation.
The exact `GodotInstance`/scene API `godot_tick` uses to apply entity
input and read state back out is not yet nailed down.

## Related

- `v-sekai-multiplayer-fabric/godot-riscv-spike`, `FINDINGS.md` and
  `syscalls.jsonld`
- `v-sekai-multiplayer-fabric/zone-server-h2o`
- `taskweft/taskweft`, RFD 0003 and RFD 0004
- This repo's own PR #126, the zonefabric decision docs ported here
  from `zone-server-h2o`'s own consolidation
- RFD 0002's `zone_state` slotmap design (this repo), referenced by
  `zone-server-h2o`'s own `src/zf_kv.c` comments
