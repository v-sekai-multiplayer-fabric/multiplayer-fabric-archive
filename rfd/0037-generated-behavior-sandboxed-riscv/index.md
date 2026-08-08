---
title: "RFD 0037: Generated behavior runs as sandboxed RISC-V"
rfd: "0037"
state: published
scope: combat behavior_source port implementation
---

## Problem

A generator can produce enemy and ability behavior. Running generated
logic in-process risks the whole instance. The combat hexagon's
`behavior_source` port had no safe implementation for this risk.

## Decision

Enemy and ability behavior may be generated, and running generated
logic in-process risks the whole instance. The combat hexagon's
`behavior_source` port needs a safe implementation. The project runs
generated enemy and art behavior as sandboxed RISC-V programs through
`feat/sandbox`, implementing the combat `behavior_source` port,
because the sandbox contains a misbehaving program rather than letting
it take down the instance. This is the runtime guarantee under the
bounded, declarative vocabulary the AI emits. Generated logic is
isolated, so a bad program degrades one entity rather than the
instance. Behavior is an adapter behind a port, so a fixture adapter
replays scripted intents for CI, and the same port accepts
hand-written and generated behavior. A misbehaving generated program
stays contained without taking down the instance, and the fixture
adapter reproduces scripted intents with no sandbox.

## References

- Original record:
  `decisions/20260611-generated-behavior-sandboxed-riscv.md`
- `feat/sandbox`

## Related

- `rfd/0040-hexagon-combat-core`: the `behavior_source` port this
  sandbox implements.
