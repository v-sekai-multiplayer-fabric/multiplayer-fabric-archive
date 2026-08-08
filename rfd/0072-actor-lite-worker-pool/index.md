---
title: "RFD 0072: Actor-lite worker pool architecture for zone-server-h2o"
rfd: "0072"
state: published
scope: zone-server-h2o worker dispatch
---

## Problem

zone-server-h2o needs to dispatch HTTP requests from the H2O network
thread to worker threads. A generalized actor framework adds
scheduler overhead and lock contention on the fast path. The design
targets top-10 TechEmpower R23 data update throughput, and that
target needs a dispatch path with neither cost.

## Decision

zone-server-h2o dispatches HTTP requests from the H2O network thread
to a pool of worker threads. It uses lock-free single-producer,
single-consumer ring buffers, not a generalized actor framework. Each
worker owns one ring and one libpq pipeline connection to FoundationDB,
and runs pop, execute, return in a tight loop. The return path wakes
the H2O event loop through `h2o_multithread_send`, with no shared
state. This design targets top-10 TechEmpower R23 data update throughput,
where h2o already ranks second at 1,226,814 requests per second. The
SPSC design adds no scheduler overhead and no lock contention on the
fast path.

## References

- Full architecture diagram, component breakdown, and verification
  table: `DETAILS.md`
- Original record: `decisions/20260806-actor-lite-worker-pool.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0005-actor-lite-architecture.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

- `rfd/0073-async-fdb-callback-chain`: how each worker drives FDB
  without blocking its ring.
- `rfd/0077-pert-critical-path-zonefabric`: task C in the build order.

## Detail

{{< include DETAILS.md >}}
