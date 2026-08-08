---
title: "RFD 0092: ReBAC gates libriscv guest access, not five callbacks"
rfd: "0092"
state: discussion
scope: zone-guest-middleham, godot-sandbox-style libriscv hosts
---

## Problem

`rfd/0079` picked raw `libriscv` for sandboxed guest code (now living in
`zone-guest-middleham`), but never defined what host capabilities a
guest may reach. `rfd/0079`'s own "Consequences" section says so
directly: "The exact `GodotInstance`/scene API `godot_tick` uses to
apply entity input and read state back out is not yet nailed down."

`godot-sandbox`, the reference `libriscv`-Godot integration, already
solved this once, with five independent allow-callbacks: class, object,
method, property, and resource. Each callback returns a plain boolean.
None of the five shares state with the others, and none returns a
reason or a path a reviewer could audit later.

## Decision

Gate guest access through ReBAC, not five separate callbacks, and not
a two-tier core/user split like `godot-luau-script`'s.

Neither existing engine fits as it stands. `lean-rebac-core`'s
`rebacCheck` carries real Lean proofs, but it is a five-rank total
order, which is a tier model. `tw_rebac.hpp` has the right
relationship-graph shape, but it carries no proofs.

So extend `lean-rebac-core` in Lean to the relationship-graph shape,
and generate the C from it. This keeps `rfd/0083`'s existing rule:
ReBAC types generate from `lean-rebac-core`, not hand-duplicated per
language. `tw_rebac.hpp` becomes the reference shape and the
differential-test oracle, not the shipped engine. `DETAILS.md` lists
the theorems this migration must carry.

Model each guest `libriscv::Machine` instance as a ReBAC subject.
Model each of `godot-sandbox`'s five gated things (a class, an object,
a method, a property, a resource path) as a ReBAC object. Replace each
boolean callback with one `check_expr`/`can()` call: does this guest
subject have `HAS_CAPABILITY` (or a defined computed relation) to this
object.

Split the graph into a use plane and an admin plane. On the use plane
a guest asks whether it may reach a capability. On the admin plane a
principal asks whether it may add an edge. A `CAN_GRANT` relation
governs the admin plane. The orchestrator runs every admin check, and
a guest never reaches that plane.

Also gate `libriscv`'s own execution-budget primitives (instruction-
counted timeout, pause/resume) as ReBAC actions, not hardcoded
constants. Budget extension becomes an authorized relationship.

## References

- Engine comparison, mapping table, the `godot-sandbox` API this
  replaces, and ten open questions: `DETAILS.md`
- `v-sekai-multiplayer-fabric/zone-server-h2o`,
  `thirdparty/taskweft-nif/standalone/tw_rebac.hpp`
- `sinew-mocap/solve` org's `lean-rebac-core`, `Rebac/core/ReBAC.lean`
- `libriscv/godot-sandbox`, `docs/host_langs/godot_integration/godot_docs/restrictions`

## Related

- `rfd/0079-sandboxed-godot-in-zone-server-h2o-via-raw-libriscv`: the
  sandboxing decision this gates.
- `rfd/0086-defer-nogod-gossip-zone-authority`: the other place
  `lean-rebac-core` is already in scope.

## Detail

{{< include DETAILS.md >}}
