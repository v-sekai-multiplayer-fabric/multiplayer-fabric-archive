**Scale knob:** zone count (direct, no multiplier)
**Source:** weftspun/scenario-tpcc-bench PR #2

Models weft-warp-loop's hub/instanced-zone game server. Each zone has 200 entities with uniform-random position/velocity. EFFECT_ENTITY and FANOUT_TARGET are runtime-only (produced by CastSpell, not seeded at load).

## Tables

| Table         | Rows              | Formula      |
| ------------- | ----------------- | ------------ |
| ZONE          | scalefactor       | 1 per zone   |
| ENTITY        | scalefactor × 200 | 200 per zone |
| EFFECT_ENTITY | 0 at load         | runtime-only |
| FANOUT_TARGET | 0 at load         | runtime-only |

Fixed constants: ENTITIES_PER_ZONE=200, WORLD_EXTENT=10000.0, GHOST_RANGE=150.0, AUTHORITY_CAPACITY=256, INTEREST_CAPACITY=512, SPLIT_COST_THRESHOLD=40000.0. Uniform (Flat) random for entity attributes. No skewed zone selection at load — any skew comes from runtime workload.

## FDB keyspace design

### Per-entity keyspace (current, granular)

```
zf/zone/{z_id}                         -> packed zone_t (authority_cap, interest_cap, cost, population)
zf/entity/{z_id}/{e_id}                -> packed entity_t (x, y, vx, vy, rtt_ms)
zf/entity_pos/{z_id}                   -> range: all entities in zone (for tick scan)
zf/effect/{ee_id}                      -> packed effect_entity_t (source_id, zone_id, x, y)
zf/fanout/{ee_id}/{e_id}               -> empty value (presence = fanout target)
```

### Zone-state keyspace (slotmap + zstd, batched)

With slotmap entity storage (RFD 0017) and zstd compression (RFD 0016),
the per-entity keyspace is superseded for persistence by a single
batched zone-state blob per zone per persistence tick:

```
zf/zone_state/{z_id}                   -> zstd-compressed binary blob containing:
                                            - zone_t header (cost, population, authority_cap, interest_cap)
                                            - Slotmap<EntityID, entity_t>  (dense array + generation counters)
                                            - Slotmap<EffectID, effect_entity_t> (runtime effects, if any)
                                            - FANOUT_TARGET adjacency list (effect_id → [entity_id...])
```

This reduces 200+ FDB keys per zone to 1 key per zone. At 10,000 zones,
that's 10,000 keys instead of 2,000,000 — a 200x reduction in FDB key
count and transaction conflict surface.

**Generational IDs survive serialization.** The slotmap's (index, version)
pairs are stored inside the blob. On load after a crash, all
EFFECT_ENTITY and FANOUT_TARGET references resolve correctly because the
generation counters are part of the persisted state. A fireball aimed at
Entity(index=12, gen=1) will safely fizzle if Entity 12 was destroyed
(gen bumped to 2) before the fireball resolved — the slotmap lookup
returns NULL, no dangling reference, no hitting the wrong player.

**Per-entity keyspace retained for:**

- Entity migration between zones (point read + clear + set)
- Ghost relevance queries (range scan by zone prefix)
- Real-time point lookups (CastSpell source entity)

Zone queries are FDB range scans over `zf/entity/{z_id}/` prefixes.
At 200 entities/zone, each range read returns ~200 KV pairs packed as
binary structs (RFD 0010). No SQL, no parsing — zero-copy struct cast.

The batched `zf/zone_state/` key is used for periodic persistence
(every 100ms = 10Hz), not per-tick writes. Per-tick computation uses
the in-memory slotmap only.

## Runtime operations

### ZoneTick (read-heavy, per-zone, per-tick)

1. Range scan `zf/entity/{z_id}/` → 200 entity_t structs
2. Update position: `x += vx * dt`, `y += vy * dt` (in C, no FDB)
3. Batch write updated entities (1 transaction, 200 sets)
4. If zone population² > SPLIT_COST_THRESHOLD, emit split event

