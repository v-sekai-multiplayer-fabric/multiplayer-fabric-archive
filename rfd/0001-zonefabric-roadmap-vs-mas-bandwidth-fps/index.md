---
title: "RFD 0001: Zonefabric roadmap: PERT order, current status, and fps/eBPF/sandbox notes"
rfd: "0001"
state: discussion
scope: zone-server-h2o
---

## Problem

The project had no fixed order for the remaining zonefabric work.
Two encoding choices, the CastSpell sandbox boundary format and its
package manifest format, had no settled answer. The `mas-bandwidth/fps`
reference design lacks delta compression, client prediction,
multicast, and a kernel-bypass ingest layer, and no RFD stated whether
this gap was acceptable. No RFD picked a license-safe library for
future kernel-bypass or eBPF work.

## Decision

Build the remaining `zone-server-h2o` zonefabric work in the PERT
critical-path order `decisions/20260806-pert-critical-path-zonefabric.md`
already set: task A, B, C, F, then I, then M. `DETAILS.md` carries the
full per-task status table and the numbered follow-up list.

Two encoding choices apply to CastSpell's sandbox boundary: the same
bitpacked struct format the zone tick already uses for its runtime FFI,
and CBOR-LD for its package manifest. `RFD 0003` and `RFD 0004` are the
full designs those two choices point to. `mas-bandwidth/fps` stays a
settled reference point, not an open requirement: this design lacks its
delta compression, client prediction, multicast, and kernel-bypass
ingest layer, and the project accepts that gap for now. Prefer
`iovisor/ubpf`, Apache-2.0, over `libbpf`/`libxdp`'s GPL-adjacent
license, for any later kernel-bypass or eBPF work.

## References

- Full status table, task order, and follow-ups: `DETAILS.md`
- PERT order: `decisions/20260806-pert-critical-path-zonefabric.md`
- `decisions/20260806-zone-server-h2o-replaces-godot-fabriczone.md`

## Related

`rfd/0003-castspell-sandbox-package-and-manifest-encoding/index.md`,
`rfd/0004-castspell-libgodot-sandbox-runtime-scope/index.md`,
`rfd/0005-gltf-interactivity-value-type-taxonomy-correction/index.md`

## Detail

{{< include DETAILS.md >}}
