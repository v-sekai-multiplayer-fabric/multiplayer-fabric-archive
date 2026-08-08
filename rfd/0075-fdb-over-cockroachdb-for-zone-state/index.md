---
title: "RFD 0075: FoundationDB selection over CockroachDB for zone-server-h2o state"
rfd: "0075"
state: published
scope: zone-server-h2o database backend
---

## Problem

TPC-C is 88 percent writes. CockroachDB's Raft consensus path gives
higher write latency than a log-structured MVCC store for a
write-heavy load. The project's CockroachDB fork also carries no
upstream activity.

## Decision

Use the raw FoundationDB C API (`libfdb_c`) as the database for
h2o-bench-tpcc, not CockroachDB. TPC-C is 88 percent writes, and FDB's
log-structured MVCC gives lower write latency than CockroachDB's Raft
consensus path for a write-heavy load. FDB 7.3.79 stays under active
Apple development, while the project's CockroachDB fork has no
upstream activity. `libfdb_c` is a native C shared library with a
callback-based API that integrates directly with h2o's event loop.
It needs no JVM, no JNI, no gRPC bridge, and no SQL parsing or
planning overhead. FDB transactions are native ACID objects, not a
SQL `BEGIN`/`COMMIT` wrapper.

## References

- Full API surface, tradeoffs, and revisit criteria: `DETAILS.md`
- Original record: `decisions/20260806-fdb-over-cockroachdb-for-zone-state.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0006-fdb-selection.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

`rfd/0073-async-fdb-callback-chain`: the callback pattern this API
surface drives.

## Detail

{{< include DETAILS.md >}}