**Cost**: 1 range read (200 KV pairs) + 1 commit (200 sets) per zone per tick.

### CastSpell (write-heavy, fanout)

1. Read source entity (1 point read)
2. Range scan entities within GHOST_RANGE of spell position
3. Insert effect_entity row
4. Insert fanout_target rows for each entity in range
5. Commit

**Cost**: 1 point read + 1 range read (~20 entities within 150.0 range)

- 1 effect insert + ~20 fanout inserts + 1 commit.

### GhostRelevance (read-only, interest management)

1. Range scan entities within GHOST_RANGE of zone boundary
2. Return entity IDs and positions to requesting zone

**Cost**: 1 range read (~10 entities near boundary).

## Comparison matrix

Zonefabric is a novel workload — there is no TechEmpower benchmark for
game server zone scaling. Each operation is mapped to its closest
existing benchmark with the specific numbers we need to beat:

| Zonefabric operation           | Closest benchmark               | Reference                        | Our target                                |
| ------------------------------ | ------------------------------- | -------------------------------- | ----------------------------------------- |
| ZoneTick (200 entities)        | EnTT ECS 10K entities/7 systems | 350µs total                      | <10µs (200 entities, 1 system)            |
| Zone query (FDB range scan)    | FDB range read                  | 3.6M keys/sec/core               | 18,000 zone queries/sec/core              |
| Entity position update         | FDB batch set + commit          | 35K writes/sec/core              | 175 zone ticks/sec/core (200 writes each) |
| CastSpell effect fanout        | FDB read + write mix            | 90K reads + 35K writes/sec/core  | ~1,000 casts/sec/core                     |
| Ghost relevance query          | QuickZone LBVH spatial query    | O(N log Z), 1M zones             | O(N) per zone (fixed 200)                 |
| Entity replication (full mesh) | Arcane full-mesh broadcast      | 2,750 CCU @ 60Hz, 8×c6in.4xlarge | >5,000 CCU (AOI-filtered, not full-mesh)  |

### Arcane comparison (closest game-server benchmark)

