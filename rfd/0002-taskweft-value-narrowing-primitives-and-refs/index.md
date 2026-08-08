---
title: "RFD 0002: taskweft/nif's value narrowing: primitives, and refs as pointer strings"
rfd: "0002"
state: discussion
scope: zf_kv.c / mud_kv.c FDB value encoding
---

## Problem

This project plans new FDB value types for `zf_kv.c` and `mud_kv.c`.
No RFD defined a shared encoding scheme for these new types. Without
a shared design, each new value type could use its own, incompatible
representation for references. `taskweft/nif` already solves this same
problem for its own interpreter and ABI boundaries.

## Decision

`taskweft/nif` narrows every value crossing an interpreter or ABI
boundary to one tagged union, `TwValue`, with seven kinds: `NIL`,
`BOOL`, `INT`, `FLOAT`, `STRING`, `ARRAY`, `DICT`. A reference is not
an eighth kind; it is a plain `STRING` shaped as an RFC 6901 JSON
Pointer, resolved against a flat `var -> Dict` state tree at the point
of use. This RFD proposes the same primitives-plus-refs-as-strings
principle for this project's own new FDB value types, without
migrating the existing, already-deployed `zf_zone_val_t`,
`zf_entity_val_t`, or `mud_session_val_t` structs.

## References

- Full source-read detail, the proposed `zf_value_kind_t` sketch, and
  open questions: `DETAILS.md`
- `taskweft/nif`: `standalone/tw_value.hpp`, `tw_loader.hpp`,
  `tw_state.hpp`

## Detail

{{< include DETAILS.md >}}
