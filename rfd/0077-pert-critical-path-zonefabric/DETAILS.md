## Method

Expected duration (TE) for each task uses the PERT formula:

```
TE = (O + 4M + P) / 6
```

Variance (σ²) measures uncertainty:

```
σ² = ((P - O) / 6)²
```

The critical path is the chain of dependent tasks with the longest total
TE. Any delay on the critical path delays the entire project. Tasks off
the critical path have slack — they can slip without affecting the end
date.

## Task list

All durations in engineering days. One engineer, full-time.

| ID  | Task                                            | Depends on | O   | M   | P   | TE  | σ²   |
| --- | ----------------------------------------------- | ---------- | --- | --- | --- | --- | ---- |
| A   | Binary value encoding (RFD 0010)                | —          | 1   | 2   | 4   | 2.2 | 0.25 |
| B   | FDB keyspace + async callbacks (RFD 0009, 0011) | A          | 2   | 3   | 6   | 3.3 | 0.44 |
| C   | Actor-lite worker pool (RFD 0005)               | B          | 3   | 5   | 9   | 5.3 | 1.00 |
| D   | Slotmap entity storage (RFD 0017)               | A, B       | 2   | 3   | 5   | 3.2 | 0.25 |
| E   | Zstd compression (RFD 0016)                     | A          | 1   | 2   | 3   | 2.0 | 0.11 |
| F   | ZoneTick (read/write loop)                      | C, D       | 3   | 4   | 8   | 4.5 | 0.69 |
| G   | Zone-state blob persistence                     | D, E, F    | 2   | 3   | 5   | 3.2 | 0.25 |
| H   | GhostRelevance (AOI query)                      | D, F       | 2   | 3   | 6   | 3.3 | 0.44 |
| I   | CastSpell (effect + fanout)                     | D, F, H    | 3   | 5   | 10  | 5.5 | 1.36 |
| J   | EntityMigration (zone handoff)                  | D, F       | 2   | 4   | 7   | 4.2 | 0.69 |
| K   | ZoneSplit (cost threshold)                      | F, G       | 2   | 3   | 5   | 3.2 | 0.25 |
| L   | Macaroon + XDP security (RFD 0018)              | C, D       | 4   | 7   | 14  | 7.7 | 2.78 |
| M   | Feature ablation (RFD 0019)                     | F, H, I    | 3   | 5   | 8   | 5.2 | 0.69 |
| N   | Benchmark harness (wrk scripts)                 | F, G       | 1   | 2   | 3   | 2.0 | 0.11 |
| O   | Core-scaling measurement                        | N          | 2   | 3   | 5   | 3.2 | 0.25 |

## Dependency graph

```
A (binary encoding)
├── B (FDB keyspace + async)
│   ├── C (actor-lite pool)
│   │   ├── F (ZoneTick) ◄═══ CRITICAL PATH
│   │   │   ├── G (zone-state blob)
│   │   │   │   └── K (ZoneSplit)
│   │   │   ├── H (GhostRelevance)
│   │   │   │   └── I (CastSpell)
│   │   │   │       └── M (ablation)
│   │   │   ├── J (EntityMigration)
│   │   │   └── N (benchmark harness)
│   │   │       └── O (scaling measurement)
│   │   └── L (Macaroon + XDP)
│   └── D (slotmap) ──► (feeds into F, H, I, J, L)
├── E (zstd) ──► (feeds into G)
└── (A also feeds D, E directly)
```

## Critical path

The critical path is the longest chain through the dependency graph:

```
A → B → C → F → I → M
```

| Step | Task                              | TE (days) | Cumulative |
| ---- | --------------------------------- | --------- | ---------- |
| 1    | A: Binary value encoding          | 2.2       | 2.2        |
| 2    | B: FDB keyspace + async callbacks | 3.3       | 5.5        |
| 3    | C: Actor-lite worker pool         | 5.3       | 10.8       |
| 4    | F: ZoneTick                       | 4.5       | 15.3       |
| 5    | I: CastSpell                      | 5.5       | 20.8       |
| 6    | M: Feature ablation               | 5.2       | 26.0       |

**Critical path total: 26.0 engineering days (~5.2 weeks)**

This is the minimum time to a verified, ablation-tested zonefabric
benchmark with CastSpell. The critical path passes through CastSpell
because it depends on three upstream tasks (slotmap, tick, ghost) and
has the highest variance (σ² = 1.36) — it's both the longest and the
riskiest task.

## Slack analysis

Tasks NOT on the critical path have slack. Slack = (latest finish) −
(earliest start + duration).

| Task                   | TE  | Earliest start  | Latest finish   | Slack |
| ---------------------- | --- | --------------- | --------------- | ----- |
| D: Slotmap             | 3.2 | 5.5 (after A+B) | 10.8 (before F) | 2.1   |
| E: Zstd                | 2.0 | 2.2 (after A)   | 15.3 (before G) | 11.1  |
| G: Zone-state blob     | 3.2 | 15.3 (after F)  | 19.8 (before K) | 1.3   |
| H: GhostRelevance      | 3.3 | 15.3 (after F)  | 20.8 (before I) | 2.2   |
| J: EntityMigration     | 4.2 | 15.3 (after F)  | 26.0 (end)      | 6.5   |
| K: ZoneSplit           | 3.2 | 18.5 (after G)  | 26.0 (end)      | 4.3   |
| L: Macaroon + XDP      | 7.7 | 10.8 (after C)  | 26.0 (end)      | 7.5   |
| N: Benchmark harness   | 2.0 | 18.5 (after G)  | 26.0 (end)      | 5.5   |
| O: Scaling measurement | 3.2 | 20.5 (after N)  | 26.0 (end)      | 2.3   |

