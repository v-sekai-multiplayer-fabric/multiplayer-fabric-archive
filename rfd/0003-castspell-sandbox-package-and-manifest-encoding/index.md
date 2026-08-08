---
title: "RFD 0003: CastSpell sandbox package format and manifest encoding"
rfd: "0003"
state: discussion
scope: zone-server-h2o CastSpell
---

## Problem

CastSpell's effect step needed a package format and a manifest
encoding, and no RFD fixed either one. Without a fixed format, each
effect step package could use a different, incompatible layout. The
design also needed a runtime FFI format between the host and a loaded
package, instead of adding a third, unrelated binary format.

## Decision

CastSpell's effect step ships as a single `.elf` file: an embedded,
static manifest inside the ELF, not a side-car file. The manifest
encodes as CBOR-LD, decoding to plain JSON-LD. The runtime FFI between
the host and a loaded package reuses the zone tick's own bitpacked
struct format, gated by an explicit ABI version field, not a third
binary format. `DETAILS.md` has the full reasoning, the library picks
(`jsonld-cpp` plus `QCBOR` for an offline authoring tool; `QCBOR` alone
inside the host, in pure C under Fil-C), and the rejected alternatives.

## References

- Full design, library picks, and rejected alternatives: `DETAILS.md`
- `decisions/20260611-generated-behavior-sandboxed-riscv.md`
- `decisions/20260612-fabric-channels-as-reliability-classes.md`

## Related

`rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps/index.md` (item 4),
`rfd/0004-castspell-libgodot-sandbox-runtime-scope/index.md`

## Detail

{{< include DETAILS.md >}}
