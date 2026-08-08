## Status

Discussion only. No implementation exists yet. Nothing today calls
`check_expr`, `rebac_can_json`, or any `lean-rebac-core` predicate from
a `libriscv` host callback.

## What `godot-sandbox` documents today

`godot-sandbox` is the reference `libriscv`-Godot integration.
`libriscv` itself calls "pause and resume ... a first-class citizen"
in its own README, and documents an "optional execution timeout using
instruction counting". `godot-sandbox` exposes a `restrictions`
property. Setting it `true` blocks
all external access by default, then five independent callbacks each
grant a narrow exception:

- `set_class_allowed_callback(func(sandbox, name): ...)` — gates class
  instantiation. Checked with `is_allowed_class(name)`.
- `add_allowed_object(obj)` / `remove_allowed_object(obj)` /
  `clear_allowed_objects()`, plus `set_object_allowed_callback(func(sandbox,
obj): ...)` for anything not in the explicit list.
- `set_method_allowed_callback(func(sandbox, obj, method): ...)` —
  gates method calls.
- `set_property_allowed_callback(func(sandbox, obj, prop): ...)` —
  gates property reads/writes.
- `set_resource_allowed_callback(func(sandbox, path): ...)`, checked
  with `is_allowed_resource(path)` — gates resource loads.

Each callback returns a plain boolean, independently of the other
four. None of them shares a subject/object model, a delegation
concept, or an audit trail. One documented workaround for building a
real whitelist is to run once in trace mode. This logs every access
request a callback sees, then a person hand-promotes the log into a
static allow-list. This RFD's ReBAC model closes that gap
structurally, not by manual log review.

## The existing ReBAC engine this reuses

`thirdparty/taskweft-nif/standalone/tw_rebac.hpp` (in
`zone-server-h2o`, ported from `plan_memory/rebac.py`, no Godot
dependency) already implements:

- A relation vocabulary: `HAS_CAPABILITY`, `CONTROLS`, `OWNS`,
  `IS_MEMBER_OF`, `DELEGATED_TO`, `SUPERVISOR_OF`, `PARTNER_OF`, plus
  an `UNKNOWN`/string-named escape hatch for domain-specific relations.
- A boolean expression algebra over relations: `base`, `union`,
  `intersection`, `difference`, `tuple_to_userset` (Zanzibar's own
  "pivot through another relation" pattern) — `check_expr()`.
- `IS_MEMBER_OF` transitive inheritance and `CONTROLS`/`DELEGATED_TO`
  inversion, both handled inside `check_base()`, not bolted on per
  caller.
- `can()`/`rebac_can_json()` — a DFS capability search that returns
  the actual path, not just a boolean, format:
  `{"authorized":bool,"path":[...]}`. This is the audit trail
  `godot-sandbox`'s five callbacks do not have today.
- JSON (de)serialization (`graph_from_json`/`graph_to_json`), so a
  guest's capability graph is itself data, not code.

## The two engines differ, and neither one fits as it stands

An earlier draft of this RFD called `lean-rebac-core`'s `rebacCheck`
an interchangeable alternative to `tw_rebac.hpp`. That was wrong. The
two carry different authorization semantics, and the difference
decides this RFD's whole premise.

Read `zone-server-h2o`'s own port, `src/gen/rebac.h`. `rebac_check`
grants access if and only if the claim's highest relation rank is
greater than or equal to the action's minimum rank. The ranks form a
fixed total order: `REBAC_RELATION_PUBLIC = 0`,
`INSTANCE_MEMBER = 1`, `FRIEND = 2`, `GUILD_MEMBER = 3`,
`OWNER = 4`. The actions are `REBAC_ACTION_OBSERVE`, `INTERACT`, and
`MODIFY`.

That is a five-tier lattice, not a relationship graph. It has no
subjects, no objects, and no edges. This RFD exists to replace a
two-tier model. A five-tier model is the same shape with three more
tiers, so it does not answer the problem statement.

`lean-rebac-core` keeps its own value, and `rfd/0083` and `rfd/0086`
both still depend on it. Its theorems are real (`rebac_empty_denied`,
`rebac_public_observe`, the owner-only `.modify` boundary). It answers
a different question: what rank a player claim needs for a world
action. It does not answer what a sandboxed guest may reach on the
host.

