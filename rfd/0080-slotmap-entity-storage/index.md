---
title: "RFD 0080: Slotmap entity storage with generational IDs"
rfd: "0080"
state: published
scope: zone-server-h2o in-memory entity storage
---

## Problem

In-memory entity storage per zone used a hash map or a linked list
approach. A hash map of 200 entities touches about 8 random cache
lines per iteration. A linked list needs pointer-chasing, and both
approaches cost more than a sequential scan.

## Decision

Use a slotmap for in-memory entity storage per zone, replacing hash
map and linked list approaches. A slotmap is a sparse set with
generational indices. Each zone owns one thread-local slotmap. FDB
stays the durable backing store, and the slotmap is the hot in-memory
index for tick processing. Iterating 200 entities in a slotmap's dense
array touches about 8KB sequentially, roughly one L1 miss after
warmup. A hash map of the same size touches about 8 random cache
lines, and a linked list needs pointer-chasing.

Generational (index, version) handles prevent ABA problems, where a
recycled slot looks like a live entity. A stale handle's
version mismatch returns null instead of the wrong entity. The team
rejected a full ECS framework like EnTT as unjustified overhead for
one component type and one system. The slotmap is about 100 lines of
C, against EnTT's roughly 15,000 lines of C++, at comparable iteration
speed for this workload.

## References

- Full slotmap struct layout, operations, cache-behavior table, and
  FDB migration sync sequence: `DETAILS.md`
- Original record: `decisions/20260806-slotmap-entity-storage.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0017-slotmap-entity-storage.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

`rfd/0074-binary-value-encoding-for-fdb` (the format the slotmap
stores by `memcpy`), `rfd/0076-macaroon-xdp-security` (the
`entity_id` caveat maps to a slotmap handle), and
`rfd/0072-actor-lite-worker-pool` (each worker owns its zone's slotmap
thread-locally).

## Detail

{{< include DETAILS.md >}}
