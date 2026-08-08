---
title: "RFD 0004: CastSpell's sandboxed runtime: embed libgodot, not godot-sandbox's narrow API"
rfd: "0004"
state: discussion
scope: zone-server-h2o CastSpell
---

## Problem

CastSpell needed a sandboxed runtime for each zone. The existing
`godot-sandbox` project exposes only a narrow, syscall-proxied API.
Reimplementing that narrow API inside `libriscv` risked missing needed
engine functionality. No RFD proved that a real `libgodot` instance
could run inside a `libriscv` sandbox, or measured its performance
against the project's 10Hz/200-entity/many-zones budget.

## Decision

Embed a real, headless `libgodot` instance per zone inside each
CastSpell `libriscv` sandbox `Machine`, instead of reimplementing
`godot-sandbox`'s narrow, syscall-proxied API. Build from the pinned
`fabric-godot-core` tag, headless, `arch=rv64`, `threads=no`,
`modules/sandbox` disabled. A real spike already booted this
configuration inside `libriscv`'s actual sandbox and produced real
script output over five `iteration()` calls, needing a musl guest libc
and seven upstream-worthy `libriscv` fixes. Performance measurement
against the 10Hz/200-entity/many-zones budget stays the open gate
before real implementation work starts. `DETAILS.md` has the full
build configuration, the maintainer feedback, and the ranked fallback
options.

## References

- Full build configuration, spike detail, fallbacks: `DETAILS.md`
- Spike script and `libriscv` patch:
  `v-sekai-multiplayer-fabric/godot-riscv-spike`

## Related

`rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps/index.md` (item 6)

## Detail

{{< include DETAILS.md >}}
