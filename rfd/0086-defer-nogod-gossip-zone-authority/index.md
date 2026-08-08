---
title: "RFD 0086: Defer NoGod.lean's gossip zone authority"
rfd: "0086"
state: published
scope: zone-server-h2o, multi-process zone authority
---

## Problem

`lean-rebac-core`'s `NoGod.lean` is a proven, coordinator-free gossip
protocol for zone authority: vector clocks, Hilbert-range ownership,
and theorems that no two zones ever claim the same range. Should
`zone-server-h2o` port it now, alongside the rest of `ReBAC.lean`?

## Decision

Not yet. `zone-server-h2o` today hardcodes one zone ID per process
(`z_id = 0` in `src/transport/webtransport_server.c`). One process
has no peer to gossip with and no second zone's clock to order events
against. Porting the full gossip/vector-clock system now would build
structure for a need that does not exist yet.

The narrow, reusable part — `ZoneRange` and `geometricAuthority`,
about ten lines of pure C — ports cleanly whenever a second concurrent
zone is real work. The gossip layer (`VClock`, `HLC`, `GossipMsg`)
only earns its cost once there is a second process to coordinate
with.

## References

- Full context and the exact revisit trigger: `DETAILS.md`
- `v-sekai-multiplayer-fabric/zone-server-h2o`
- `sinew-mocap/solve` org's `lean-rebac-core`, `Rebac/core/NoGod.lean`

## Related

- `rfd/0083-zone-server-h2o-replaces-godot-fabriczone`: the
  consolidation decision this defers within.
