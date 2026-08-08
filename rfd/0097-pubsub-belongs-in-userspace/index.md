---
title: "RFD 0097: Pubsub belongs in userspace, and FoundationDB keeps the durable log"
rfd: "0097"
state: discussion
scope: zone-server-h2o, zone-guest-godot
---

## Problem

A zone fabric needs to publish. A zone server produces state at 64 Hz.
Clients subscribe. Zones tell other zones about events.

Three mechanisms were candidates. FoundationDB is already a dependency,
it is linearizable, and it has a watch API that looks like exactly this.
Userspace fan-out is already possible, because the zone server already
holds every subscriber connection. eBPF was raised as a third option.

`rfd/0096` established that ranking these from documentation produces
wrong answers. So this RFD measures first.

## Method

A probe ran against FoundationDB in the CI image, single node, memory
storage engine.

That configuration is the FASTEST case. A real cluster replicates,
crosses machines, and adds latency. Every FoundationDB number below is
therefore a lower bound for production.

The comparison figure is `rfd/0096`'s measured `AF_UNIX`
`SOCK_SEQPACKET` round trip on Fly: 8910 ns. One ZoneTick at 64 Hz is
15.6 ms.

## Data

| Operation                             | Median     | p99        | Fraction of one tick |
| ------------------------------------- | ---------- | ---------- | -------------------- |
| FDB commit, one publish               | 3552.9 us  | 5112.5 us  | 23 percent           |
| FDB watch fire, publish to subscriber | 12214.3 us | 12579.5 us | **78 percent**       |
| FDB versionstamp log append           | 2509.5 us  | 3691.2 us  | 16 percent           |
| `AF_UNIX` round trip, from `rfd/0096` | 8.9 us     | --         | 0.06 percent         |

Concurrent watches, measured to the failure point:

```
FDBPUB concurrent watches held = 10000,
       stopped at error 1032 (Too many watches currently set)
```

## Decisions

### 1. Real-time pubsub runs in userspace

A watch fires 12214 us after the publish. That is 78 percent of a
15.6 ms tick, on the fastest possible FoundationDB configuration.

An `AF_UNIX` round trip is 8.9 us. The watch path is approximately 1371
times slower.

A tick cannot spend 78 percent of itself on notification. The zone
server already holds every subscriber connection through picoquic and
WebTransport. Fan-out belongs there.

### 2. FoundationDB is not a message bus, and the watch cap proves it

The probe held exactly 10000 concurrent watches and then failed with
error 1032, `Too many watches currently set`.

That cap belongs to the database, not to a zone. A fabric of many zones
shares it. Ten thousand subscribers is a plausible target for a
multiplayer fabric, so this is a real ceiling and not a theoretical one.

Watches also carry semantics that do not match a message bus. A watch
is edge-triggered and reports only that a key changed. It does not
report what the value became. A subscriber must read the key again, and
values that changed between the fire and the re-read are lost.

That behavior is correct for "configuration changed, load it again". It
is wrong for an event stream.

### 3. FoundationDB keeps the durable event log

A versionstamp append commits in 2509 us. Versionstamps give a total
order, assigned by the cluster, without coordination between writers.

This is the right tool for events that must survive a process, need an
order across zones, and arrive at a low rate. Zone lifecycle, guest
load and unload, administrative actions, and ReBAC changes all fit.

The rate limit is explicit. At 2509 us per commit, a publisher gets
approximately 400 appends per second in the best case. Do not put a
64 Hz state stream here.

### 4. Watches are for rare signals only

A watch is acceptable when the event is rare and the subscriber count
is small. Configuration reload is the example.

Never place a watch on a per-tick key. Never allocate a watch per
subscriber, because decision 2 gives the ceiling.

### 5. eBPF does not fan out

`SOCKMAP` and `sk_msg` redirect a message from one socket to another.
Redirection moves a message. It does not copy one message to many.

Pubsub fan-out is one message to N subscribers. That is not the
operation these programs perform, so no custom eBPF program replaces
the userspace loop.

This is the same category error that `rfd/0096` corrected for uBPF.
eBPF is a filter and redirect facility. It is not a transport, and it
is not a message bus.

One eBPF case stays open and unmeasured. `SOCKMAP` splicing suits a
RELAY, which is what the zone server becomes between a Godot guest and
a WebTransport guest. That is one to one, so decision 5 does not
exclude it. It needs its own measurement.

## The pattern

This is the same split that `rfd/0095` made for storage, applied to
messaging.

`rfd/0095` put small hot state in FoundationDB and content in an object
store, because each store does one job well. The same reasoning
applies here. FoundationDB gives durability, ordering, and
linearizability. It does not give low latency or fan-out. Userspace
gives low latency and fan-out. It does not give durability.

Route by what the message needs:

| Message                   | Path                       | Why                                       |
| ------------------------- | -------------------------- | ----------------------------------------- |
| Per-tick state to clients | Userspace, WebTransport    | 64 Hz, needs 8.9 us and not 12214 us      |
| Guest to zone             | `AF_UNIX` `SOCK_SEQPACKET` | `rfd/0096`                                |
| Durable cross-zone event  | FDB versionstamp log       | Ordering and survival matter, rate is low |
| Configuration changed     | FDB watch                  | Rare, few subscribers                     |

## Consequences

The zone server owns fan-out. That work is a loop over subscriber
connections, and it needs no new dependency.

The 10000 watch ceiling is a fabric-wide budget. Any design that
allocates a watch per subscriber, or per zone-and-subscriber pair, must
state how it stays under that number.

Every FoundationDB figure here is a lower bound, because the probe ran
single node with memory storage. Re-measure against the deployed
cluster before treating any of them as a headroom calculation.

`rfd/0096`'s lesson repeats. FoundationDB looked like a message bus
because it has a watch API. Measurement showed the watch path costs 78
percent of a tick and stops at 10000 subscribers.

## Sources

Probe source and raw output live beside this RFD in `fdbpub.c`.

- [FoundationDB developer guide, watches](https://apple.github.io/foundationdb/developer-guide.html#watches)
- [FoundationDB known limitations](https://apple.github.io/foundationdb/known-limitations.html)
- `rfd/0096` for the 8910 ns `AF_UNIX` measurement on Fly
