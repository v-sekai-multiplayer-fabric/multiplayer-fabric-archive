---
title: "RFD 0084: zstd compression for batched zone-state FDB values"
rfd: "0084"
state: published
scope: zone-server-h2o FDB value compression
---

## Problem

Large value blobs, such as entity batches, zone snapshots, and asset
content, wrote to FDB uncompressed. Uncompressed entity batches and
zone snapshots take more FDB storage and bandwidth than needed.
Compressing small fixed-size structs the same way would pay
compression overhead that exceeds the savings.

## Decision

Compress FDB values with zstd before writing, and decompress after
reading. Apply this to large value blobs: entity batches, zone
snapshots, asset content. Skip it for small fixed-size structs, where
compression overhead exceeds the savings. The threshold is 512 bytes.
Entity batches (~8KB) and zone snapshots compress 2 to 3 times at
level 3. Asset blobs (~5MB) compress 3 to 5 times at level 19.

Single 40-byte `entity_t` structs and packed TPC-C rows stay
uncompressed. Their high-entropy fields do not compress meaningfully,
and a 40-byte struct adds decompression latency for no benefit. The
zstd magic number (0x28B52FFD), not a flags byte, discriminates
compressed values from raw ones. Old uncompressed values therefore
still read correctly once compression rolls out. zstd is orthogonal to
delta compression against a baseline, and the two compose, with zstd
applied after delta encoding.

## References

- Full API design, FDB value framing, compression-level table,
  dependency setup, and benchmark impact: `DETAILS.md`
- Original record: `decisions/20260806-zstd-compression-for-zone-state.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0016-zstd-compression.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

- `rfd/0074-binary-value-encoding-for-fdb`: the packed struct format
  zstd compresses.
- `rfd/0082-zonefabric-scaling`: the batched `zf/zone_state/` blob
  this compression targets.

## Detail

{{< include DETAILS.md >}}
