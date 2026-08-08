## Problem

Building an MMO is expensive. Every feature is a bet:

- **Feature built that wasn't needed** = wasted engineering dollars.
  The code exists, must be maintained, adds complexity, slows down
  the hot path, and provides zero player value.

- **Feature built wrong, then rebuilt** = speculator debt. You ship
  a feature based on assumption, discover the assumption was wrong in
  production, then pay to tear it out and rebuild it properly. You
  pay twice: once to build it wrong, once to build it right.

- **Feature not built that was needed** = opportunity cost, but
  recoverable. You can always add it later if the architecture
  accommodates it. The dangerous case is building a _replacement_
  that conflicts with the missing feature's invariants.

Traditional game development resolves this with playtesting and
prototyping, which are expensive, slow, and subjective.
plausible-witness-dag resolves it with formal invariant search, which
is fast, deterministic, and exhaustive within the modeled state space.

## Approach

### Step 1: Model features as invariant sets

Each zonefabric feature is modeled as a set of predicates over the
game state. A feature is "present" when its invariants are enforced;
"ablated" when they are removed.

| Feature         | Invariants when present                              | What breaks when ablated                               |
| --------------- | ---------------------------------------------------- | ------------------------------------------------------ |
| ZoneTick        | `∀ entity: pos' = pos + vel × dt`                    | Entities freeze; positions never update                |
| CastSpell       | `effect → fanout ⊆ entities_in_ghost_range`          | Effects hit entities outside range; teleporting damage |
| GhostRelevance  | `ghost ⊆ entities_within_ghost_range(zone_boundary)` | Players see entities they shouldn't; information leak  |
| ZoneSplit       | `population² > threshold ⟹ split`                    | Hot zones never split; performance cliff               |
| EntityMigration | `entity leaves zone A ⟹ entity enters zone B`        | Entities vanish or duplicate; state corruption         |
| SlotmapGenIDs   | `handle.gen == slot.gen ⟹ entity is live`            | Dangling references; fireball hits wrong player        |
| SessionExpiry   | `now > expiry ⟹ XDP drops packets`                   | Expired sessions still route; security hole            |
| RateLimit       | `packets_per_sec ≤ rate_limit`                       | No flood protection; DDoS amplification                |

### Step 2: Ablation matrix

For each feature F, run plausible-witness-dag with F's invariants
removed and all other features' invariants present. The witness
search either:

- **Finds a witness** (a state trace that violates a remaining
  invariant when F is absent) → F is load-bearing. Building it wrong
  or omitting it causes a cascade of invariant violations. This is
  speculator debt — build it correctly or pay twice.

- **Finds no witness** → F's invariants are independent of the rest.
  The feature can be safely deferred (unspent dollars preserved) or
  built in any order without risking the core system.

### Step 3: Dependency ordering

The ablation matrix produces a dependency graph. If removing feature A
breaks feature B's invariants, then A must be built before B. This
gives a build order that minimizes speculator debt: no feature is
built on top of an unverified foundation.

```
SlotmapGenIDs ──────► CastSpell (needs valid entity handles)
       │
       ├────────────► GhostRelevance (needs valid ghost handles)
       │
       └────────────► EntityMigration (needs generational safety)

SessionExpiry ──────► RateLimit (expired sessions shouldn't rate-limit)

ZoneTick ───────────► ZoneSplit (split needs current population)
       │
       └────────────► CastSpell (spell needs current positions)

GhostRelevance ─────► CastSpell (fanout scoped by ghost range)
```

### Step 4: Cost-weighted ablation

Each feature has an implementation cost (engineering hours) and a
speculator-debt risk (probability × cost-of-rework). The ablation
matrix tells us which features are safe to defer (low risk) and which
must be built first (high risk if wrong).

| Feature         | Impl cost      | Load-bearing?                            | Defer? | Rationale                         |
| --------------- | -------------- | ---------------------------------------- | ------ | --------------------------------- |
| SlotmapGenIDs   | Low (~100 LoC) | Yes — breaks CastSpell, Ghost, Migration | No     | Cheap, foundational, do first     |
| ZoneTick        | Medium         | Yes — breaks ZoneSplit, CastSpell        | No     | Core loop, can't defer            |
| SessionExpiry   | Low            | Yes — breaks RateLimit, security         | No     | Cheap, security-critical          |
| CastSpell       | High           | No — independent of ZoneSplit            | Yes    | Complex, defer until tick works   |
| GhostRelevance  | Medium         | No — independent of ZoneSplit            | Yes    | Can stub with full-mesh initially |
| ZoneSplit       | Medium         | No — independent of CastSpell            | Yes    | Only needed at scale              |
| EntityMigration | Medium         | Yes — breaks on slotmap absence          | No     | Needed for correctness at scale   |
| RateLimit       | Low            | No — independent of game logic           | Yes    | Can add after launch              |

## plausible-witness-dag integration

### Predicate language

Each invariant is expressed as a Lean 4 predicate over the game state:

