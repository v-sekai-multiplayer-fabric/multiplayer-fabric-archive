## Context

The production zone server (`zone-server`, deployed as `multiplayer-fabric-zone`
on Fly.io) is a boot scaffold today — OpenTelemetry init only, no WebTransport
listener, no game logic (`project/main.gd`'s own `TODO(cycle-5)` comment). The
real entity/simulation engine, `FabricZone`/`FabricZoneJournal`/`FabricMMOGZone`,
exists as a working Godot C++ module in `V-Sekai-fire/multiplayer-fabric-build`
(`godot/modules/multiplayer_fabric/`) but has never been wired into a deployed
zone server. Separately, `weftspun/h2o-bench-tpcc` designed (RFD 0002,
`discussion` state, never implemented) a `libh2o` + FoundationDB "zonefabric"
scenario modeling the same hub/instanced-zone shape — entity authority via a
Hilbert-curve partition, ghost/AOI interest management — independently of this
org's actual zone-server work.

## Consequences

- The Godot zone-server deployment (`zone-server` repo, Fly app
  `multiplayer-fabric-zone`) is retired once `zone-server-h2o` passes
  equivalent verification, in favor of the new implementation.
- `zone-backend` (Uro) is unaffected: it still only does identity, zone
  directory, asset storage, and ReBAC, and still never sees per-entity data
  in the live session.
- CockroachDB's scope stays account-level (identity, zones directory,
  avatars). The one exception under discussion — inventory/profile commits,
  previously headed toward CockroachDB via the progression hexagon's
  SQLite "commit valve" — targets FDB instead, as the single write path for
  everything the zone server owns.
- The related, previously-unimplemented `weftspun/h2o-bench-tpcc` RFDs this
  decision carries forward are filed alongside this entry, dated the same day:
  zonefabric scaling, actor-lite worker pool, FDB selection, verification
  strategy, binary value encoding, async FDB callback chain, zstd
  compression, slotmap entity storage, macaroon/XDP security, feature
  ablation, and the PERT critical path.

## Ported RFDs

The following RFDs in this repo carry forward `weftspun/h2o-bench-tpcc`'s
zonefabric design, filed the same day as this decision:

- `rfd/0082-zonefabric-scaling`
- `rfd/0072-actor-lite-worker-pool`
- `rfd/0075-fdb-over-cockroachdb-for-zone-state`
- `rfd/0081-three-layer-verification-strategy`
- `rfd/0074-binary-value-encoding-for-fdb`
- `rfd/0073-async-fdb-callback-chain`
- `rfd/0084-zstd-compression-for-zone-state`
- `rfd/0080-slotmap-entity-storage`
- `rfd/0076-macaroon-xdp-security`
- `rfd/0078-plausible-witness-dag-feature-ablation`
- `rfd/0077-pert-critical-path-zonefabric`

## Related

- `v-sekai-multiplayer-fabric/zone-server-h2o`
- `V-Sekai-fire/multiplayer-fabric-build`,
  `godot/modules/multiplayer_fabric/`
- `sinew-mocap/solve`, `lean-humanoid-rom`, `swing-twist-kusudama`
- `lean-entity-packet`, `lean-rebac-core`
