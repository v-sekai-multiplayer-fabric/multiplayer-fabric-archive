## Rationale

mas-bandwidth/fps assumes 10x bandwidth reduction via delta compression
against a baseline (RFD 0002). zstd provides general-purpose compression
that complements delta compression:

- Entity batch writes: ZoneTick writes 200 entity_t structs in one
  FDB transaction. Packing 200 × 40-byte structs into a single 8KB blob
  and compressing with zstd level 3 typically achieves 2-3x reduction on
  positional data (floats have redundant exponent/mantissa bits when
  entities are spatially clustered).

- Zone snapshots: ghost relevance queries return entity states for
  replication. Compressing the snapshot reduces both FDB read amplification
  and network bandwidth on the return path.

- Asset content: assetcdn stores 5MB asset blobs. zstd level 19
  achieves 3-5x on typical 3D model data (GLB, USD).

## When to compress

| Value type                | Size         | Compress? | Level | Expected ratio            |
| ------------------------- | ------------ | --------- | ----- | ------------------------- |
| entity_t (single)         | ~40 bytes    | No        | —     | 1.0x (overhead > savings) |
| entity batch (200)        | ~8KB         | Yes       | 3     | 2-3x                      |
| zone snapshot             | ~8-16KB      | Yes       | 3     | 2-3x                      |
| asset blob                | ~5MB         | Yes       | 19    | 3-5x                      |
| TPC-C row (packed struct) | 50-500 bytes | No        | —     | 1.0x                      |
| World table row           | ~10 bytes    | No        | —     | 1.0x                      |

Threshold: compress values ≥ 512 bytes. Below that, the 4-byte zstd
frame header + compression CPU cost exceeds the savings.

## When NOT to compress

1. Point reads with small values: FDB point reads on 40-byte
   entity_t structs, where decompression adds latency for zero benefit.

2. TPC-C transaction values: packed C structs (RFD 0010) are
   50-500 bytes with high entropy (IDs, counters, decimal fields).
   zstd cannot compress high-entropy data meaningfully.

3. Hot path latency-sensitive reads: zstd decompression at level 3
   adds ~1-2µs for 8KB. For the ZoneTick range scan (200 entities),
   this is negligible vs FDB's 0.1-1ms read latency. For per-entity
   point reads in a 100Hz tick, it would add 200 × 2µs = 400µs per
   tick, so avoid it there.

## API design

```c
// Compress a value blob before FDB write
// Returns compressed size, or original size if below threshold
size_t zf_compress(const void *src, size_t src_size,
                   void *dst, size_t dst_capacity, int level);

// Decompress a value blob after FDB read
// Returns decompressed size
size_t zf_decompress(const void *src, size_t src_size,
                     void *dst, size_t dst_capacity);

// Check if a value is zstd-compressed (magic number 0x28 0xB5 0x2F 0xFD)
bool zf_is_compressed(const void *data, size_t size);
```

The magic number check lets the reader handle both compressed and
uncompressed values transparently — old uncompressed values still read
correctly after compression is deployed.

## FDB value framing

```
┌──────────────────────────────────────┐
│ uint8_t flags                        │  bit 0: is_compressed
│                                      │  bits 1-7: reserved
├──────────────────────────────────────┤
│ if compressed: zstd frame (variable) │
│ if not: raw value bytes              │
└──────────────────────────────────────┘
```

A 1-byte flags header precedes every value. bit 0 indicates whether
the remaining bytes are a zstd frame or raw data. This is backward-
compatible: the flags byte is 0x00 for uncompressed, which existing
readers interpret as the first byte of the struct (safe because all
packed structs start with a non-zero field like an ID).

Wait — backward compatibility is not safe if struct's first byte can
be 0x00. Alternative: use the zstd magic number (0x28B52FFD) as the
discriminator instead of a flags byte. Readers check the first 4
bytes:

```c
bool zf_is_compressed(const void *data, size_t size) {
    if (size < 4) return false;
    uint32_t magic;
    memcpy(&magic, data, 4);
    return magic == 0xFD2FB528; // zstd magic, little-endian
}
```

This requires no framing byte and is transparent to existing code.
Values that happen to start with the zstd(0x28B52FFD) magic are
vanishingly unlikely for packed structs whose first field is an ID.

## Compression levels

| Level | Use case                   | Speed    | Ratio |
| ----- | -------------------------- | -------- | ----- |
| 1     | Hot path (ZoneTick batch)  | ~1GB/s   | 2x    |
| 3     | Default (zone snapshot)    | ~800MB/s | 2-3x  |
| 9     | Warm path (asset upload)   | ~200MB/s | 3-4x  |
| 19    | Cold path (asset CDN seed) | ~20MB/s  | 3-5x  |

zstd level 3 at ~800MB/s is not a bottleneck for 8KB values: compression
takes ~10µs, negligible vs FDB's 1.5-2.5ms commit latency.

## Dependency

zstd is a single C library (`libzstd`), available as a system package
(`libzstd-dev` on Ubuntu) or vendored as a single `zstd.h` + `zstd.c`
amalgamation. No external runtime dependency, no JVM, no Python.

CMake:

```cmake
find_package(zstd REQUIRED)
target_link_libraries(h2o-bench-tpcc PRIVATE zstd::zstd)
```

Dockerfile:

```dockerfile
apt-get install -y libzstd-dev
```

## Benchmark impact

For zonefabric ZoneTick:

- Without compression: 200 × 40 bytes = 8KB per zone write
- With zstd level 3: ~3-4KB per zone write (2-3x reduction)
- FDB commit latency unchanged (dominated by fsync, not value size)
- Network bandwidth on snapshot delivery: 2-3x more zones per Gbit/sec

For assetcdn:

- Without compression: 5MB per asset read
- With zstd level 19: ~1-1.5MB per asset read (3-5x reduction)
- FDB read latency: reduced proportional to value size (less data to
  transfer from storage server to client)

## Relationship to mas-bandwidth/fps

Glenn Fiedler assumes delta compression (10x) for snapshot delivery.
zstd is orthogonal — it compresses the current state, not the delta.
The two compose:

```
snapshot = zstd_compress(delta_encode(current, baseline))
```

zstd on deltas achieves even higher ratios because deltas are sparse
(most fields unchanged → zero bytes compress extremely well). This RFD
covers zstd only; delta encoding is a future RFD.

## Relationship to RFD 0010 (binary value encoding)

RFD 0010 mandates packed C structs with `#pragma pack(push, 1)`. zstd
operates on the serialized byte stream, not the struct. The pipeline:

1. Pack struct to byte array (RFD 0010)
2. zstd compress byte array (this RFD)
3. FDB transaction set key → compressed bytes
4. FDB future get key → compressed bytes
5. zstd decompress → byte array
6. Cast byte array to struct (RFD 0010 zero-copy)

Steps 2 and 5 are conditional on value size ≥ 512 bytes.
