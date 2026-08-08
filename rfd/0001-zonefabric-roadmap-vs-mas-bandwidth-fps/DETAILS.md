## Summary

This RFD sequences the remaining `zone-server-h2o` zonefabric work by the
PERT critical path `decisions/20260806-pert-critical-path-zonefabric.md`
already established. It records a done, compiled-but-not-wired, or
not-started status for each ported RFD task, so a future contributor reads
one table instead of re-deriving it. It records a settled reference note
as context, not an open question: how this architecture compares to
Glenn Fiedler's `mas-bandwidth/fps` article-series code.

It also records two encoding decisions for CastSpell's sandbox boundary.
One is the same bitpacked struct format `RFD 0010` already chose for
runtime data. The other is CBOR-LD for package manifests. It also records
a licensing preference, Apache-licensed eBPF tooling, for later,
currently-deferred work.

## Background

`decisions/20260806-zone-server-h2o-replaces-godot-fabriczone.md` recorded
the decision to build `zone-server-h2o` and ported the zonefabric RFDs from
`weftspun/h2o-bench-tpcc`. That decision left two things unstated.

First, which of the now-ported RFD tasks the repo already finished today,
versus only scaffolded or unit-tested in isolation. Second, how this
architecture's scaling reasoning compares against `mas-bandwidth/fps`.
That project is the companion code for a public article series on scaling
a first-person shooter's server architecture. Recording this comparison
means future capacity planning starts from a reference point, not a blank
page.

## Motivation

Recording the status table and the reference notes here avoids three
failure modes. A future session re-derives the PERT order from
`decisions/20260806-pert-critical-path-zonefabric.md` from scratch, instead
of reading a checkable status table. A reader mistakes the `fps` kernel-bypass
comparison for a missing requirement, instead of a deliberately deferred
optimization. A future eBPF or effect-scripting task starts a new design
from zero, instead of reusing tooling the project already built and
accepted.

One decision driver runs through every item 4 choice explicitly: keep
`zone-server-h2o` itself, the deployed process, down to the fewest
components possible. This is why the CastSpell FFI reuses `RFD 0010`'s
existing bitpacked struct instead of adding FlatBuffers or protobuf. This
is also why the manifest's JSON-LD processing runs in an offline
authoring tool and never inside the deployed server. It is also why the
server-side manifest mechanism stays a small, purpose-built ELF section,
not an imported convenience API. Read every encoding and tooling choice
below against this driver first.

## Proposal

Adopt the existing PERT critical path as the build order for the
remaining zonefabric work. In order: task A, task B, task C, task F,
task I, task M. Record the current status of every task in the table
below.

| Task | What it is                                | Status                                                  |
| ---- | ----------------------------------------- | ------------------------------------------------------- |
| A    | Binary value encoding                     | Done                                                    |
| B    | FDB keyspace and async callbacks          | Done                                                    |
| C    | Actor-lite worker pool                    | Done                                                    |
| F    | ZoneTick                                  | Done                                                    |
| D    | Slotmap entity storage                    | Not started                                             |
| G    | Zone-state blob persistence               | Not started                                             |
| H    | GhostRelevance (AOI query)                | Not started                                             |
| I    | CastSpell (effect and fanout)             | Not started                                             |
| J    | EntityMigration                           | Not started                                             |
| K    | ZoneSplit                                 | Not started                                             |
| L    | Macaroon plus XDP security                | Not started                                             |
| M    | Feature ablation                          | Not started                                             |
| N, O | Benchmark harness and scaling measurement | Not started                                             |
| N/A  | ReBAC (`src/gen/rebac.c`)                 | Compiled, unit-tested, not called from the request path |
| N/A  | Avatar IK (`src/gen/sinew_align.c`)       | Compiled, unit-tested, not called from the tick loop    |
| N/A  | Physics (`src/physics/mj_physics.c`)      | Compiled, unit-tested, not called from the tick loop    |

