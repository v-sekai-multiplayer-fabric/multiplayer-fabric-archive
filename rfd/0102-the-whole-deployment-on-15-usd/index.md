---
title: "RFD 0102: The whole deployment on 15 USD, including the parts rfd/0100 omitted"
rfd: "0102"
state: discussion
scope: zone-server-h2o, zone-backend, aria-storage
---

## Problem

`rfd/0100` gives a topology of one machine, a volume, and a zone
server, then prices it as if that were the deployment.

It is not. That machine cannot serve a game. There is no way to upload
an avatar or a map, no content delivery, and no relational store. The
server cannot even load a default map, because nothing puts one where
it can reach it.

`rfd/0062` already owns the capability inventory, and `rfd/0100` did
not consult it.

## The parts that were missing

`zone-backend`, which is Uro, is a Phoenix and Elixir service for
identity, the zone directory, and the planner. It runs on
**CockroachDB**, with separate DML and DDL roles over mTLS client
certificates, per `rfd/0006`.

`aria-storage` is the Elixir casync and desync library that
`zone-backend` already uses.

Neither appears in `rfd/0100`. Nor does any relational store, and
`rfd/0095` depends on Uro for `/acl/check`, `/auth/script_key` and
`/storage/manifest`.

## Decision

    Fly machine 1, shared-cpu-1x 256 MB, iad          2.02 USD
      |- 1 GB volume, ssd engine                      0.15 USD
      |- fdbserver, single process
      |- zone-server-h2o, one zone, ZONE_TICK_HZ 64
      |- default map baked into the container image   0 USD
      +- UDP 7443 on fly-global-services -> clients

    Fly machine 2, shared-cpu-1x 256 MB, iad          2.02 USD
      |- zone-backend (Uro): identity, zone directory,
      |    planner, /acl/check, /storage/manifest
      +- aria-storage: casync chunking on publish

      +- PostgreSQL, WAL to Tigris (see rfd/0103)   +1.45 USD

    Tigris object storage                             0 USD
      |- casync chunks and .caibx indexes             5 GB free
      |- fdbbackup target (disaster recovery)
      +- auto-replicated to Fly edge: this IS the CDN

FoundationDB holds zone state. CockroachDB holds identity and the
directory. `rfd/0075` chose FoundationDB over CockroachDB **for zone
state**, and that scope never removed Uro's own relational store.

CockroachDB does not run on Fly here. Fly charges about 5 USD per GB of
RAM per month. A `shared-cpu-1x` at 2 GB is therefore 11.11 USD, which
is 74 percent of the budget for the database alone. That leaves
nothing.

CockroachDB Cloud Basic is on the blocklist, so the managed free tier
is not available either. `rfd/0103` moves Uro to PostgreSQL for that
reason, and it carries the measurements.

## Assets never pass through the zone server

Tigris charges 0.02 USD per GB stored and **0 USD egress**, and it
replicates to Fly edge regions automatically. That replication is the
CDN.

So a client fetches avatars and maps straight from Tigris. Those bytes
never cross the zone server, and they never count against `rfd/0100`'s
256 kbps cap.

Routing assets through the zone server would put them on Fly egress at
0.02 USD per GB. One 20 MB avatar fetched by 39 clients is 0.78 GB,
which is 6.8 client-hours spent on a single asset.

## Cost

| Topology                          | Fixed USD | Egress USD | GB      | Concurrent at 4 h/day |
| --------------------------------- | --------- | ---------- | ------- | --------------------- |
| `rfd/0100`, zone only, incomplete | 2.17      | 12.83      | 642     | 46.4                  |
| **Uro and PostgreSQL colocated**  | **4.34**  | **10.66**  | **533** | **38.6**              |
| Uro and PostgreSQL separate       | 6.36      | 8.64       | 432     | 31.2                  |

Take the colocated form. Phoenix and PostgreSQL on one 256 MB machine
is tight, and it is the difference between 38.6 and 31.2 concurrent
users. Measure the memory before committing, because 256 MB is the
constraint that `rfd/0096` measured as 212188 kB usable.

The complete deployment costs 7.8 concurrent users against `rfd/0100`'s
number. Without those services there is no game.

## The default map ships in the image

A zone cannot bootstrap from an empty object store. The default map
goes into the container image at build time.

That costs nothing and needs no upload path. It also removes a circular
dependency, where the server needs content to start and content needs a
running server to upload.

User-generated content uses the object store. The default map does not.

## Upload path

A client uploads through Uro, never through the zone server.

1. Client asks Uro to publish, and Uro checks ReBAC at `/acl/check`.
2. `aria-storage` chunks the asset and writes only chunks Tigris lacks.
3. Uro returns the `.caibx` index id.
4. `ZONE_OBJ_PUT` records that id, gated on a delegation edge per
   `rfd/0095`.

The zone server records an identifier. It never carries the bytes.

## What is unbuilt

`ZONE_OBJ_GET` and `ZONE_OBJ_PUT` return `-ENOSYS`. See
`zone-server-h2o` issues 32, 33 and 34.

`modules/multiplayer_fabric_asset` holds the C++ casync client and is
not extracted from the Godot build.

`zone-backend` is not deployed on Fly, so its 2.02 USD line is an
estimate rather than a measurement.

Uro's move from CockroachDB to PostgreSQL is not done. `rfd/0103`
decides it, and Uro's migrations are not yet audited for
CockroachDB-specific SQL.

## Which record owns which fact

This RFD owns the deployment topology and its cost. It restates nothing
else, because restating is what caused the error above.

| Fact                                 | Owner       |
| ------------------------------------ | ----------- |
| Repository and capability inventory  | `rfd/0062`  |
| CockroachDB and mTLS role separation | `rfd/0006`  |
| FoundationDB for zone state          | `rfd/0075`  |
| Fly as the deployment target         | `rfd/0089`  |
| Uro packaging                        | `rfd/0090`  |
| Guest classes and the two stores     | `rfd/0095`  |
| Per-client bandwidth cap             | `rfd/0100`  |
| Wire format and compression          | `rfd/0101`  |
| Deployment topology and cost         | this record |

## A correction

An earlier revision of this RFD said Uro runs on PostgreSQL. It does
not. It runs on CockroachDB.

The error came from reading `config/runtime.exs`, seeing
`adapter: Ecto.Adapters.Postgres`, and concluding the server from the
adapter. CockroachDB speaks the PostgreSQL wire protocol, so that
adapter is what an Elixir application uses to reach it.

The same file names `CRDB_SNI`, `CRDB_CA_CERT`, `CRDB_CLIENT_CERT` and
`CRDB_ADMIN_CERT`, which `rfd/0006` already documents. The evidence was
present and was not read.

## Sources

- [Fly pricing](https://fly.io/docs/about/pricing/), about 5 USD per GB of RAM per month
- [Tigris pricing](https://www.tigrisdata.com/pricing/), 0.02 USD per GB, 0 egress, 5 GB free
- `zone-backend` `config/runtime.exs`, for the `CRDB_*` variables
