---
title: "RFD 0104: Hypothesis, what 1000 concurrent users costs"
rfd: "0104"
state: discussion
scope: zone-server-h2o, zone-backend
---

## This is a hypothesis, not a measurement

Nothing here ran at 1000 concurrent. Every number extrapolates from
`data/measurements/`, which tops out at 32 workers on one machine.

The record exists so the guess is written down, falsifiable and
attributable, rather than repeated as if measured. Each claim below
carries the test that would break it.

## The claim

**The database is not the constraint. Bandwidth is, by roughly 100
times.**

## Database side

Measured ceilings, from `throughput.parquet`:

| Host                | vcpus | Ceiling      |
| ------------------- | ----- | ------------ |
| Fly `shared-cpu-1x` | 1     | 2159.1 ops/s |
| Workstation         | 16    | 6086.5 ops/s |

`rfd/0102` estimates a user at about 10 requests per minute.

| Load                   | Required     | Fraction of one `shared-cpu-1x` |
| ---------------------- | ------------ | ------------------------------- |
| 1000 users, 10 req/min | 166.7 ops/s  | **0.08**                        |
| 1000 users, 60 req/min | 1000.0 ops/s | **0.46**                        |

So one machine carries 1000 users on the database side, with room left
over. That is the surprising half of this record.

## Bandwidth side

`rfd/0100` caps a client at 256 kbps, which is 0.1152 GB per
client-hour.

| Usage                         | GB per month | Egress at 0.02 USD per GB |
| ----------------------------- | ------------ | ------------------------- |
| 1000 concurrent, 4 h/day peak | 13824        | **276.48 USD**            |
| 1000 concurrent, 24/7         | 82944        | **1658.88 USD**           |

Against a 15 USD budget, one machine and a volume leave 642 GB. Spread
across 1000 users that is 5.6 client-hours per month, or **0.19 hours
per day**.

So 1000 concurrent users is an 18 to 110 times budget increase, and
none of it buys database capacity.

## Why each number could be wrong

**The Fly ceiling is soft.** The curve reads 2.20, 1.64, 1.11, 1.10,
then 1.23 times per doubling. A clean saturation does not rise again at
the end. So 2159.1 ops/s may be noise around a lower plateau. The
machine may also never reach saturation at all.

**1000 users is not 1000 workers.** The probe ran at most 32
concurrent transactions. A thousand idle connections consume memory and
scheduler time that this never measured.

**Contention was never exercised.** Zero aborts at every level, across
1000 districts. A thousand users in fewer zones would collide, and
FoundationDB would abort and retry. That reverses the conclusion if the
abort rate is high.

**The request rate is assumed.** 10 per minute per user comes from
`rfd/0102` and is not measured against a real client.

**Interest management is assumed to work.** `rfd/0100` gets 256 kbps by
sending 8 avatars, not 1000. If a zone must show more, the per-client
rate rises and the egress figure rises with it.

## Tests that would settle it

1. Run the existing probe at 128, 256, 512 and 1024 workers on one
   machine. If throughput still climbs past 32, the ceiling is wrong.
2. Hold 1000 idle connections open and measure memory and idle CPU.
3. Rerun with 10 districts rather than 1000, and record the abort rate.
   That is the contention case, deliberately.
4. Measure a real client's request rate, and replace the 10 per minute
   assumption.

Tests 1 and 3 reuse `probe/fly/scaling.exs` from `ecto-bench-tpcc` with
different constants.

## What follows if it holds

Scale the database last. One machine covers 1000 users, so effort
belongs in bandwidth: interest management, the send rate, delta
compression and the voice bitrate.

`rfd/0101` measured zstd delta at 16.1 times on the wire. That is the
lever with the most headroom, and it is already built.

## Sources

- `data/measurements/`, the raw rows behind every measured figure
- `rfd/0100` for the 256 kbps cap, `rfd/0102` for the budget
- `ecto-bench-tpcc` ADR 0007 for the throughput runs