`src/zf_kv.c` covers only the `zf/zone/` and `zf/entity/` keys today. The
`zf/zone_state/`, `zf/effect/`, and `zf/fanout/` keys `RFD 0002
(zonefabric-scaling)` also describes remain unimplemented. TLS in `main.c`
still passes `NULL` for both the certificate and the key, so the server
accepts no client authentication yet.

`docs/0001-defer-nogod-gossip-authority.md`, in `zone-server-h2o` itself,
still describes the zone ID as a hardcoded value. Commit `a36bc8a` already
made the zone ID a required command-line flag, and nobody updated that doc
to match.

### fps comparison, as settled context

`weftspun/h2o-bench-tpcc`'s `rfd/0002-zonefabric-scaling.md` already
compares this design's shape against `mas-bandwidth/fps`. Four gaps
stand against it. It has no delta compression, no client prediction or
rollback, no real-time multicast or snapshot delivery path, and no
kernel-bypass packet ingest layer.

The kernel-bypass gap does not mean this design lacks UDP.
`decisions/20260501-webtransport-over-quic-for-game-traffic.md` already
puts WebTransport, and therefore QUIC and UDP, under all client-server game
traffic. The gap names only the absence of an ingest-layer optimization.
That optimization intercepts packets ahead of the normal kernel socket
path, the way `mas-bandwidth/fps` does with its own kernel tooling. The
project already filed that optimization as a future item, not a blocker
for the current milestone.

Every comparison in this RFD, and any future one, states the concept
`mas-bandwidth/fps` shows in this project's own words. A phrase like "a
hub server plus per-zone instances" is an example. So is "a shallow
cross-server state cache." No comparison copies `mas-bandwidth/fps`'s
code, comments, file layout, or prose. That project sits outside this
org. The project treats it as reference material only, the same way it
would treat any other unaffiliated open-source project it studies for
scaling ideas.

## Recommendation and next steps

List these follow-ups in PERT order.

1. Wire the three orphaned modules into the real request path. This work
   costs little and unblocks no other task, but it removes the ambiguity
   around otherwise-unused code. Call `rebac_check` from `wt_session` or
   from a real handler under `src/handlers/`. Drive `mj_physics_step` from
   `zf_zonetick.c` for entities that carry physics. Call `sinew_align` from
   an IK pipeline the tick loop calls.
2. Land task D, slotmap entity storage. The PERT slack analysis marks this
   task as near-critical, so build it early.
3. Land task G, the zone-state blob (a batched slotmap plus zstd
   persistence design), and task H, GhostRelevance, an AOI query. Neither
   exists in `zf_kv.c` today.
4. Land task I, CastSpell. This is the single riskiest task on the
   critical path: it carries the highest variance and three upstream
   dependencies. Stub the fanout radius scan first, per the PERT
   risk-mitigation note, and optimize it once the rest of the loop works.

   CastSpell's effect step runs as a sandboxed `libriscv` ELF package,
   reusing `decisions/20260611-generated-behavior-sandboxed-riscv.md`'s
   existing sandboxed-execution work rather than a new execution engine.
   `rfd/0003-castspell-sandbox-package-and-manifest-encoding/index.md` is the
   full design. It is a single `.elf` file per package, an embedded
   CBOR-LD manifest, and one runtime FFI boundary. That boundary is the
   same bitpacked struct format `RFD 0010` already uses for the zone
   tick.

5. Type the CastSpell sandbox boundary, and extend `RFD 0010
(binary-value-encoding)`, with a primitive-versus-reference split. Reuse
   the value-type vocabulary the `gltf_interactivity` specification, vendored
   under `taskweft/thirdparty/gltf_interactivity/`, already defines, rather
   than invent a new one.

   `rfd/0005-gltf-interactivity-value-type-taxonomy-correction/index.md` records
   that specification's real taxonomy: primitive types, `ref`, and a
   third `custom` category the specification defers to extensions.
   `zone-server-h2o` implements only the first two.

   Map `ref` onto the slotmap's generational entity handles from `RFD 0017