[brainy-bots/arcane-scaling-benchmarks](https://github.com/brainy-bots/arcane-scaling-benchmarks)
measured 2,750 CCU at 60Hz with <31ms latency on 8×c6in.4xlarge (16
vCPU each, 50 Gbps NIC). The ceiling was **NIC saturation** from O(N²)
full-mesh broadcast bandwidth.

Zonefabric's advantage: GHOST_RANGE=150.0 in a 10000.0 world means
each zone's interest bubble covers ~0.02% of world space. Replication
scales as O(N × ghost_entities) not O(N²). With AOI filtering, we
should sustain significantly higher CCU per core.

### FDB baseline (our storage layer)

FDB 7.3 official numbers, directly comparable since we use the same
C API:

| Workload                   | Single core        | 12-machine (48 cores) |
| -------------------------- | ------------------ | --------------------- |
| Reads (memory engine)      | 90,000/sec         | 5,540,000/sec         |
| Writes (memory engine)     | 35,000/sec         | 720,000/sec           |
| Range scans                | 3,600,000 keys/sec | —                     |
| 90/10 mixed                | —                  | 2,390,000 ops/sec     |
| Read latency (<75% load)   | 0.1-1ms            | 0.1-1ms               |
| Commit latency (<75% load) | 1.5-2.5ms          | 1.5-2.5ms             |

### ECS iteration baseline

From [abeimler/ecs_benchmark](https://github.com/abeimler/ecs_benchmark),
EnTT iterates 10K entities with 7 systems in 350µs. Zonefabric's tick
is 200 entities with 1 system (position+velocity update), which
extrapolates to ~7µs — negligible compared to FDB commit latency.

The entity update is NOT the bottleneck. The **network replication**
and **FDB transaction commit** are the binding constraints.

## Benchmark methodology

### Scaling test: zone count vs throughput

Run ZoneTick at varying zone counts (1, 10, 100, 1000, 10000) with
fixed 200 entities/zone. Measure:

1. Zone ticks/sec: how many zones can be ticked per second
2. Entity updates/sec: zone ticks × 200
3. FDB ops/sec: range reads + writes
4. P50/P99 latency: per zone tick
5. Core scaling: 1, 2, 4, 8, 16 threads

Expected: linear scaling until FDB commit latency becomes the bottleneck
(~2ms per commit → max ~500 zone ticks/sec/core with writes).

### CastSpell stress test

Sustained CastSpell at varying rates. Measure fanout latency and
throughput as effect count grows.

Expected: CastSpell is write-heavy (1 effect + ~20 fanouts per cast).
At 35K writes/sec/core, max ~1,670 casts/sec/core (21 writes per cast).

### Ghost relevance test

Measure ghost entity query latency as zone count grows. Ghost range
is fixed at 150.0, so the number of ghost entities per zone boundary
is constant (~10-20), regardless of total zone count.

Expected: O(1) per query, independent of zone count.

## What zonefabric measures that existing benchmarks don't

1. Spatial locality: zone-scoped queries hit FDB range scans, not
   random point reads. This exercises FDB's range scan path (3.6M
   keys/sec) rather than point reads (90K/sec).

2. Write amplification: each zone tick writes 200 KV pairs in one
   transaction. This measures FDB's batch commit throughput, not
   single-key write throughput.

3. Fanout factor: CastSpell produces variable fanout (entities
   within ghost range). This is a read-amplify-write pattern not
   captured by TPC-C or YCSB.

4. AOI-filtered replication: ghost relevance queries are spatial
   range scans, not full-table scans. This is the key differentiator
   from Arcane's full-mesh broadcast.

5. Core-scaling efficiency: with per-zone FDB transactions, each
   core processes independent zones. No cross-zone conflicts (entities
   belong to exactly one zone). This should give near-linear core
   scaling, unlike TPC-C where district-level conflicts cause retries.

## Bridge to mas-bandwidth/fps

Glenn Fiedler's ["Creating a first person shooter that scales to
millions of players"](https://mas-bandwidth.com/creating-a-first-person-shooter-that-scales-to-millions-of-players/)
proposes a three-layer architecture: player servers, world servers, and
a "world database" (Redis-like, async, in-memory). He explicitly states:
_"somebody just needs to make this world database"_ and _"creating this
world database would be a fantastic open source project."_

Zonefabric IS that world database, implemented on FDB instead of Redis.

### Concept mapping

| mas-bandwidth/fps concept                                          | Zonefabric implementation                         | Why FDB                                                      |
| ------------------------------------------------------------------ | ------------------------------------------------- | ------------------------------------------------------------ |
| **World database** (Redis-like, async)                             | FDB as the world database                         | ACID transactions, range scans, durability                   |
| **World server grid** (10km × 10km, 1km² cells)                    | ZONE rows with entity subspace per zone           | FDB key prefix = spatial partition                           |
| **Player state history** (100 bytes × 100Hz × 10K players = 100MB) | ENTITY rows with x, y, vx, vy, rtt_ms             | FDB range scan returns packed structs                        |
| **"Raycast and find first hit"**                                   | CastSpell with GHOST_RANGE scan                   | FDB range read over entity positions                         |
| **"Find players in volume"**                                       | GhostRelevance spatial range query                | FDB range scan filtered by ghost range                       |
| **"Apply damage to entity"**                                       | CastSpell fanout_target insert                    | FDB transactional write                                      |
| **Shallow state** (100 bytes, interpolation)                       | entity_t packed struct (~40 bytes)                | Binary value encoding (RFD 0010)                             |
| **Deep state** (1000 bytes, prediction/rollback)                   | Not modeled (future: add history ring)            | FDB version stamps for MVCC history                          |
| **Async calls** (goroutine per player, blocking on world DB)       | H2O event loop + FDB async callbacks              | fdb_future_set_callback (RFD 0011)                           |
| **Input-driven simulation** (no global tick)                       | ZoneTick is per-zone, on-demand                   | Each zone is an independent FDB transaction                  |
| **XDP kernel bypass** (10G NIC, 50K players/server)                | H2O HTTP/TCP (not UDP/XDP)                        | HTTP is the benchmark transport; XDP is production transport |
| **O(N²) snapshot delivery**                                        | AOI-filtered (GHOST_RANGE=150.0 in 10000.0 world) | 0.02% of world space per zone — not full-mesh                |
| **Static geometry, no player collision**                           | Entities have position+velocity only              | No physics engine — pure data operations                     |
| **8K-50K players per 32-CPU server**                               | Benchmark target: zones per core                  | FDB scales linearly with cores                               |

### What mas-bandwidth/fps has that zonefabric does not (yet)

1. UDP/XDP transport: Glenn uses XDP kernel bypass for raw packet
   throughput. We use HTTP/TCP via H2O. XDP is a production transport
   optimization; the benchmark measures the world database layer, not
   the packet processing layer. Adding a UDP/XDP transport is a future
   deployment concern, not a benchmark concern.

2. Client-side prediction + rollback: Glenn's deep state (1000 bytes)
   includes prediction history for lag compensation. Zonefabric's
   entity_t is shallow (position + velocity). FDB's MVCC version stamps
   could provide history, but this is not modeled in the current schema.

3. Delta compression: Glenn assumes 10x bandwidth reduction via delta
   compression against a baseline. Zonefabric measures raw FDB
   throughput; compression is a transport-layer concern.

4. Real-time multicast: Glenn identifies O(N²) snapshot delivery as
   the binding constraint. Zonefabric avoids this via AOI filtering
   (GHOST_RANGE), but does not model the snapshot delivery path.

### What zonefabric adds beyond mas-bandwidth/fps

1. Measured, not estimated: Glenn's numbers are back-of-the-envelope
   ("100MB for history", "1Gbit/sec for state"). Zonefabric produces
   actual measured throughput via wrk against FDB.

2. ACID guarantees: Glenn's "world database" is conceptual (Redis-
   like). FDB provides strict serializable ACID transactions.
   CastSpell's effect + fanout inserts are atomic, with no partial writes
   on failure.

