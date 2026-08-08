---
title: "RFD 0076: Macaroon + eBPF/XDP security fabric"
rfd: "0076"
state: published
scope: zone-server-h2o network security
---

## Problem

The packet path needs to check every packet at the NIC, inside a
sub-10-microsecond packet budget. XDP itself cannot validate a
chained-HMAC Macaroon, because the BPF verifier rejects the
instruction count and variable-length parsing HMAC needs. A full
Macaroon validation also costs 50 to 100 microseconds, far above that
budget.

## Decision

Use a two-tier security architecture. Macaroon-based authentication
runs in user space. eBPF/XDP session-key enforcement runs in kernel
space. User space validates a cryptographic Macaroon once, at
connection time. It then pushes a 64-bit session key into an eBPF
whitelist map keyed by player IP. An XDP program checks every
following packet at the NIC.

The XDP program does one hash-map lookup and one integer comparison.
This runs in roughly 10 to 50 nanoseconds per packet, with zero
cryptographic operations in that path. XDP itself cannot validate a chained-HMAC
Macaroon: the BPF verifier rejects the instruction count and
variable-length parsing HMAC needs. A full validation also costs 50 to
100 microseconds, far above the sub-10-microsecond packet budget. At
25M concurrent users, this design keeps XDP filtering under 1 percent
of one core and still absorbs a spoofed-packet flood at line rate.

## References

- Full architecture, packet-rate analysis, Macaroon caveat design,
  session lifecycle, and scope exclusions: `DETAILS.md`
- Original record: `decisions/20260806-macaroon-xdp-security.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0018-macaroon-xdp-security.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

- `rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps`: prefers
  `iovisor/ubpf` over `libbpf`/`libxdp` for later eBPF work.
- `rfd/0080-slotmap-entity-storage`: the `entity_id` caveat maps to a
  slotmap handle.
- `rfd/0072-actor-lite-worker-pool`: the orchestrator that validates
  Macaroons runs on this worker pool.

## Detail

{{< include DETAILS.md >}}
