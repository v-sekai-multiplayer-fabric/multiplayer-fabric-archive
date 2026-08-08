---
title: "RFD 0078: plausible-witness-dag for zonefabric feature ablation"
rfd: "0078"
state: published
scope: zone-server-h2o feature scoping
---

## Problem

The team needed to know, before writing implementation code, which
zonefabric game features are load-bearing and which are safe to
defer. Building a feature the wrong way once, then rebuilding it
correctly, pays for that feature twice. The team also needed a
dependency-ordered build order, so no feature would build on an
unverified foundation.

## Decision

Use plausible-witness-dag to ablate zonefabric game features before
writing implementation code. Model each feature as a set of Lean 4
invariants over game state. Then remove one feature's invariants at a
time, and search for a witness trace that violates a remaining
invariant. A found witness marks the feature load-bearing: the team
must build it correctly the first time, or it becomes speculator debt
paid for twice. No witness at escalating scale (L0, L1, L2) marks the
feature deferrable, an unspent-dollar feature safe to stub or skip.
The ablation matrix also gives a dependency-ordered build order, so no
feature is built on an unverified foundation.

Slotmap generational IDs and ZoneTick are load-bearing and come
first. CastSpell, GhostRelevance zone splitting, and rate limiting
are independent of the core loop, so the team may defer or stub them.

## References

- Full feature-invariant table, ablation matrix, dependency graph,
  Lean predicates, and economics: `DETAILS.md`
- Original record: `decisions/20260806-plausible-witness-dag-feature-ablation.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0019-feature-ablation.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

- `rfd/0081-three-layer-verification-strategy`: plausible-witness-dag's
  other role, as the runtime verification layer.
- `rfd/0080-slotmap-entity-storage`: the first feature this ablation
  verifies as foundational.
- `rfd/0076-macaroon-xdp-security`: security features ablated
  independently of game logic.

## Detail

{{< include DETAILS.md >}}
