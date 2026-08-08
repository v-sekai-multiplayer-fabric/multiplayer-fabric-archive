---
title: "RFD 0073: Async FDB callback chain on the libh2o event loop"
rfd: "0073"
state: published
scope: zone-server-h2o FDB transaction handling
---

## Problem

FDB's C API is callback-based by design. A worker that blocks on
`fdb_future_block_until_ready` cannot process the next request from
its SPSC ring. Each TPC-C transaction needed a way to chain FDB
futures without blocking the worker.

## Decision

Implement TPC-C transactions as async callback chains over FDB
futures. Each transaction step submits an FDB future and registers a
callback for when it resolves, and the callback runs the next step. On
any FDB error, the callback calls `fdb_transaction_on_error` and
restarts the chain from the first read. FDB's C API is callback-based
by design, and a worker that blocks on `fdb_future_block_until_ready`
cannot process the next request from its SPSC ring. Each transaction
allocates one heap context struct that flows through the whole chain,
and the final callback frees it. The chain needs no reference
counting: it is linear, and only one future stays outstanding at a
time.

## References

- Full callback pattern, error handling, and memory rules: `DETAILS.md`
- Original record: `decisions/20260806-async-fdb-callback-chain.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0011-async-fdb-callback-chain.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

`rfd/0072-actor-lite-worker-pool`: the worker thread this callback
chain runs inside.

## Detail

{{< include DETAILS.md >}}
