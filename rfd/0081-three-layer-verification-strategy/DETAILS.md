## Layers

| Layer           | Tool                  | Scope          | What it proves                                               |
| --------------- | --------------------- | -------------- | ------------------------------------------------------------ |
| C invariants    | CBMC                  | Implementation | SPSC ring FIFO, bounds, head-tail, NURand range              |
| Specification   | Lean 4                | Design         | SPSC linearizability, push/pop preserve bounds               |
| TPC-C semantics | plausible-witness-dag | Runtime        | NewOrder atomicity, Delivery correctness, Stock non-negative |

## Why three layers

CBMC proves the C code is correct for bounded inputs but cannot reason
about TPC-C semantics. Lean 4 proves the specification is sound but does
not verify the C implementation. plausible-witness-dag searches for
runtime invariant violations via HTTP but cannot prove absence of bugs.

Together they cover the gap: CBMC catches implementation bugs, Lean 4
catches specification bugs, plausible-witness-dag catches integration
bugs.

## CBMC harnesses

- `test/cbmc/spsc_harness.c` — FIFO ordering, overflow, underflow, head-tail invariant
- `test/cbmc/nurand_harness.c` — NURand result in [x, y]
- `test/cbmc/random_harness.c` — get_random_number in [0, max]

## Lean 4 modules

- `TpccVerification/Spsc.lean` — SPSC ring buffer specification with proofs: `init_safe`, `push_safe`, `pop_safe`
- `TpccVerification/Basic.lean` — TPC-C invariant predicates for plausible-witness-dag

## plausible-witness-dag integration

Depends on `fire/plausible-witness-dag` as a Lake dependency. The
`resolve` function takes a candidate predicate and a deterministic
readback, escalating through L0/L1/L2 until it finds a witness or
proves none exists.

Escalation ladder: L0 (10 txns, W=1), L1 (100 txns, W=5), L2 (1000 txns, W=20).
