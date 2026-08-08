---
title: "RFD 0081: Three-layer verification strategy (CBMC, Lean 4, plausible-witness-dag)"
rfd: "0081"
state: published
scope: zone-server-h2o / h2o-bench-tpcc verification
---

## Problem

CBMC proves C implementation invariants for bounded inputs, but it
does not prove the specification is sound. Lean 4 proves the
specification is sound, but it does not verify the C code itself.
plausible-witness-dag searches for runtime invariant violations, but
it does not prove their absence.

## Decision

Verify h2o-bench-tpcc with three complementary tools. Each tool
targets a different layer of the stack. CBMC proves C implementation
invariants (SPSC ring FIFO, bounds, head-tail, NURand range) for
bounded inputs. Lean 4 proves the specification is sound (SPSC
linearizability, push/pop preserve bounds), but does not verify the C
code itself. plausible-witness-dag searches for runtime invariant
violations over HTTP (NewOrder atomicity, Delivery correctness, Stock
non-negative), but does not prove their absence.

No single tool covers all three gaps. CBMC catches implementation
bugs, Lean 4 catches specification bugs, and plausible-witness-dag
catches integration bugs. Together they cover the stack from code to
design to runtime behavior.

## References

- Full layers table, CBMC harnesses, Lean 4 modules, and the
  plausible-witness-dag escalation ladder: `DETAILS.md`
- Original record: `decisions/20260806-three-layer-verification-strategy.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0008-verification-strategy.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

- `rfd/0072-actor-lite-worker-pool`: the SPSC ring these CBMC and
  Lean 4 proofs cover.
- `rfd/0078-plausible-witness-dag-feature-ablation`: extends
  plausible-witness-dag's runtime role to pre-implementation design
  ablation.

## Detail

{{< include DETAILS.md >}}
