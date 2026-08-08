---
title: "RFD 0082: Zonefabric scaling: hub/instanced-zone game-server benchmark shape"
rfd: "0082"
state: published
scope: zone-server-h2o benchmark and keyspace design
---

## Problem

Glenn Fiedler's mas-bandwidth/fps architecture calls for a "world
database" and leaves it unbuilt. A per-entity keyspace also costs 200
times more FDB keys than a per-zone keyspace. Full-mesh O(N²)
broadcast also does not scale replication across many zones.

## Decision

Model weft-warp-loop's hub/instanced-zone game server as zonefabric: a
FoundationDB-backed benchmark scaled by zone count, with 200 entities
per zone at fixed constants (WORLD_EXTENT=10000.0, GHOST_RANGE=150.0,
AUTHORITY_CAPACITY=256, SPLIT_COST_THRESHOLD=40000.0). Persistence
uses a single zstd-compressed `zf/zone_state/{z_id}` blob per zone,
per 10Hz tick, holding the slotmap and effect state. This cuts FDB key
count 200-fold against the per-entity keyspace, which the design
retains only for migration, ghost-relevance range scans, and point
lookups. Zonefabric is the "world database" that Glenn Fiedler's
mas-bandwidth/fps architecture calls for and leaves unbuilt. It builds
this on FDB instead of Redis, with AOI-filtered replication
(GHOST_RANGE covers 0.02 percent of world space per zone) in place of
full-mesh O(N²) broadcast. Per-zone independent FDB transactions and
thread-local slotmaps give near-linear core scaling, with no
cross-zone conflicts.

## References

- Full FDB keyspace design, runtime operation costs, benchmark
  comparison matrix, and mas-bandwidth/fps concept mapping: `DETAILS.md`
- Original record: `decisions/20260806-zonefabric-scaling.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0002-zonefabric-scaling.md`
- Glenn Fiedler, "Creating a first person shooter that scales to
  millions of players":
  https://mas-bandwidth.com/creating-a-first-person-shooter-that-scales-to-millions-of-players/

## Related

`rfd/0080-slotmap-entity-storage`, `rfd/0084-zstd-compression-for-zone-state`
(the batched blob's two layers), `rfd/0076-macaroon-xdp-security`
(unmeasured transport layer), `rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps`.

## Detail

{{< include DETAILS.md >}}
