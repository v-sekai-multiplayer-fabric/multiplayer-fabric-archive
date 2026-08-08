---
title: "RFD 0069: Defer the loot-slice hardening scope until its need arrives"
rfd: "0069"
state: published
scope: loot-action vertical slice deferred hardening scope
---

## Problem

The loot-action vertical slice's decided scope goes beyond its stated
goal, a slice complete enough to exercise every integration seam. The
running `godot-loop-slice` already meets that integration goal. The
rest of the decided scope risks conflating the shipped deliverable
with pending work, and building structure ahead of a demonstrated
need.

## Decision

The loot-action vertical slice's decided scope goes beyond its stated
goal — a slice complete enough to exercise every integration seam.
The running `godot-loop-slice` already carries the loop end to end and
the playable-loop smoke passes, so the integration goal is met. The
rest of the decided scope — a separate presence reducer, a separate
progression reducer, the budgeter core, the CockroachDB adapter, the
measured 90 Hz performance gate, and a real OpenXR build — carries
forward as deferred-until-needed. Each deferred item pairs with the
concrete trigger that revives it; the team builds an item only when
its trigger fires, not before. This avoids conflating the shipped
deliverable with pending work, and avoids building structure ahead of
a demonstrated need.

## References

- Full deferred-item table, downsides, and rejected alternatives:
  `DETAILS.md`
- Original record:
  `decisions/20260629-defer-loot-slice-hardening-until-needed.md`

## Related

- `rfd/0045-loot-action-core-loop-mvp-vertical-slice`: the slice record
  whose unbuilt scope this record defers.
- `rfd/0039-hexagon-budgeter-core` through
  `rfd/0043-hexagon-progression-core`: the deferred cores.

## Detail

{{< include DETAILS.md >}}
