---
title: "RFD 0079: Sandboxed Godot game logic in zone-server-h2o, via raw libriscv"
rfd: "0079"
state: published
scope: zone-server-h2o CastSpell sandboxing
---

## Problem

CastSpell needs to run sandboxed Godot game logic inside
`zone-server-h2o`. `godot-sandbox`'s `Sandbox` Node API needs its own
separate live Godot process. Linking the guest directly into
`zone-server-h2o`'s binary would tie two unrelated build systems
together for no benefit.

## Decision

Use `godot-riscv-spike`'s proven whole-engine-in-`rvlinux` approach
directly. Drive it with raw `libriscv` (`copy_to_guest`,
`copy_from_guest`, `vmcall`). Run it as a separate
`sandbox-orchestrator` process, not linked into `zone-server-h2o`'s
binary. `zone-server-h2o` spawns this new binary and connects to it
over a `socketpair()`, registered with h2o's event loop the same way
`udp_fd`/`timer_fd` already are. The guest ELF exposes
`godot_boot(cbor_config)`, run once, and `godot_tick(input_addr,
output_addr)`, run once per real tick at 64 Hz.

The guest runs offline: no socket syscalls execute inside the
sandbox, even though `libriscv` implements them for a later need.
Control messages use CBOR with JSON-LD framing. The per-tick entity
buffer stays bitpacked, reusing the existing 100-byte
`xr_grid_entity_packet_t` format, and it addresses entities by the
zonefabric slotmap index. The team chose this over `godot-sandbox`'s
`Sandbox` Node API, which needs its own separate live Godot process.
The team also rejected linking the guest directly into
`zone-server-h2o`'s binary, which would tie two unrelated build
systems together for no benefit.

## References

- Full context, rejected designs, prior art, and consequences: `DETAILS.md`
- Original record:
  `decisions/20260806-sandboxed-godot-in-zone-server-h2o-via-raw-libriscv.md`
- `v-sekai-multiplayer-fabric/godot-riscv-spike`, `FINDINGS.md`

## Related

`taskweft/taskweft` RFD 0003/0004, and `rfd/0080-slotmap-entity-storage`
(shared addressing scheme).

## Detail

{{< include DETAILS.md >}}