The other candidate has the opposite problem. `tw_rebac.hpp` carries
the right shape and no proofs.

## Migrate `lean-rebac-core` to the graph shape, do not replace it

Do not ship the unproved engine. Extend `lean-rebac-core` in Lean
from a rank lattice to a relationship graph, then generate the C from
it. `rfd/0083` already sets this rule for this exact module: ReBAC
types generate from `lean-rebac-core`, and they never get
hand-duplicated per language. `zone-server-h2o`'s `src/gen/rebac.c`
is already that generated artifact. This work extends the Lean source
behind it, instead of adding a second, unproved authorization engine
beside it.

`tw_rebac.hpp` keeps two roles in this plan. It is the reference
shape, because its relation vocabulary and expression algebra already
describe the target. It is also the differential-test oracle, in the
same way `test/unit/test_xr_grid_entity_packet.c` checks a generated
codec against golden vectors.

Precedent exists inside `tw_rebac.hpp` itself. Its own comment on the
`member_edges` index reads:

```
Formally justified by Planner.ExpandIndex: expand_index_equiv proves
that iterating member_edges gives the same result as scanning all
edges.
```

So `taskweft` already proves one fast path equal to its slow path, in
Lean, for this same header. This migration generalizes that habit
rather than inventing it.

### Theorems this migration must carry

- `rebac_empty_denied`, generalized: an empty graph denies every
  request. The rank version already proves this.
- Fuel soundness. If `check_expr` grants at fuel `n`, it grants at
  fuel `n + 1`. More search never reverses a grant, and a fuel-zero
  denial is therefore always conservative. This closes open question 4.
- Snapshot soundness. A resolved capability table, built at read
  version `V`, answers exactly as a graph walk at read version `V`
  answers. This replaces an earlier append-only monotonicity theorem,
  which `difference` makes false. See `rfd/0093`.
- Resolution equivalence. The flat capability table, built at bind
  time, answers exactly as a full graph walk answers. This is the same
  theorem shape as `expand_index_equiv`, and it is what makes the
  enforcement path trustworthy.
- Plane separation. No use-plane derivation grants `CAN_GRANT`. A
  guest therefore cannot reach the admin plane. This closes open
  question 2.
- Non-delegability, if the team takes that fork. No derivation grants
  `CAN_GRANT` to a subject that does not already hold a host-seeded
  `CAN_GRANT`.

## Administrative relations: who may write edges

A capability graph that its own subjects may edit grants nothing.
`godot-luau-script` answers this with a coarse split, where core
scripts may set their own permissions and user scripts may not. This
RFD answers it inside the graph instead, with administrative
relations.

Every check belongs to one of two planes:

| Plane | Question                                 | Relations                            |
| ----- | ---------------------------------------- | ------------------------------------ |
| Use   | May this guest call `X`?                 | `HAS_CAPABILITY`, `CONTROLS`, `OWNS` |
| Admin | May this principal add edge `(S, O, R)`? | `CAN_GRANT`                          |

A guest lives only on the use plane. Each grant request runs a
`check_expr` on the admin plane first, and the orchestrator runs that
check. `tw_rebac.hpp` supports `CAN_GRANT` today with no enum change,
because `parse_rel` returns `UNKNOWN` for an unrecognized string and
`check_base` then matches on `rel_name` directly. Reuse of the
existing `SUPERVISOR_OF`, `OWNS`, or `DELEGATED_TO` relations needs a
deliberate decision, because `check_base` already gives
`DELEGATED_TO` a delegation meaning, not an administration meaning.

Three sub-decisions stay open:

- Bootstrap. If edge-writing authority is itself an edge, some
  agent must write the first edge. That seed grant belongs outside the
  graph, host-side, before any guest runs. `godot-luau-script` seeds
  at `SandboxService` in `init.lua`. The equivalent here is the
  orchestrator at `mud_boot` time.
- Delegability. May a `CAN_GRANT` holder grant `CAN_GRANT` itself?
  A yes makes the relation viral, and one compromised administrator
  then owns the graph. Open question 3 below records that no
  revocation exists, so that capture stays permanent. Recommendation:
  make `CAN_GRANT` non-delegable in the first version.
