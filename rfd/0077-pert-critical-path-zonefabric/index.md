---
title: "RFD 0077: PERT critical path for zonefabric implementation"
rfd: "0077"
state: published
scope: zone-server-h2o build order
---

## Problem

The zonefabric implementation spans many RFDs with dependencies
between them. The team had no critical path or task order for the
remaining build work. Without that order, the team could not tell
which tasks carry slack and which tasks block the whole build.

## Decision

Model the zonefabric implementation as a PERT network. Each RFD's
implementation is a task with optimistic, most-likely, and pessimistic
duration estimates, and expected duration follows
`TE = (O + 4M + P) / 6`. The critical path is A (binary encoding) →
B (FDB keyspace) → C (actor-lite pool) → F (ZoneTick) → I (CastSpell)
→ M (feature ablation), totaling 26.0 engineering days. CastSpell sits
on the critical path because it depends on three upstream tasks and
carries the highest variance.

Zstd compression and Macaroon/XDP security both carry large slack
(11.1 and 7.5 days), and the team can build them later or in
parallel. The slotmap has only 2.1 days of slack, so the team must
build it early. A second engineer can absorb the slack tasks in
parallel without shortening the sequential critical path itself.

## References

- Full task list, dependency graph, slack analysis, milestones,
  two-engineer parallelization, and risk register: `DETAILS.md`
- Original record: `decisions/20260806-pert-critical-path-zonefabric.md`
- Source: `weftspun/h2o-bench-tpcc`, `rfd/0020-pert-critical-path.md`
- Consolidation target: `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

`rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps`: adopts this task
order (A, B, C, F, then I, then M) as the current build plan.

## Detail

{{< include DETAILS.md >}}
