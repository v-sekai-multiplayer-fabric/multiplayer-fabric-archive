---
title: "RFD 0103: Uro on ecto_foundationdb"
rfd: "0103"
state: discussion
scope: zone-backend, aria-storage
---

## Decision

Uro stores its data through **`ecto_foundationdb`**, in the
FoundationDB cluster `rfd/0102` already deploys. No second datastore,
and no machine for a database.

## Cost

| Option                                       | Fixed USD | Concurrent at 4 h/day |
| -------------------------------------------- | --------- | --------------------- |
| **`ecto_foundationdb`, no database machine** | **4.19**  | **39.1**              |
| PostgreSQL colocated at 512 MB               | 5.64      | 33.9                  |
| PostgreSQL on its own machine                | 6.36      | 31.2                  |
| Self-hosted CockroachDB, 1 GB                | 10.26     | 17.1                  |
| Neon, paid, always on at 1 CU                | 81.59     | over budget           |

The database is free here, because `rfd/0102` already runs
`fdbserver` for zone state.

## Latency, measured

| Engine                                     | Point read   |
| ------------------------------------------ | ------------ |
| PostgreSQL 16                              | 0.084 ms     |
| **FoundationDB, new transaction per read** | **0.405 ms** |
| **FoundationDB, inside one transaction**   | **0.157 ms** |
| DuckDB                                     | 0.919 ms     |

0.405 ms is the floor for a `Repo.get`, and 0.157 ms is the floor
inside `Repo.transactional/2`. That is 4.8 and 1.9 times PostgreSQL,
in the same order rather than a different one.

## What this gives up

`ecto_foundationdb` is a layer over a key-value store. Its own
documentation states the limit directly. A query is valid only when one
Get or one GetRange satisfies it.

- No joins.
- No `or`. "A query with an `or` condition is not possible."
- No aggregates in the database. Filtering, aggregation and
  grouping "must be done by your Elixir code".
- One Between clause per query, on an indexed field, and index
  field order decides which ranges work.
- Migrations cannot rename tables or fields, delete fields, drop
  indexes, or roll back. The standard `mix ecto.migrate` tasks do not
  apply.
- Tenants are mandatory. Omitting one raises at run time.
- Transactions run under FoundationDB's 5 second limit, may
  re-execute on conflict, and must have no side effects. So no PubSub
  publish inside one.

## The gate

This decision depends on Uro's queries fitting one Get or one
GetRange. That is not audited, and it is the one thing that can
overturn this RFD.

Audit `zone-backend`'s queries first. A join or an `or` in a hot path
means restructuring the schema, not configuring an adapter.

The migration constraints matter as much. Uro has existing Ecto
migrations, and this adapter cannot roll back or drop an index.

## Why not the others

**FRL.** Its own ADR 0003 accepts costs this avoids entirely:

- No crash isolation, permanently
- A JDK and a Rust toolchain required to `mix compile`
- Every call on Rustler's `DirtyIo`
- No per-call timeout

`ecto_foundationdb` uses `erlfdb`, so there is no JVM.

A separate measurement of FRL at 45.259 ms per point read is withdrawn.
It went through JDBC, and ADR 0003 removed that transport in v0.2. FRL
is not ruled out on speed here.

**PostgreSQL.** Faster per point read at 0.084 ms, and it costs a
machine and a second datastore. It stays the fallback if the query
audit fails.

**DuckDB.** 0.919 ms, one writing process, and a rewrite off Ecto. It
stays for analytics and telemetry writing ETNF Parquet.

**Neon.** Its free plan forces scale-to-zero, suspending after 5
minutes idle. A login cannot cold start. Paid compute at 0.106 USD per
CU-hour is about 77 USD for one CU held for a month.

**CockroachDB.** `rfd/0006` chose it, Cloud Basic is on the blocklist,
and self-hosting at 2 GB is 15.45 USD before any bandwidth.

## Recovery

`rfd/0100` already sends `fdbbackup` to Tigris for disaster recovery,
and the volume carries live data. Uro's data joins that path rather
than adding one.

## Open

Uro's queries are not audited against the one-Get-or-GetRange rule.

`ecto_foundationdb`'s tenant model is not mapped onto Uro's schema.

Latency above is raw FoundationDB through the C client. The adapter
adds Elixir and `erlfdb` on top, and that overhead is unmeasured.

## Sources

- [ecto_foundationdb](https://github.com/foundationdb-beam/ecto_foundationdb), v0.7.6, Apache-2.0
- [Ecto.Adapters.FoundationDB docs](https://ecto-foundationdb.hexdocs.pm/), for the query and migration limits
- Measurements above, `pointget.c` and the PostgreSQL and DuckDB probes
- `rfd/0102` for the deployment budget, `rfd/0006` for CockroachDB
