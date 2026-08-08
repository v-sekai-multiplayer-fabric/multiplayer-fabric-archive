## Rationale

1. Zero-copy deserialization: a packed struct can be cast directly
   from FDB's `FDBKeyValue.value` pointer. No parsing step. The
   callback handler does `(stock_val_t *)kv->value` and reads fields.

2. Minimal size: no type tags, no field names, no separators. A
   warehouse row is 109 bytes packed vs ~350 bytes in JSON.

3. Deterministic layout: `#pragma pack(push, 1)` ensures no padding.
   The same struct compiles identically on the loader, the server, and
   the verification harness. Cross-platform safe as long as all parties
   use the same header file.

4. Endianness is fixed: network byte order for all integers. x86_64 and ARM
   both support unaligned access, so the byte-swap cost is negligible.

## Tradeoffs

- Schema evolution: adding a field breaks compatibility. Solution:
  version the struct (include a `uint8_t version` field) or use FDB
  key prefixes to distinguish row formats.
- Debugging: binary values are not human-readable in fdbcli. Solution:
  the loader and verification harness have debug print functions that
  decode the struct.
- Portability: requires matching `#pragma pack` behavior across
  compilers. GCC and Clang both support this correctly.

## What we did not use

- FDB tuple layer for values: the tuple layer is for keys (where
  lexicographic ordering matters). Values don't need ordering, so the
  tuple layer's overhead is unnecessary.
- Protocol Buffers / Cap'n Proto: adds a dependency and codegen step.
  For fixed-schema TPC-C, a C struct is simpler and faster.
- JSON: 3-5x larger, requires a parser, and yajl allocation per row
  would dominate the hot path.
