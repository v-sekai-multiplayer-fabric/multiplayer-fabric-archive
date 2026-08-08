## Context and problem statement

The [loot-action core-loop slice](../0045-loot-action-core-loop-mvp-vertical-slice/README.md)
spans more than a dozen repositories in the
[`v-sekai-multiplayer-fabric`](https://github.com/v-sekai-multiplayer-fabric)
organization: a playable app, one proven core per loop concern, the
wire and transport specs, the engine fork and its assembly recipe,
the backend and infrastructure services, the verification queue, and
these docs. A reader who clones one repository sees one slice of the
whole, with no single place that says which repository owns which
concern and how they compose.

Without an index, a contributor cannot tell which repository owns a
concern, which name is current (several repositories carry a
`fabric-` prefix that an earlier short name does not), or how the
pieces relate, in particular whether the playable slice imports the
cores or carries its own copies. This record is that index, and it
has to match the repositories as they stand, not as first sketched.

## The repository map

One repository owns each concern; this record is the index. Every
repository lives in
[`v-sekai-multiplayer-fabric`](https://github.com/v-sekai-multiplayer-fabric).

### The playable slice

- [`godot-loop-slice`](https://github.com/v-sekai-multiplayer-fabric/godot-loop-slice):
  the GDScript app. It has the Hub deck and teleporter, the Field room
  with one enemy, the timed melee combo, first-touch loot contention,
  and the round trip that commits the result to SQLite. Transport is
  switchable over one text protocol: `TRANSPORT=enet` for the stable
  local default, `TRANSPORT=wt` for the WebTransport path.

The slice does not import the core repositories below. It
transcribes the proven cores into GDScript reducers (combat into
`core/combat.gd`, loot into `core/loot.gd`), with the cores' wire
parities pinning the behaviour. Progression is an inline inventory
append through the SQLite adapter (`adapters/sqlite_profiles.gd`),
not a transcribed reducer. The core repositories are the canonical,
proven reference; the slice is a standalone playable build.

### The proven loop cores

Each loop concern is a hexagon (`core/` + `ports/` + `adapters/`)
holding a dependency-free Lean core behind narrow ports, with a
matching standalone Lean workspace:

- combat: [`combat`](https://github.com/v-sekai-multiplayer-fabric/combat)
  hexagon and [`lean-combat-core`](https://github.com/v-sekai-multiplayer-fabric/lean-combat-core),
  covering combo timing, hit validation, the enemy
  spawn-invulnerability window, and damage.
- loot: [`loot`](https://github.com/v-sekai-multiplayer-fabric/loot)
  hexagon and [`lean-loot-core`](https://github.com/v-sekai-multiplayer-fabric/lean-loot-core),
  covering the seeded weighted roll and first-touch contention, with
  R128 fixed-point.
- progression: [`progression`](https://github.com/v-sekai-multiplayer-fabric/progression)
  hexagon and [`lean-progression-core`](https://github.com/v-sekai-multiplayer-fabric/lean-progression-core),
  covering profile and inventory rules: credits, the affinity gate on
  arts, valid item transitions.

### The wire, transport, and determinism specs (Lean + Plausible)

Each is a Lean 4 + Plausible workspace proving one spec; several split
out of the now-archived `lean-predictive-bvh` monorepo along the
dependency seams:

- [`lean-entity-packet`](https://github.com/v-sekai-multiplayer-fabric/lean-entity-packet):
  the 100-byte entity transform packet (int64 micrometers, no origin
  shift), with roundtrip and size proofs and a C++ differential.
- [`lean-http3-queue`](https://github.com/v-sekai-multiplayer-fabric/lean-http3-queue):
  the HTTP/3 inbound queue. Atomic push and pop keep
  `size == nodes.length` proven.
- [`lean-connection-fsm`](https://github.com/v-sekai-multiplayer-fabric/lean-connection-fsm):
  the client-server connection lifecycle, proven sound and recovering
  inside the
  [five-second transaction limit](../0050-five-second-transaction-limit/README.md).
- [`lean-spatial-oracle`](https://github.com/v-sekai-multiplayer-fabric/lean-spatial-oracle):
  the predictive spatial oracle (ghost-expansion and SAH proofs),
  emitting `predictive_bvh.h`.
- [`lean-shared-core`](https://github.com/v-sekai-multiplayer-fabric/lean-shared-core):
  the shared primitive types every cluster core builds on,
  dependency-free.
- [`lean-rebac-core`](https://github.com/v-sekai-multiplayer-fabric/lean-rebac-core):
  the NoGod / ReBAC authorization core.
- [`lean-humanoid-rom`](https://github.com/v-sekai-multiplayer-fabric/lean-humanoid-rom):
  humanoid range-of-motion and IK constraints (Kusudama, muscle and
  prismatic limits).
- [`lean-fabric-protocol`](https://github.com/v-sekai-multiplayer-fabric/lean-fabric-protocol):
  the fabric networking and SLA proofs: saturation, waypoint bounds,
  the abyssal SLA.
- [`lean-interest-mgmt`](https://github.com/v-sekai-multiplayer-fabric/lean-interest-mgmt):
  authority-interest and solve-order: who-sees-whom and solve
  sequencing.

### The engine and its assembly

- [`fabric-godot-core`](https://github.com/v-sekai-multiplayer-fabric/fabric-godot-core):
  the double-precision Godot 4.7 engine fork, each module on its own
  `feat/*` branch.
- [`fabric-godot-assembly`](https://github.com/v-sekai-multiplayer-fabric/fabric-godot-assembly):
  the `gitassembly` recipe and driver that stage and merge every
  `feat/*` branch into the assembly and tag the result. The branch
  stays local and the tag is the durable artifact.
- [`godot-images`](https://github.com/v-sekai-multiplayer-fabric/godot-images):
  the engine build factory. A GitHub Actions matrix cross-compiles the
  fork's editor and export templates for Windows and Linux with
  sccache, publishing per-platform release zips (`windows-editor.zip`,
  `linux-editor.zip`, and the matching templates) that
  `godot-loop-slice` unpacks, and a parallel job builds the rootless
  podman server images to GHCR.

### Backend and infrastructure services

- [`zone-backend`](https://github.com/v-sekai-multiplayer-fabric/zone-backend):
  the Phoenix/Elixir backend (Uro). It has identity, the zone and
  shard directory, and the loop profile commit endpoint, in a
  hexagonal `uro_loop` sub-app.
- [`zone-server-quadlet`](https://github.com/v-sekai-multiplayer-fabric/zone-server-quadlet):
  the podman quadlet for `zone-server`, the headless Godot multiplayer
  zone runtime.
- [`zone-baker`](https://github.com/v-sekai-multiplayer-fabric/zone-baker)
  and [`zone-baker-quadlet`](https://github.com/v-sekai-multiplayer-fabric/zone-baker-quadlet):
  the headless Godot asset validator and exporter, and its quadlet.
- [`cockroach-crdb-quadlet`](https://github.com/v-sekai-multiplayer-fabric/cockroach-crdb-quadlet):
  the podman quadlet for a single-node CockroachDB with mTLS.
- [`fabric-game-observability`](https://github.com/v-sekai-multiplayer-fabric/fabric-game-observability):
  the observability stack. VictoriaMetrics (8428), VictoriaLogs
  (9428), VictoriaTraces (10428), and an OTEL collector (4317 gRPC,
  4318 HTTP) run in one pod.
- [`fabric-casync-central`](https://github.com/v-sekai-multiplayer-fabric/fabric-casync-central):
  the content-addressable casync chunk store whose chunks
  `fabric-platform-central` fetches.

### Platform tooling

- [`fabric-platform-central`](https://github.com/v-sekai-multiplayer-fabric/fabric-platform-central):
  the cross-platform Elixir tray launcher (`godot-launcher-fire`), a
  self-contained Burrito binary that starts and monitors the editor on
  Linux, macOS, and Windows.

### Verification, tooling, and docs

- [`fabric-container-verify`](https://github.com/v-sekai-multiplayer-fabric/fabric-container-verify):
  the verification smokes as a systemd podman quadlet queue (monado,
  loot, combat, four-player), gating the cores against their golden
  vectors.
- [`vsekai-godot-mcp`](https://github.com/v-sekai-multiplayer-fabric/vsekai-godot-mcp):
  the MCP server that drives the Godot editor and deployed builds
  from an MCP client.
- [`xr-grid`](https://github.com/v-sekai-multiplayer-fabric/xr-grid):
  the VR interaction tool.
- [`multiplayer-fabric-manuals`](https://github.com/v-sekai-multiplayer-fabric/multiplayer-fabric-manuals):
  these decisions, the changelog, and the reference docs, with the
  `tropes` house-style check run as a pre-commit hook.

## Downsides

- An index drifts: a repository renamed or re-scoped leaves the map
  stale until someone updates it, and the map carries no automated
  check that its links resolve.
- Listing the cores apart from the slice that transcribes them can
  read as if the slice imports them. It does not, and the text says
  so, but the two copies stay in step only as long as the wire-parity
  vectors are checked.

## Rejected alternatives

- A single monorepo for the slice: one tree is easier to read at once
  but couples every concern's history and CI. One repository per
  concern keeps a change to one concern in one repository, and
  `lean-predictive-bvh` was split apart for exactly that reason.
- Wiring the cores into the slice as submodules or packages: the
  slice transcribes the proven cores into GDScript instead, so a core
  proves itself in Lean while the playable build stays a standalone
  Godot project. The wire-parity vectors, not a build dependency,
  keep the two honest.

## Confirmation

The playable-loop smoke runs the slice end to end: four Godot clients
through the Hub-to-Field-to-Loot round, exactly one grant, and the
SQLite profile commit, passing on 2026-06-29.
`fabric-container-verify` runs the same smokes (loot, combat,
four-player, monado) as a quadlet queue against the cores' golden
vectors.
