---
title: "RFD 0083: Replace FabricZone (Godot) with zone-server-h2o (libh2o + FDB + Fil-C)"
rfd: "0083"
state: published
scope: zone server implementation
---

## Problem

The Godot `FabricZone` engine ran the zone server side.
`FabricZone`/`FabricZoneJournal` stored its journal in local SQLite,
so state did not share across zone-server processes. The project
also needed memory safety against untrusted client input, which the
existing engine did not give.

## Decision

Build `zone-server-h2o`, a native `libh2o` and FoundationDB zone
server. Build it with Fil-C for memory safety against untrusted client
input. It fully replaces the Godot `FabricZone` engine on the server
side. The client stays Godot, unchanged. Scope carries over from
`FabricZone`/`FabricZoneJournal`, not a blank-slate design: entity
slots, zone-to-zone migration, ghost/AOI state, and a durable journal.
The team reimplements these against FDB instead of local SQLite, so
they share state across zone-server processes.

Physics and IK port from `sinew-mocap/solve`. Entity and ReBAC types
generate from `lean-entity-packet` and `lean-rebac-core`, instead of
hand-duplicating them across the retired engine, the new code, and any
client-facing schema. The first working milestone is a WebTransport
datagram round-trip plus a bare `ZoneTick`. The team builds everything
else after that milestone works. `weftspun/h2o-bench-tpcc` gets
archived once its reusable infrastructure and zonefabric design get
ported. Its accepted TPC-C benchmark work stays unaffected, read-only,
in place.

## References

- Full context, consequences, and the list of ported RFDs: `DETAILS.md`
- Original record:
  `decisions/20260806-zone-server-h2o-replaces-godot-fabriczone.md`
- `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

`rfd/0082-zonefabric-scaling`, `rfd/0072-actor-lite-worker-pool`, and
the other `rfd/0073`-`0081`, `0084` records carried forward from
`weftspun/h2o-bench-tpcc`.

## Detail

{{< include DETAILS.md >}}
