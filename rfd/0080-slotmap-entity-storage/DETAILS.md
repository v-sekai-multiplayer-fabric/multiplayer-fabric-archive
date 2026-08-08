## Rationale

Zonefabric's ZoneTick iterates all 200 entities in a zone every tick.
The iteration pattern is:

1. Range scan FDB `zf/entity/{z_id}/` → 200 KV pairs (cold path)
2. Deserialize into in-memory entity array (warm path)
3. Iterate: update position += velocity × dt (hot path)
4. Batch write back to FDB (cold path)

Steps 1 and 4 are FDB operations (~1ms latency). Step 3 is pure compute
(~7µs for 200 entities). Step 2 is the deserialization bridge — and
its data structure choice determines whether step 3 is cache-friendly.

A hash map (open-addressing) scatters entities across memory based on
hash of entity ID. Iterating 200 entities from a hash map of capacity
1024 touches ~8 cache lines randomly — 8 potential L1 misses.

A slotmap stores entities in a dense array indexed by a slot index.
Iteration is a sequential scan of the dense array — 200 entities × 40
bytes = 8KB, which fits in 128 cache lines, accessed sequentially with
hardware prefetch. Zero L1 misses after warmup.

## Slotmap design

```c
typedef struct {
    uint32_t index;   // slot index into entries[]
    uint32_t version; // generation counter for ABA safety
} zf_slot_t;

typedef struct {
    zf_entity_t entity;    // 40 bytes: the actual data
    uint32_t version;      // generation: incremented on free
    uint32_t next_free;    // free list link (UINT32_MAX = end)
} zf_slot_entry_t;

typedef struct {
    zf_slot_entry_t *entries;  // dense array, capacity = power of 2
    uint32_t *slots;           // slot index → entry index (or free)
    uint32_t free_head;        // head of free list in entries[]
    uint32_t count;            // live entity count
    uint32_t capacity;         // total slots
} zf_slotmap_t;
```

### Operations

**Insert** (entity enters zone):

```
entry_idx = free_head
free_head = entries[entry_idx].next_free
entries[entry_idx].entity = entity
entries[entry_idx].version++
slot_idx = allocate_slot()
slots[slot_idx] = entry_idx
return {slot_idx, entries[entry_idx].version}
```

**Remove** (entity leaves zone):

```
entry_idx = slots[slot.version]
if entries[entry_idx].version != slot.version: return STALE
entries[entry_idx].next_free = free_head
free_head = entry_idx
entries[entry_idx].version++  // invalidate all existing handles
free_slot(slot.index)
count--
```

**Lookup** (random access by handle):

```
entry_idx = slots[handle.index]
if entries[entry_idx].version != handle.version: return NULL
return &entries[entry_idx].entity
```

**Iterate** (tick all entities):

```
for i in 0..count:
    entity = &entries[dense_order[i]].entity
    entity->x += entity->vx * dt
    entity->y += entity->vy * dt
```

Iteration uses a dense order array so live entities are contiguous,
even after removals (removal swaps the last entry into the freed slot).

## Why not just a flat array?

A flat array works if entities never leave zones. But zonefabric has
entity migration — entities cross zone boundaries. When entity #47 of
200 leaves, a flat array either:

- Leaves a gap (iteration must skip dead entries → branch per entity)
- Compacts (swap-remove → O(1) but changes entity ordering)

A slotmap gives O(1) insert/remove with stable handles (generational
indices prevent ABA problems where a recycled slot is mistaken for a
live entity). The dense iteration order is maintained by swap-remove
on the slot array.

## Why not entt / flecs / an ECS framework?

From [abeimler/ecs_benchmark](https://github.com/abeimler/ecs_benchmark),
EnTT iterates 10K entities with 7 systems in 350µs. A slotmap with
200 entities and 1 system (position+velocity) takes ~7µs. The overhead
of a full ECS framework (archetype storage, component registration,
system scheduling, query matching) is not justified for a single
component type and a single system.

The slotmap is ~100 lines of C. EnTT is ~15K lines of C++. We need
the iteration speed, not the abstraction.

## Generational handles and FDB sync

When an entity migrates from zone A to zone B:

1. Zone A: `slotmap_remove(sm_a, handle)` — version bumped, slot freed
2. FDB: `fdb_transaction_clear(zf/entity/{a_id}/{e_id})` — delete key
3. FDB: `fdb_transaction_set(zf/entity/{b_id}/{e_id}, packed_entity)` — insert key
4. Zone B: `handle_b = slotmap_insert(sm_b, entity)` — new slot, new version

The generational handle prevents zone A from using a stale handle to
access an entity that has already migrated to zone B. The version
mismatch on lookup returns NULL, signaling the caller to re-read from
FDB.

## Memory layout and cache behavior

For 200 entities at 40 bytes each:

| Structure           | Memory                     | Iteration cache misses      |
| ------------------- | -------------------------- | --------------------------- |
| Hash map (cap 1024) | 40KB + 4KB indices         | ~8 (random access)          |
| Linked list         | 200 × (40 + 8 ptr) = 9.6KB | ~200 (pointer chasing)      |
| Slotmap (dense)     | 200 × 40 = 8KB             | ~1 (sequential, prefetched) |
| Flat array          | 200 × 40 = 8KB             | ~1 (same as slotmap)        |

The slotmap matches flat array iteration speed while supporting O(1)
insert/remove with stable handles.

## Capacity and growth

Initial capacity: 256 (AUTHORITY_CAPACITY from RFD 0002). Grows by
doubling when count reaches capacity. Growth is amortized O(1) — the
realloc + copy is rare (log₂(200/256) = 0 growth events at steady
state).

Memory per zone: 256 × (40 + 8) = 12KB. For 10,000 zones: 120MB.
Negligible vs FDB's storage overhead.

## Relationship to other RFDs

- **RFD 0002** (zonefabric): defines ENTITIES_PER_ZONE=200 and
  AUTHORITY_CAPACITY=256. Slotmap capacity = AUTHORITY_CAPACITY.
- **RFD 0010** (binary encoding): FDB values are packed structs. The
  slotmap stores the same struct in memory — `memcpy` from FDB value
  to slotmap entry, no conversion.
- **RFD 0011** (async callbacks): FDB range scan callback populates
  the slotmap. Each KV pair in the range result is one
  `slotmap_insert`.
- **RFD 0016** (zstd): if the FDB value is compressed, decompress
  before `memcpy` into the slotmap entry. The slotmap always stores
  uncompressed data.
- **RFD 0005** (actor-lite): each worker thread owns its zone's
  slotmap. No cross-thread access — the slotmap is thread-local.
  No locks, no atomics on the hot path.