- Scope. A global admin right and a scoped one differ completely.
  A scoped admin edge must name what it governs, such as one relation
  type or an object prefix. `tuple_to_userset` already expresses
  scoped administration, because it pivots through one relation to
  evaluate another.

## Proposed mapping

| `godot-sandbox` axis         | ReBAC subject       | ReBAC object          | Relation                               |
| ---------------------------- | ------------------- | --------------------- | -------------------------------------- |
| Class instantiation          | the guest `Machine` | the class name        | `HAS_CAPABILITY`                       |
| Object access                | the guest `Machine` | the object instance   | `HAS_CAPABILITY` or `CONTROLS`         |
| Method call                  | the guest `Machine` | `<object>.<method>`   | `HAS_CAPABILITY`                       |
| Property access              | the guest `Machine` | `<object>.<property>` | `HAS_CAPABILITY`                       |
| Resource load                | the guest `Machine` | the resource path     | `HAS_CAPABILITY`                       |
| Instruction-budget extension | the guest `Machine` | the "more gas" action | `DELEGATED_TO` from a supervisor/owner |

Delegation, membership, and capability inheritance fall out of the
expression algebra already in `tw_rebac.hpp`. A guest that is
`IS_MEMBER_OF` a trusted group inherits that group's `HAS_CAPABILITY`
edges automatically, with no new code path per axis. `godot-sandbox`
needs a new hand-written callback for each such case today.

## Consequences

Good: one authorization model instead of five independent callbacks
plus a two-tier VM split. A denied check returns a real reason (no
path found), not just `false`. The engine is not new, untested code.
`tw_rebac.hpp` already has real callers in `taskweft-nif`.

Bad: the team has not implemented this yet. `zone-guest-middleham`'s
`mud-sandbox-orchestrator` calls exactly two `vmcall`-reachable guest
functions today (`mud_boot`, `mud_step`). It has no host-capability
surface yet for this model to gate. The design has nothing to attach
to until that surface grows. No one wired
`thirdparty/taskweft-nif` into `zone-server-h2o` yet —
`mud/domains/README.md` already says the MUD guest does not use any
of its three domains yet. So reusing `tw_rebac.hpp` here means
depending on code that is itself not yet load-bearing anywhere.

Bad, and stated plainly: the Lean migration above is real work, and
nobody started it. Six theorems need proving, and two of them
(resolution equivalence, plane separation) carry the security
argument. Until they exist, this design has a proved rank model and an
unproved graph model, and neither one gates a guest today.

## The graph authors permissions. It does not enforce them.

A guest runs at near-native speed. `libriscv`'s own README records a
`vmcall` cost of 3ns, against 50 to 150ns for other sandboxes. The
same README records a CoreMark score of 38223 against 41382 native,
about 92%. Guest code therefore reaches the host boundary at
native-code frequency.

`_rebac_dfs` allocates. It copies a `std::unordered_set<std::string>`
at every branch (`sub_visited = p_visited`), and each element is a
heap-allocated string. Nobody measured that cost yet, so this RFD
states no figure. But an allocating graph search in front of a 3ns
call inverts the cost model. The check would dominate the call it
protects, and it would discard the one property `rfd/0079` chose
`libriscv` for.

So the ReBAC graph is the authoring model, not the enforcement model.
Resolve the graph once, at guest boot time and again at each grant,
into a flat capability table. The hot path then reads that table by
index. It never walks the graph.

Key that table by snapshot version, the way Zanzibar keys its cache.
An earlier draft of this RFD justified the table with an append-only
monotonicity argument, where a resolved grant stays valid forever.
Google's own system rejects that argument, and `rfd/0093` records why:
Zanzibar supports exclusion, so a later edge can revoke a grant.

Zanzibar's answer is a timestamp, not a monotonicity claim:

```
We avoid reusing results evaluated from a different snapshot by
encoding snapshot timestamps in cache keys.
```

`zone-server-h2o` already has the equivalent. FoundationDB is
multi-version, and `zf_zonetick.c` already opens one transaction per
tick, so every tick carries a read version. Key the resolved table by
that read version. Correctness then rests on multi-version reads, not
on a monotonicity property that `difference` breaks.