3. Spatial range queries: Glenn's world database is key-value with
   "raycast" primitives. FDB's ordered key space enables native range
   scans, so finding all entities within GHOST_RANGE is a single range
   read, not a custom spatial index.

4. Zone split cost model: Zonefabric models SPLIT_COST_THRESHOLD
   (population² > 40000 → split). mas-bandwidth/fps assumes static
   grid cells. Zonefabric measures the cost of dynamic zone splitting.

5. Core-scaling measurement: mas-bandwidth/fps estimates 8K-50K
   players per 32-CPU server. Zonefabric measures actual zone tick
   throughput per core, providing ground truth for capacity planning.

### The path from utopia to here

```
mas-bandwidth/fps (concept)          zonefabric (measurement)
┌──────────────────────┐            ┌──────────────────────┐
│ Player servers       │            │ (not modeled —       │
│ (XDP, 50K players)   │            │  HTTP/wrk is the     │
│                      │            │  load generator)     │
│ World servers        │── maps to──│ ZONE + ENTITY tables │
│ (10km grid, 1km²)    │            │ (FDB range scans)     │
│                      │            │                      │
│ World database       │── maps to──│ FoundationDB         │
│ (Redis-like, async)  │            │ (ACID, range scans)  │
│                      │            │                      │
│ "Raycast, find in    │── maps to──│ CastSpell,           │
│  volume, apply dmg"  │            │ GhostRelevance       │
│                      │            │                      │
│ O(N²) snapshots      │── avoided──│ AOI (GHOST_RANGE)    │
│                      │            │ 0.02% world coverage │
└──────────────────────┘            └──────────────────────┘
```