(slotmap-entity-storage)`, and map the primitive types onto plain
   numeric fields such as position, velocity, and health. Represent this
   split inside the bitpacked struct layout item 4 introduces. A
   primitive field holds its value inline. A `ref` field holds a slotmap
   handle, at a fixed offset the manifest's ABI version field pins down.
   This split gives the sandbox FFI boundary, and any future CastSpell
   parameter encoding, a clean value-versus-handle distinction. The
   project already uses that distinction elsewhere, instead of building a
   new one from scratch.

6. Scope how much of `libriscv`/`godot-sandbox`'s own API surface
   CastSpell effects actually need, rather than assume the whole thing.
   `rfd/0004-castspell-libgodot-sandbox-runtime-scope/index.md` is the full
   design. `godot-sandbox`'s own API is mostly a thin remote-call proxy
   into a live Godot process, not local math. This project embeds a
   real, headless `libgodot` instance per zone instead of reimplementing
   that API. A real spike already booted this configuration inside
   `libriscv`'s actual sandbox. Performance measurement against the
   10Hz/200-entity/many-zones budget stays the open gate.

7. Defer these tasks, per the PERT slack analysis: task E, zstd (11.1 days
   of slack), task L, Macaroon plus XDP (7.5 days of slack, and this task
   can run in parallel with other work), task J, EntityMigration (stub this
   as "stay in the birth zone" at first), and task K, ZoneSplit.

   Record one licensing note here, for task L and for the kernel-bypass
   ingest layer named above. Both stay deferred, not blocking. Linux's
   native `libbpf`/`libxdp` stack sits close to the GPL-licensed kernel
   tree, since several kernel eBPF helpers need a GPL-compatible program
   license before the kernel loads them.

   `iovisor/ubpf` gives an Apache-2.0 alternative: a userspace eBPF virtual
   machine with an interpreter and a JIT compiler for x86-64 and ARM64.
   `ubpf` carries no built-in verifier of its own, so a future adopter
   pairs it with an external verifier such as PREVAIL. `ubpf` carries no
   dependency on the Linux kernel GPL boundary, since it runs entirely in
   userspace. Prefer embedding `ubpf` directly over routing through
   `microsoft/ebpf-for-windows`, since that project only wraps `ubpf` for
   Windows hosts, and this project targets Linux and `libh2o`. Treat this
   as a licensing note to carry forward, not a task to start now.

8. Fix the stale claim in `docs/0001-defer-nogod-gossip-authority.md`, in
   `zone-server-h2o`. That doc still describes the zone ID as hardcoded.
   Commit `a36bc8a` and the current `main.c` already contradict that claim.
9. Replace the stale, TPC-C-flavored `test/verification/README.md` with
   zonefabric-specific invariants: entity migration, ghost consistency, and
   journal replay. Add these as those features land, per task M.
10. Wire real TLS certificate and key material before any real
    client-handshake test runs. `main.c` still passes `NULL` for both today.

## Open questions and verification

A future session checks this RFD's claims still hold this way. Re-run the
local equivalent of `zone-server-h2o`'s `real-build.yml`. Grep `src/` for
calls to `rebac_check`, `mj_physics_step`, and `sinew_align`. Zero calls
exist outside the unit tests today, so this check reveals whether the
wiring work in item 1 above happened. Check `zf_kv.h`'s own scoping
comment, and confirm whether the `zf/zone_state/`, `zf/effect/`, and
`zf/fanout/` keys exist yet.

Resolved: the sandboxed-CastSpell approach in item 4 needs its own
follow-up RFD. That is because the scope differs between a client-side
sandbox and a server-side one running under load. That follow-up is
`rfd/0003-castspell-sandbox-package-and-manifest-encoding/index.md`. Its own
open questions, on `jsonld-cpp`'s maintenance risk, the authoring
tool's sandboxing, and the `QCBOR`-versus-`zcbor` pick, live there, not
here.

Resolved, item 6: the scope of `libriscv`/`godot-sandbox`'s API surface
CastSpell effects need, and the choice to embed `libgodot` instead of
reimplementing that surface. That decision, and its own open
performance-measurement gate (part 2 of the spike), live in
`rfd/0004-castspell-libgodot-sandbox-runtime-scope/index.md`.
