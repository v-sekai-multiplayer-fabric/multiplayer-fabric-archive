---
title: "RFD 0074: Binary value encoding for FDB values"
rfd: "0074"
state: published
scope: zone-server-h2o FDB value format
---

## Problem

A warehouse row costs roughly 350 bytes in JSON. A protobuf or JSON
payload also needs a parsing step before code can use FDB's returned
value pointer. The project needed a value format that avoids both
costs.

## Decision

Encode TPC-C row values as packed C structs with network byte order
(big-endian) integers, using `#pragma pack(push, 1)` for a
deterministic layout. No protobuf, no JSON, and no FDB tuple layer for
values. A packed struct casts directly from FDB's returned value
pointer, so there is no parsing step. A warehouse row is 109 bytes
packed, versus roughly 350 bytes in JSON. The tuple layer stays
reserved for keys, where lexicographic ordering matters. Values do not
need that ordering.

## References

- Full rationale, tradeoffs, and rejected alternatives: `DETAILS.md`
- Original record: `decisions/20260806-binary-value-encoding-for-fdb.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0010-binary-value-encoding.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

- `rfd/0080-slotmap-entity-storage`: stores the same packed struct in
  memory, `memcpy` from FDB value to slotmap entry.
- `rfd/0084-zstd-compression-for-zone-state`: compresses the packed
  byte stream this RFD produces.

## Detail

{{< include DETAILS.md >}}
