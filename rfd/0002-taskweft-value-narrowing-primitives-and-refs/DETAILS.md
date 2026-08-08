## Summary

`taskweft/nif` (the C++ core behind the `taskweft/taskweft` Elixir host,
a separate repo from the one the name suggests) narrows every value that
crosses an interpreter or ABI boundary down to one small tagged union,
`TwValue`, with seven kinds: `NIL`, `BOOL`, `INT`, `FLOAT`, `STRING`,
`ARRAY`, `DICT`. There is no eighth kind for references. A reference is a
plain `STRING` value, shaped as an RFC 6901 JSON Pointer, resolved
against a flat `var -> Dict` state tree at the point of use. This RFD
records that real design, checked directly against the source, and
proposes the same pattern for this project's own FDB value encoding
(`zf_kv.c`/`mud_kv.c`), which today hand-rolls one packed C struct per
value type instead of one shared, narrow value representation.

## Background

Earlier work this session read `taskweft/taskweft`'s own RFD 0004
("KHR_interactivity Tier 2 -- embed libriscv, compile behavior graphs to
riscv64") for a different reason, and carried forward a working
assumption that its value system split into "primitives and refs" as two
separate, peer kinds. A direct source read for this RFD found that
assumption imprecise. The real split is primitives (the `TwValue` enum
itself) plus a convention, layered on top, for treating one primitive
kind (`STRING`) as a reference when its content parses as a JSON
Pointer. Recording the corrected, source-checked version here, not the
earlier paraphrase.

## Motivation

Three real, separate things make this worth a project-level record
instead of a one-off note.

First, `zf_kv.c` and (this session's own new) `mud_kv.c` both hand-roll
one packed C struct per stored value type (`zf_zone_val_t`,
`zf_entity_val_t`, `mud_session_val_t`). Every new value type needs a new
struct, a new encode function, and a new decode function. `TwValue`'s own
shape shows a real, working alternative already proven at a real
production scale (`taskweft/nif` backs a real planner and ReBAC engine):
one shared tagged union covers every value type this project has needed
so far (integers, floats, strings, small maps), and a plain-string
convention covers references without a second tagged-union arm.

Second, `mud_kv.c`'s own turn-history keyspace
(`zf/mud/turn/{session_id}/{turn}`) already stores a plain string value
(the narration text) with no packed struct at all -- it already,
independently, arrived at the same "some values are just strings" shape
`TwValue` uses, without deliberately copying it. That is a real signal
the pattern fits this project's own data, not just taskweft's.

Third, `taskweft/nif`'s own NIF boundary (`c_src/taskweft_nif.cpp`)
narrows even further at the actual ABI crossing: no `TwValue` ever
crosses that boundary directly, only `std::string` (carrying serialized
JSON), `int64_t`, `double`, `bool`, and `std::vector<std::string>`.
Structured payloads travel as JSON text, parsed back into `TwValue` on
the receiving side. This project already does the JSON-LD-framed CBOR
equivalent of that at the `mud-sandbox-orchestrator` boundary (per the
`sandboxed-godot-in-zone-server-h2o` decision doc), so this is a second,
independent confirmation of the same narrowing principle at a different
boundary, not a new one to adopt.

## Proposal

### The real `TwValue` shape, as it exists in `taskweft/nif`

File `standalone/tw_value.hpp`:

```cpp
class TwValue {
public:
    enum class Type { NIL, BOOL, INT, FLOAT, STRING, ARRAY, DICT };
    using Array = std::vector<TwValue>;
    using Dict  = tsl::ordered_map<std::string, TwValue>;

private:
    Type    _type = Type::NIL;
    bool    _b    = false;
    int64_t _i    = 0;
    double  _f    = 0.0;
    std::string                 _s;
    std::unique_ptr<Array>      _arr;
    std::unique_ptr<Dict>       _dct;
```

A hand-written tagged union, not `std::variant` -- deep-copying copy
constructor, a `stable_hash()` used for planner memoization (sorted dict
keys, `NaN` and `-0.0` normalized), and structural `operator==`/`<`.
`Dict` uses `tsl::ordered_map` specifically so key iteration order
matches Python dict insertion order, for determinism against a reference
implementation -- stated directly in the file's own header comment, not
inferred.

### References are `STRING`s shaped as JSON Pointers, not a distinct kind

`standalone/tw_loader.hpp`:

```cpp
// RFC 6901 -- parse a JSON Pointer into decoded reference tokens.
// Empty string -> {} (whole-document reference).
inline std::vector<std::string> parse_rfc6901(const std::string &ptr) { ... }

// Resolve a templated pointer into Taskweft's 2-segment (var, key) shape.
inline std::pair<std::string, TwValue> parse_pointer(
        const std::string &ptr, const Params &params) {
    auto tokens = parse_rfc6901(substitute_pointer(ptr, params));
    if (tokens.size() != 2) return {"", TwValue{}};
    return {std::move(tokens[0]), TwValue(std::move(tokens[1]))};
}
```

`standalone/tw_state.hpp` holds what those pointers resolve against: a
flat `var -> Dict` map, not a slotmap or entity-handle table.

```cpp
struct TwState {
    tsl::ordered_map<std::string, TwValue> vars;
    std::shared_ptr<const TwReBAC::TwReBACGraph> rebac_graph;
    int rebac_fuel = 8;
    void set_var(const std::string &key, TwValue val) { ... }
    TwValue get_var(const std::string &key) const { ... }
};
```

RFD 0002 in `taskweft/taskweft` itself (its own numbering, a different
sequence from this repo's) is where the `pointer/get`/`pointer/set`
node-type convention was introduced, aligning on the glTF
`KHR_interactivity` node model. RFD 0003 there covers the calling
convention for value-computing node types (one `TwValue` in, one out,
`(a,b,c,d)` input-slot table, a `b`-selector for multi-output nodes) --
decomposition and structural-node policy, not the value union's own
shape. RFD 0004 there is the one with the ABI-crossing line this
project's earlier work already cited correctly: "Values cross the ABI
as JSON, reusing existing `TwValue` (de)serialization."

### Recommendation for this project's own FDB value encoding

`zf_kv.h`/`mud_kv.h` today: one packed C struct per value type, one
hand-written encode function, one hand-written decode function, per
type. Real, working, and already verified (this project's own golden-
vector and multi-zone-isolation tests), but every new value type this
project adds needs the same three-part boilerplate again.

Proposed direction, not yet built: a single narrow tagged value type for
new FDB value encoding, shaped like `TwValue` but scoped to what this
project's own data actually needs --

```c
typedef enum {
    ZF_VAL_INT,
    ZF_VAL_FLOAT,
    ZF_VAL_STRING,
    ZF_VAL_BYTES,   /* the existing xr_grid_entity_packet_t-style packed
                        wire structs stay valid as one more "kind" here,
                        not replaced -- see Open questions below */
} zf_value_kind_t;
```

with references handled the same way `TwValue` handles them: not a
distinct kind, a `ZF_VAL_STRING` whose content is one of this project's
own existing FDB key-builder outputs (`zf_kv_entity_key`,
`mud_kv_turn_key`, ...), resolved by a plain FDB `fdb_async_get()` at
the point of use, mirroring `parse_pointer`'s own resolve-at-use-time
shape rather than eagerly dereferencing.

This is a real, scoped design proposal, not a decision -- see Open
questions below for what blocks moving from proposal to implementation.

## Recommendation and next steps

Adopt the "primitives plus refs-as-strings" principle for **new** FDB
value types added after this RFD, without migrating `zf_zone_val_t`,
`zf_entity_val_t`, or `mud_session_val_t` -- those are real, tested, and
already have real data behind them in every environment this project has
deployed to. `mud_kv.c`'s own turn-history value (plain narration text,
no struct) already follows this RFD's own recommendation by coincidence;
treat that as the first real example, not an exception to fix.

Next real step, not done by this RFD: prototype `zf_value_kind_t` (or an
equivalent) against one genuinely new value type this project needs
next, and verify it against a real golden-vector test the same way
`xr_grid_entity_packet.c` was checked against `lean-entity-packet`,
before treating the pattern as this project's own default.

## Open questions and verification

- Whether `zf_value_kind_t`'s `ZF_VAL_BYTES` escape hatch (for
  `xr_grid_entity_packet_t` and any future packed wire struct) undermines
  the whole point of narrowing -- if most of this project's real values
  end up as `ZF_VAL_BYTES` anyway, the narrowing buys little. Needs a
  real inventory of this project's own value types before deciding,
  not assumed either way here.
- Whether tagged-union overhead (a discriminant byte plus the widest
  member's storage, per value) is acceptable inside FDB's own real
  single-transaction byte-size ceiling (already measured this session,
  see `zone-server-h2o`'s own real throughput verification work) at this
  project's real entity-count scale (1400/1800 per zone).
- `taskweft/nif`'s own `Dict` ordering guarantee (`tsl::ordered_map`,
  for cross-implementation determinism against a Python reference) has
  no equivalent need in this project yet -- flagged so a future
  prototype does not import that dependency without first confirming a
  real, present need for it here.