Glenn's architecture is the target state. Zonefabric is the
benchmark that proves whether FDB can serve as the world database
that makes that architecture possible. The gap between "utopia" and
"what we have" is:

- Transport: HTTP/TCP (benchmark) → UDP/XDP (production)
- State depth: shallow only → add prediction history
- Scale: zones per core → players per server (add player server layer)
- Delivery: request/response → snapshot multicast (add world server layer)

Each gap is a future RFD. The world database layer, the hardest part,
is what zonefabric benchmarks today.

## Architectural completeness

Three layers combine to form a complete blueprint for 25M CCU:

### 1. Network fabric — XDP/eBPF (future RFD)

Handles massive UDP fanout traffic at line rate (10G+), bypassing the
OS kernel. mas-bandwidth/fps demonstrates 50K players per 32-CPU server
with XDP. This is the transport layer — not benchmarked by zonefabric
(HTTP/wrk is the benchmark transport), but the architecture is
XDP-ready: zonefabric's operations are transport-agnostic.

### 2. Compute and storage — zstd + FDB + slotmap (RFDs 0016, 0017)

Per-tick computation uses the in-memory slotmap (RFD 0017) — no FDB
calls on the hot path. Persistence is batched at 10Hz: the entire zone
state (slotmap + effects + fanout) is serialized as a contiguous buffer,
zstd-compressed (RFD 0016), and written as a single FDB key.

At 25M CCU with 200 entities/zone, that's 125,000 zones. At 10Hz
persistence, that's 1.25M zone-state commits/sec. With 100 zones per
FDB transaction batch, that's 12,500 commits/sec — well within FDB's
single-core capacity (35K writes/sec/core on memory engine). Across
48 cores: 600K commits/sec capacity, 48x headroom.

The zstd compression drops each zone-state blob from ~8KB to ~3KB
(2-3x), reducing FDB storage and network transfer proportionally.

### 3. Memory safety — slotmap generational IDs (RFD 0017)

The slotmap's generational handles solve the dangling reference problem
inherent in dynamic game simulations:

- CastSpell fanout: Effect 10 targets Entity(index=12, gen=1).
  Entity 12 disconnects → slot freed, gen bumped to 2. New player
  takes slot 12 (gen=2). Effect 10 resolves: slotmap lookup sees
  gen mismatch → returns NULL → fireball fizzles. No wrong target hit.

- Ghost entities: Ghost reference to Entity(index=47, gen=3) in
  zone A. Entity migrates to zone B → slot freed in zone A, gen=4.
  Zone A's ghost lookup fails safely. Zone B creates new slot with
  its own generation.

- Crash recovery: Zone-state blob is loaded from FDB. Slotmap
  generation counters are part of the persisted state. All
  EFFECT_ENTITY and FANOUT_TARGET references resolve correctly
  post-recovery, with no stale handles and no corruption.

### Why this is complete

| Problem                                 | Solution                                                     | RFD        |
| --------------------------------------- | ------------------------------------------------------------ | ---------- |
| Dangling references (use-after-free)    | Slotmap generational IDs                                     | 0017       |
| FDB write amplification (200 keys/zone) | Batched zone-state blob (1 key/zone)                         | 0002       |
| Bandwidth / storage size                | zstd compression (2-3x on batches)                           | 0016       |
| O(N²) snapshot delivery                 | AOI filtering (GHOST_RANGE=150.0, 0.02% world)               | 0002       |
| Core scaling contention                 | Per-zone independent FDB transactions, thread-local slotmaps | 0005, 0017 |
| Transport bottleneck (kernel overhead)  | XDP/eBPF kernel bypass (future)                              | —          |
| World database (Glenn Fiedler's gap)    | FDB as ACID async world database                             | 0006, 0011 |

The three layers compose: XDP handles the packet flood, zstd + FDB +
slotmap handle compute and storage, generational IDs handle memory
safety. No layer requires the others to function, but together they
eliminate every known bottleneck for 25M CCU.
