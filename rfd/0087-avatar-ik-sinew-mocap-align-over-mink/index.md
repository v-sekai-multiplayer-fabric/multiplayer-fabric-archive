---
title: "RFD 0087: Avatar IK uses sinew-mocap/solve's Align.lean, not mink"
rfd: "0087"
state: published
scope: zone-server-h2o, avatar posing and entity physics
---

## Problem

`sinew-mocap/solve` was flagged as less trusted than `kevinzakka/
mink`, a widely-used differential IK library. Should `zone-server-h2o`
port `mink` for avatar IK? Separately, entity/prop contact physics
used vendored MuJoCo — does that stay?

## Decision

No port of `mink`. Reading `mink`'s own core shows it assembles a
full quadratic program each solve, dispatched through a separate
QP-solver library plus its own Lie group math — a large, multi-session
port, not a small one.

`sinew-mocap/solve`'s own Lean source (`core/spec/Sinew/Align.lean`)
is not a QP solver at all. It is Kabsch-style rotation fitting. This
ports directly (`src/gen/sinew_align.{c,h}`), verified against
`Align.lean`'s own known-rotation-recovery test, to floating-point
precision. `mink`'s remaining features stay deferred.

Vendored MuJoCo (entity/prop contact physics) is dropped. Godot's own
Jolt physics already covers that role.

## References

- Full context, the mink QP breakdown, and revision history:
  `DETAILS.md`
- `v-sekai-multiplayer-fabric/sinew-mocap/solve`
- `v-sekai-multiplayer-fabric/mujoco-riscv64`: dropped MuJoCo's home

## Related

- `rfd/0083-zone-server-h2o-replaces-godot-fabriczone`
