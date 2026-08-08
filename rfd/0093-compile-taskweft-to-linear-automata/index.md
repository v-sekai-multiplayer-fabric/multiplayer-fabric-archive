---
title: "RFD 0093: Compile taskweft relation expressions to linear automata"
rfd: "0093"
state: discussion
scope: taskweft-nif evaluation, zone-server-h2o ReBAC
---

## Problem

`taskweft` evaluates a relation expression by recursive descent with a
fuel counter (`check_expr`). It searches the graph with a depth-first
walk that copies its visited set at every branch (`_rebac_dfs`). Both
backtrack. Neither has a termination bound except fuel, and a
fuel-zero result denies silently.

This runs on two hot paths, not one. `tw_domain.hpp` routes every
planner goal binding through `check_expr`, so a backtracking planner
calls a backtracking evaluator. `rfd/0092` then puts the same
evaluator in front of a `libriscv` guest host call, which costs 3ns.

## Decision

Compile, the way `google/re2` compiles. Treat a relation expression as
a regular path query over edge labels. Build the automaton once, per
named definition, with a Thompson construction. Then evaluate by
simulating that automaton against the graph, as a product of automaton
state and graph node. No backtracking runs, and no visited set gets
copied.

Take the algorithm, not the library. `re2` matches bytes. This
alphabet is relation labels.

Build the deterministic automaton lazily, and hold it in a bounded
cache. Flush that cache when it fills, exactly as `re2` does. This is
the mechanism behind `rfd/0092`'s resolution-equivalence theorem, and
it retires the `p_fuel` parameter, because termination becomes
structural.

Two constructs resist this. Zanzibar already answers the first one.
`difference` is non-monotonic, so follow Leopard's rule: denormalize
the monotone fragment only, and evaluate exclusion at query time over
the compiled sets.

The second stays open, and it decides feasibility.
`tuple_to_userset` moves the object as well as the subject, and nobody
checked whether it stays regular.

## References

- Construction, the two hard constructs, and Zanzibar's own answer to
  the negation problem: `DETAILS.md`
- `v-sekai-multiplayer-fabric/zone-server-h2o`,
  `thirdparty/taskweft-nif/standalone/tw_rebac.hpp` and `tw_domain.hpp`
- `google/re2`, `doc/syntax.html` and its linear-time guarantee

## Related

- `rfd/0092-rebac-gates-libriscv-guest-access`: supplies the hot path
  this compilation serves, and open questions 4 and 7 that it closes.
- `rfd/0002-taskweft-value-narrowing-primitives-and-refs`: the value
  model these expressions carry.

## Detail

{{< include DETAILS.md >}}