```lean
-- ZoneTick invariant: position updates correctly
def zone_tick_invariant (s : GameState) : Prop :=
  ∀ e : Entity, e ∈ s.zone.entities →
    e.position' = e.position + e.velocity * s.dt

-- CastSpell invariant: fanout is scoped to ghost range
def cast_spell_invariant (s : GameState) : Prop :=
  ∀ eff : EffectEntity, eff ∈ s.effects →
    ∀ target : Entity, target ∈ eff.fanout →
      dist(eff.position, target.position) ≤ GHOST_RANGE

-- Slotmap generational ID invariant
def slotmap_gen_invariant (s : GameState) : Prop :=
  ∀ h : EntityHandle, h ∈ s.pending_effects →
    s.slotmap.lookup(h).isSome →
      s.slotmap.lookup(h).gen = h.gen
```

### Witness search

plausible-witness-dag's `resolve` function takes a candidate predicate
and a deterministic readback, escalating through L0/L1/L2:

- **L0** (10 transactions, 1 zone): Quick smoke test. Catches gross
  invariant violations in seconds.
- **L1** (100 transactions, 5 zones): Medium scale. Catches
  interaction bugs between features (e.g., CastSpell + EntityMigration).
- **L2** (1000 transactions, 20 zones): Large scale. Catches
  performance-adjacent invariants (ZoneSplit threshold, rate limiting).

### Ablation run

For feature F being ablated:

1. Remove F's invariants from the predicate set
2. Keep all other features' invariants
3. Run `resolve` at L0, then L1, then L2
4. If a witness is found at any level → F is load-bearing
5. If no witness at L2 → F is deferrable

The escalation ladder ensures we don't waste compute: L0 runs in
seconds, L2 in minutes. Most ablation results are determined at L0.

## What this prevents

### Speculator debt examples

1. Building CastSpell before SlotmapGenIDs. Without generational
   handles, CastSpell's fanout targets can dangle. The ablation matrix
   shows removing SlotmapGenIDs breaks CastSpell's invariant. Build
   slotmap first, or pay to refactor CastSpell later.

2. Building GhostRelevance with full-mesh fallback. If we stub
   ghost relevance with full-mesh broadcast initially, the ablation
   shows full-mesh doesn't violate any invariant (it's a superset of
   AOI). The stub is safe: we can ship with full-mesh and add AOI
   later without rework. This is unspent dollars preserved.

3. Omitting EntityMigration. The ablation shows that without migration,
   entities that cross zone boundaries vanish (ZoneTick invariant
   broken). Migration is load-bearing and can't be deferred.

4. Building ZoneSplit before ZoneTick. The ablation shows ZoneSplit
   depends on ZoneTick's population count. Building split first means
   building a mock population tracker, then replacing it with the real
   tick. That's speculator debt: pay once for the mock, once for the
   real thing. Build tick first.

## Relationship to other RFDs

- **RFD 0008** (verification strategy): plausible-witness-dag is
  already the L2 runtime verification layer. This RFD extends its use
  from post-implementation verification to pre-implementation design
  validation.

- **RFD 0002** (zonefabric): The feature set being ablated is the
  zonefabric runtime operations (ZoneTick, CastSpell, GhostRelevance,
  ZoneSplit, EntityMigration).

- **RFD 0017** (slotmap): SlotmapGenIDs is the first feature to
  verify — it's foundational and cheap. The ablation matrix confirms
  it must be built before CastSpell and GhostRelevance.

- **RFD 0018** (security): SessionExpiry and RateLimit are security
  features that can be ablated independently of game logic. The matrix
  shows they're load-bearing for security invariants but not for game
  logic invariants — so they can be built in parallel with game logic.

## Economics

| Outcome                           | Cost without ablation        | Cost with ablation                       |
| --------------------------------- | ---------------------------- | ---------------------------------------- |
| Feature built that wasn't needed  | $10K-50K engineering         | $0 (deferred, unspent dollars preserved) |
| Feature built wrong, then rebuilt | $20K-100K (build + rework)   | $5K-25K (build right first time)         |
| Feature built in wrong order      | $10K-30K (mock + replace)    | $0 (correct order from dependency graph) |
| Feature correctly deferred        | $0 (but risk of building it) | $0 (confirmed safe to defer)             |

For a 20-feature MMO backend, ablation testing costs ~1 week of
engineering time (writing predicates + running the search). The
expected savings from avoiding one speculator-debt cycle: $20K-100K.
ROI: 10-50x.

## What this RFD does NOT cover

- Player-facing feature decisions: this ablation covers backend
  system features (tick, spell, ghost, migration). Deciding whether
  players want fireballs vs. ice storms is a game design question, not
  an invariant question.

- Performance ablation: this RFD covers correctness invariants.
  Performance ablation (does feature X slow down the hot path?) is
  measured by the benchmark harness (RFD 0013), not by
  plausible-witness-dag.

- UI/UX features: not modeled. These are subjective and must be
  playtested, not formally verified.