Zanzibar also quantizes its timestamps to "a coarse granularity, such
as one or ten seconds" so that recent checks share cache entries. A
per-tick read version already gives this design the same effect, with
no extra rounding.

`godot-sandbox` documents the same artifact from the other direction.
Its trace-mode workaround produces a static allow-list by hand. This
design produces that artifact from the graph instead, which removes
the manual review step.

This also reframes open question 6. `godot-sandbox` documents that
"Objects passed as function arguments remain accessible regardless of
restrictions." That reads as a bypass. It is instead an
object-capability design: the handle itself is the authorization,
checked once when the guest receives it. That choice is deliberate and
fast. It needs a decision here, not an accident.

## Open questions

Ten questions stay open. Items 1 to 3 block implementation. The rest
are design work.

1. Engine choice. Resolved above: extend `lean-rebac-core` to the
   graph shape and generate the C, with `tw_rebac.hpp` as the
   reference shape and test oracle. Recorded here because an earlier
   draft called the two engines interchangeable, which is wrong.
2. Who may write edges. Answered above by the use/admin plane
   split, but its three sub-decisions (bootstrap, delegability,
   scope) stay open.
3. Revocation does not exist. `tw_rebac.hpp` defines `add_edge`
   and `define`, and no counterpart that removes either. A search for
   `remove`, `revoke`, `erase`, and `delete` in that header returns
   nothing. The graph is append-only. Nobody can revoke a capability
   mid-session, and no rule covers a guest paused inside a `vmcall`
   when its access disappears.

   Zanzibar shows the shape of the fix. Leopard's incremental layer
   stores each update as a `(T, s, e, t, d)` tuple. The paper defines
   the last two fields:

   ```
   t is the timestamp of the update and d is a deletion marker
   ```

   A query merges every update at or below its own timestamp on top
   of the offline index. So revocation is a tombstone plus a version
   merge, and the Lean migration above must carry both. An
   append-only graph is not the design. It is the current gap.

4. Fuel has no owner. `check_expr` and `check_base` return `false`
   at zero fuel. That fails closed, which is correct. But a legitimate
   deep delegation chain then denies silently, and authorization
   depends on graph depth. Only `tw_expand` carries a default
   (`p_fuel = 3`). `check_expr`, `check_base`, and `rebac_can_json`
   each require the caller to supply one. No rule says who picks it.
5. Identity and ID reuse. Subjects and objects are `std::string`.
   Godot recycles object instance IDs after a free. A stale
   `HAS_CAPABILITY` edge plus a recycled ID grants access to a
   different object than the authorized one. This RFD names no stable
   identity scheme.
6. An inherited bypass. `godot-sandbox`'s own restrictions page
   states this: "Objects passed as function arguments remain
   accessible regardless of restrictions." A check at the call
   boundary misses handles the guest already holds. No rule covers
   re-checking a held handle on a later tick.
7. Cost on the 64 Hz path. `_rebac_dfs` copies the visited set at
   every branch (`sub_visited = p_visited`). A graph search per host
   call, on the zone tick, has no budget in this RFD.
8. Gas checking is circular. If budget extension is itself a ReBAC
   check, and that check costs time, no rule says whose budget pays.
9. Denial leaks the graph. `rebac_can_json` returns
   `{"authorized":bool,"path":[...]}`. That path is the audit-trail
   advantage over `godot-sandbox`. Returning it to untrusted guest
   code leaks capability-graph structure.
10. Which process may answer. `src/gen/rebac.h`'s own comment
    quotes the Lean source: only the authority zone evaluates
    `.interact` and `.modify`, and interest zones may evaluate
    `.observe` locally. `rfd/0086` defers that authority mechanism.

## Revisit when

`zone-guest-middleham`'s guest surface grows past `mud_boot`/`mud_step`
into calling named host functions/classes/objects (the shape
`rfd/0079` left open). At that point, build the five-axis mapping
table above as real code, instead of ad hoc per-axis callbacks. Do the
Lean migration first, and close open questions 2 and 3 with it. Prove
resolution equivalence before anything trusts the flat capability
table on the hot path.
