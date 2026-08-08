## Context

`sinew-mocap/solve` (FK + LBS skinning) was flagged as less trusted
than [`kevinzakka/mink`](https://github.com/kevinzakka/mink), a
widely-used differential inverse-kinematics library built on MuJoCo
(which `zone-server-h2o` already vendored for entity/prop contact
physics, `thirdparty/mujoco` pinned to 3.11.0, at the time).

## What mink actually is

Read `mink`'s core (`src/mink/solve_ik.py`) directly rather than
assume from its README. It is not a simple Jacobian pseudo-inverse
solver. Each solve builds a full quadratic program:

- Objective `(H, c)` assembled from weighted per-task residuals plus
  Levenberg-Marquardt damping (`_compute_qp_objective`).
- Inequality constraints `(G, h)` from joint/velocity/collision-
  avoidance limits (`_compute_qp_inequalities`).
- Equality constraints `(A, b)` from closed-chain kinematics
  (`_compute_qp_equalities`).

That QP is solved via `qpsolvers`, a separate general-purpose QP
dispatch library backing onto solvers such as OSQP, quadprog, or
proxqp — not something `mink` implements itself. `mink` also ships
its own C-optimized Lie group (SE3/SO3) module and roughly a dozen
pluggable task/limit types (frame, COM, posture, look-at,
collision-avoidance, velocity/configuration limits).

A faithful port would need a vendored C QP solver (OSQP is the
natural pick), plus `mink`'s QP-assembly loop, Lie group math, and at
least the `FrameTask` case — a genuinely large, multi-session effort.

## Revision 1 (superseded)

Per direct instruction, `sinew-mocap/solve`'s avatar-posing role was
briefly rebuilt on a QP-based IK core mirroring `mink`'s
architecture, rather than staying on plain FK + LBS with `mink`
deferred wholesale.

## Revision 2: the actual decision

Revision 1 was wrong, caught by a direct correction: `sinew-mocap/
solve` follows this org's Lean-first convention (Lean spec → Slang
codegen, the same pattern as `lean-entity-packet` and
`lean-rebac-core`'s Lean → C ports), and its own Lean source
(`core/spec/Sinew/Align.lean`, read directly, not assumed) is **not**
a QP-based solver at all. It is Kabsch-style rotation fitting —
`rodrigues` for one vector pair, a Newton-Schulz-orthogonalized
covariance (`ns30`) for two or more, falling back to a Jacobi-SVD
Kabsch solve (`kabsch`) when the fast path produces an invalid
rotation. No QP, no `qpsolvers`, no MuJoCo dependency for this part
at all.

`Align.lean` ports directly (`src/gen/sinew_align.{c,h}`), verified
against the exact same known-rotation-recovery test
`core/spec/AlignTest.lean` itself uses (quaternion
(0.5,0.5,0.5,0.5), the same five source vectors, the same N=5/N=2/N=1
cases) — `test/unit/test_sinew_align.c` reproduces it and recovers
the rotation to floating-point precision in all three cases. This is
done, not deferred — smaller and more tractable than either Revision
1's QP framing or the original mink-wholesale framing, because it is
the algorithm this org already trusts and has proven, not a heavier
one substituted in from outside.

`mink` feature parity (limits, closed-chain constraints, the Lie
group module, multi-task weighting) stays deferred, and is unrelated
to `Align.lean`'s now-completed scope — it would only become relevant
if a genuinely different, `mink`-shaped IK need arises later.

## Revision 3: MuJoCo dropped

Vendored MuJoCo (entity/prop contact physics) is dropped, per direct
instruction. Godot's own Jolt physics already covers that role, so a
second vendored physics engine in this process was redundant.
Nothing in this record's own IK analysis changes: `sinew-mocap/
solve`/`Align.lean` never depended on MuJoCo, as Revision 2 already
established. The vendored MuJoCo build itself moved to
`v-sekai-multiplayer-fabric/mujoco-riscv64`. A real replacement plan
for entity/prop contact physics against Jolt is tracked separately,
not assumed solved by this record.

## Related

- `rfd/0083-zone-server-h2o-replaces-godot-fabriczone`
- Original record: `zone-server-h2o`'s own
  `docs/0002-defer-mink-port-keep-sinew-mocap-solve.md` (removed once
  this RFD carried its content forward).