### Slack interpretation

- E (Zstd) has 11.1 days of slack. Compression can be added any
  time before zone-state blob persistence. This confirms RFD 0019's
  ablation result: zstd is deferrable. Do NOT build it first.

- L (Macaroon + XDP) has 7.5 days of slack. Security can be built
  in parallel with game logic. This confirms RFD 0019: security
  features are independent of game-logic invariants.

- J (EntityMigration) has 6.5 days of slack. Migration is needed
  for correctness at scale but can be stubbed initially (entities
  stay in their birth zone). Build it after the core loop works.

- D (Slotmap) has only 2.1 days of slack and is near-critical.
  This confirms RFD 0019: slotmap is foundational and must be built
  early. Any delay on slotmap pushes the critical path.

## Milestones

| Milestone             | Cumulative days | What's working                                                                              |
| --------------------- | --------------- | ------------------------------------------------------------------------------------------- |
| M1: FDB reads/writes  | 5.5             | Binary encoding + FDB keyspace. Can read/write entity rows.                                 |
| M2: Worker pool       | 10.8            | H2O event loop dispatches to worker threads via SPSC ring.                                  |
| M3: Zone tick         | 15.3            | ZoneTick reads entities, updates positions, writes back. **First benchmarkable milestone.** |
| M4: Ghost queries     | 18.6            | GhostRelevance returns entities within GHOST_RANGE.                                         |
| M5: CastSpell         | 20.8            | Effects + fanout work. **Full game loop operational.**                                      |
| M6: Ablation verified | 26.0            | Feature ablation confirms no speculator debt. **Ship-ready.**                               |

## Parallelization with two engineers

If a second engineer is available, the slack tasks can be parallelized:

| Engineer 1 (critical path) | Engineer 2 (slack tasks)                        |
| -------------------------- | ----------------------------------------------- |
| A: Binary encoding (2.2d)  | (pair on A)                                     |
| B: FDB keyspace (3.3d)     | E: Zstd (2.0d) → slack                          |
| C: Actor-lite pool (5.3d)  | D: Slotmap (3.2d) → L: XDP (7.7d, partial)      |
| F: ZoneTick (4.5d)         | H: GhostRelevance (3.3d) → J: Migration (4.2d)  |
| I: CastSpell (5.5d)        | G: Zone-state blob (3.2d) → K: ZoneSplit (3.2d) |
| M: Ablation (5.2d)         | N: Benchmark harness (2.0d) → O: Scaling (3.2d) |

With two engineers, the critical path remains A→B→C→F→I→M at 26 days
(Engineer 1), but all slack tasks are completed in parallel. Total
project duration: ~26 days instead of ~38 days (serial sum).

The critical path cannot be shortened by adding engineers — it's
sequential by dependency. Only reducing task scope (e.g., stubbing
CastSpell's fanout as a fixed-radius range scan without effect
entities) can shorten it.

## Risk register

| Risk                                           | Probability | Impact                   | Mitigation                                                    |
| ---------------------------------------------- | ----------- | ------------------------ | ------------------------------------------------------------- |
| CastSpell fanout complexity exceeds estimate   | Medium      | +3 days on critical path | Stub with full-radius scan first, optimize later              |
| FDB async callback chain has memory bugs       | Low         | +5 days (debugging)      | CBMC verification (RFD 0008) before integration               |
| Slotmap serialization breaks generational IDs  | Low         | +2 days on D             | Test round-trip serialize/deserialize before F integration    |
| XDP/eBPF program rejected by verifier          | Medium      | +4 days on L             | Start L early (has 7.5d slack), use libbpf skeletons          |
| Zstd dictionary training needed for good ratio | Low         | +1 day on E              | Use level 3 default, skip dictionary training initially       |
| Actor-lite SPSC ring has race condition        | Low         | +3 days on C             | Lean 4 proof of linearizability (RFD 0008) before C completes |

## Build order (recommended)

Based on the PERT analysis and RFD 0019 ablation matrix:

```
Week 1: A (binary encoding) → B (FDB keyspace) → D (slotmap)
Week 2: C (actor-lite pool) → F (ZoneTick) → E (zstd, parallel)
Week 3: H (ghost) → G (zone-state blob) → N (benchmark harness)
Week 4: I (CastSpell) → J (migration) → K (zone split)
Week 5: L (XDP security, parallel) → M (ablation) → O (scaling)
```

This order respects all dependencies, keeps the critical path moving,
and defers high-slack tasks (zstd, XDP, zone split) to later weeks.

## Relationship to other RFDs

- **RFD 0019** (feature ablation): The dependency graph in this PERT
  chart is derived from the ablation matrix. The ablation confirms
  which tasks are load-bearing (on or near the critical path) and
  which are deferrable (high slack).

- **RFD 0008** (verification): CBMC and Lean 4 verification happen
  during tasks C (SPSC ring) and D (slotmap), not as a separate
  phase. Verification is built into the task estimates.

- **RFD 0014** (CI): The CI pipeline is assumed to exist from the
  start (building on every commit). It is infrastructure that
  supports all tasks, not a separate task.

- **RFD 0013** (benchmark harness): Task N is the wrk script
  implementation, which depends on the zone-state blob (G) being
  writable to FDB.
