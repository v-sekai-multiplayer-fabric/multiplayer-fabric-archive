---
title: "RFD 0100: 256 kbps per client, one machine, 47 concurrent"
rfd: "0100"
state: discussion
scope: zone-server-h2o, zone-guest-godot
---

## Decision

**256 kbps per client. One Fly machine plus a 1 GB volume. 46
concurrent at peak.**

The budget is 15 USD per month. Bandwidth is the only binding
constraint, so spend the budget on bandwidth and not on machines.

## Topology

    1 Fly machine, shared-cpu-1x, 256 MB, region iad
      |- 1 GB volume, ssd storage engine (live data)
      |- volume snapshots (local rollback)
      |- fdbbackup -> S3 object store (disaster recovery)
      |- fdbserver, single process
      |- zone-server-h2o, one zone, ZONE_TICK_HZ 64
      |- UDP 7443 bound to fly-global-services -> WebTransport clients
      +- no TCP service: no HTTP surface remains

One process serves one zone. To add zones, add machines and join them
over 6PN, where `<app>.internal` resolves every machine. `rfd/0096`
measured that path at 880 us median between two machines in `iad`.

## Machine cost against concurrency

A `shared-cpu-1x` 256 MB machine is 2.02 USD per month. Egress is 0.02
USD per GB in North America and Europe. At 256 kbps a client uses
0.1152 GB per hour.

| Machines | Machines USD | Egress USD | GB  | Client-hours | 24/7 | 4 h/day peak |
| -------- | ------------ | ---------- | --- | ------------ | ---- | ------------ |
| **1**    | 2.02         | 12.98      | 649 | 5634         | 7.8  | **46.9**     |
| 2        | 4.04         | 10.96      | 548 | 4757         | 6.6  | 39.6         |
| 3        | 6.06         | 8.94       | 447 | 3880         | 5.4  | 32.3         |
| 4        | 8.08         | 6.92       | 346 | 3003         | 4.2  | 25.0         |

Every added machine costs about 7 concurrent users at peak. So run one
machine until a second zone is genuinely needed.

Asia Pacific doubles the egress price, and Africa and India multiply it
by six.

## Durability: three layers, each doing one job

A Fly volume is 0.15 USD per GB per month, and snapshots are 0.08 USD
per GB with the first 10 GB free.

So 1 GB of durable storage costs 0.15 USD, which is 0.5 concurrent
users at peak. Three-machine RAM redundancy costs 4.04 USD, which is 14
concurrent users. Durability through a volume is 28 times cheaper.

They also protect different failures. A volume survives a restart and a
crash. RAM redundancy survives the loss of one machine and does NOT
survive an app restart, because every RAM disk goes at once.

The ssd engine costs nothing in commit latency. Measured on the same
probe as `rfd/0097`:

| Engine | Commit    | Versionstamp append |
| ------ | --------- | ------------------- |
| memory | 2975.3 us | 2521.9 us           |
| ssd    | 2396.6 us | 2952.7 us           |

ssd commits FASTER. FoundationDB's memory engine still writes its
transaction log to disk for durability, so the storage engine changes
the read path and not the commit path. The memory engine also holds
every key in RAM, which competes with the zone server for 212188 kB.

The three layers do different jobs, and none replaces another:

| Layer                     | Job                             | Cost                      |
| ------------------------- | ------------------------------- | ------------------------- |
| ssd engine on the volume  | live data                       | 0.15 USD per GB           |
| Fly volume snapshots      | fast local rollback             | free below 10 GB          |
| FoundationDB backup to S3 | disaster recovery, off platform | egress at 0.02 USD per GB |

A snapshot is local to Fly and periodic. It restores a bad deploy in
minutes, and it does not survive the loss of the region or the account.

`fdbbackup` streams mutations continuously to a blob store, so the
recovery point is seconds rather than hours, and the copy sits outside
Fly. It restores into a different cluster.

The project already runs an object store for casync, per `rfd/0095`, so
this adds a destination rather than a dependency. Mutation volume is
small, because live entity state is 20 KB per zone and the guest
key-value quota is 8 MB per zone.

RAM redundancy is not on this list. It answers availability, not
durability, and at this budget it costs 14 concurrent users.

## What binds, and what does not

| Resource                     | At 47 clients         | Ceiling     |
| ---------------------------- | --------------------- | ----------- |
| Egress                       | 649 GB per month      | **binding** |
| CPU, zstd compression        | 3.2 percent of a core | not binding |
| Memory, per-client baselines | 2.1 MB                | not binding |

`rfd/0101` measured compression at 68 us per client per tick. At 10 Hz
that is 0.068 percent of a core per client.

Reuse one `ZSTD_CCtx` across clients rather than holding one each.
Compression runs sequentially inside the tick, and a per-client context
would cost roughly 1.3 MB each, which is 61 MB at 47 clients.

## How 256 kbps is met

    8 avatars, 56 entities each, zstd L3 delta at 10 Hz  = 221.9 kbps
    Opus voice, server-mixed, VOIP mode                  =  24.0 kbps
    total                                                = 245.9 kbps

## Two corrections this depends on

**Three rates, not one.** Simulation stays at `ZONE_TICK_HZ` 64. State
sends at 10 Hz. Voice sends at 50 Hz, from the 20 ms Opus packets in
`modules/speech`. `rfd/0096` through `rfd/0099` assumed one rate and
produced 10.2 Mbit per second.

**An entity is a bone.** VRM 1.0 gives 55 humanoid bones plus 1 root,
so 56 per avatar, and only the root and hips translate. `rfd/0002`'s
200 entities is 3.6 avatars, not 200 objects.

## Consequences

Interest management is required. 8 avatars fits 256 kbps. 47 does not,
so a client sees 8 of the 47 at full rate.

`modules/speech` sets `OPUS_APPLICATION_AUDIO` and never calls
`OPUS_SET_BITRATE`, so it takes the default near 64 kbps, which is 25
percent of the cap. Set the bitrate, and evaluate
`OPUS_APPLICATION_VOIP`.

Server-side voice mixing is unbuilt. Without it, voice grows with
speaker count and the cap breaks.

## Sources

- Fly pricing, for 0.02 USD per GB and 2.02 USD per machine
- [Networking for Physics Programmers, GDC 2010](https://www.gamedevs.org/uploads/networking-for-physics-programmers.pdf), Sony Bandwidth Probe
- [VRM 1.0 humanoid](https://github.com/vrm-c/vrm-specification/blob/master/specification/VRMC_vrm-1.0/humanoid.md), 55 bones
- `rfd/0096` for the 6PN measurement, `rfd/0101` for the zstd rate
