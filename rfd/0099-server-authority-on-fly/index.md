---
title: "RFD 0099: Server authority on Fly, and what the client is allowed to do"
rfd: "0099"
state: discussion
scope: zone-server-h2o, zone-guest-godot
---

## Problem

`rfd/0095` through `rfd/0098` decided how guests run, how they store
data, how they talk, and what rollback costs. None of them says who
decides what is true.

The server must be authoritative. A client that reports its own position
can report any position. `rfd/0092`'s ReBAC planes do not help here,
because the client holds a legitimate relation to its own avatar.

This RFD states the authority boundary. It also states the budget the
boundary must fit, because a 256 MB machine with one shared vCPU cannot
hold an unbounded design.

## What the code already provides

Three facts came out of reading the tree, and each one changes the
design.

### The physics is already integer, so determinism is already solved

`src/gen/xr_grid_entity_packet.h` is explicit: "no floats at all --
position is int64 absolute micrometers, velocity is int16".

`src/zf_zonetick.h` gives the update rule:

    pos_um += (int64_t)vel_i16 * V_MAX_PHYSICAL_DEFAULT_UM_PER_TICK / INT16_MAX

That is integer arithmetic, applied per tick. It produces identical
results on every machine, with no dependence on compiler, operating
system, or instruction set.

Glenn Fiedler rejects deterministic lockstep for a shooter because
float determinism across platforms is nearly impossible. This codebase
does not have that problem, and it does not need libriscv to avoid it.
Fixed-point integers already avoid it.

libriscv earns its place for UNTRUSTED guest code, per `rfd/0095`. It
is not what makes the core entity update deterministic.

### The packet already carries an owner and a clock

`xr_grid_entity_packet_t` holds `class_owner`, which is
`(class << 24) | owner`, and `hlc`, which is `(frame << 8) | counter`.

So ownership and a hybrid logical clock exist on the wire already. The
authority model does not need a new field. It needs a rule about what
`owner` means.

### The entity record is 100 bytes

`XR_PACKET_SIZE` is 100. `rfd/0002` sizes a zone at 200 entities per
tick.

## Decisions

### 1. Ownership is not authority

`class_owner` names who a client speaks for. It does not make the
client correct.

A client sends INPUT. The server computes STATE. The server writes
`zf/entity/{z_id}/{e_id}`, and a client never writes it.

This is the whole boundary, and every decision below follows from it.

### 2. Clients predict with the same integer rule

A client runs the update rule from `zf_zonetick.h` locally, on its own
inputs, and draws the result immediately.

Because the rule is integer, the client and the server compute
identical values from identical inputs. Divergence therefore means a
missing input or a rejected input. It never means float drift.

That is a stronger property than a float engine can offer, and it comes
free with the existing packet format.

### 3. Server-side lag compensation is a ring of packets, not a machine snapshot

To validate a hit, the server rewinds entity positions to what the
shooter saw, then tests the shot.

That needs entity history, not a virtual machine snapshot. The size is
small:

    200 entities x 100 bytes x 7 ticks = 140000 bytes

140 KB covers about 100 ms of compensation at 64 Hz. A full second of
history is 1.28 MB.

`rfd/0098` measured a libriscv snapshot of an engine guest at 64.04 MB,
and 7 of them at 448.26 MB, which does not fit the machine. Lag
compensation does not pay that cost, because it does not snapshot a
machine. It copies 100-byte records.

Keep these two mechanisms separate. Lag compensation uses the ring.
Guest rollback uses forking, under `rfd/0098`'s 8 MB budget.

### 4. Interest management is required, not an optimization

The bandwidth arithmetic settles this.

    200 entities x 100 bytes            = 20000 bytes per tick
    20000 bytes x 64 Hz                 = 1280000 bytes per second
                                        = 10.2 Mbit per second, per client

Ten clients on one zone is 102 Mbit per second of egress. That is
implausible for a client connection and expensive on Fly.

So a zone must not send every entity to every client. Interest
management and delta compression are load-bearing requirements of the
design. `rfd/0002` and the interest-management literature already cover
the algorithms.

State this in the fan-out work (`zone-server-h2o` issue #41) rather than
discovering it after the loop exists.

### 5. The per-tick budget on Fly

One tick is 15625 us. Measured and known costs:

| Item                      | Cost                               | Source     |
| ------------------------- | ---------------------------------- | ---------- |
| FDB commit for the tick   | 3552.9 us                          | `rfd/0097` |
| Entity history ring write | 20000 byte copy                    | this RFD   |
| Fan-out serialize         | one encode per entity              | issue #41  |
| Fan-out send              | one queued datagram per subscriber | issue #41  |

The database commit is 23 percent of the tick before any game logic
runs. That is the dominant fixed cost, and it grows on a real cluster,
because `rfd/0097` measured a single node with memory storage.

### 6. Authority must not stall behind the database

`zonetick_fdb_this_zone()` skips a tick while the previous FDB commit stays
open. Fan-out must not sit inside that guard.

If it does, a slow database freezes every client's view of the world.
Measured p99 commit is 5112.5 us against a 15625 us tick, so there is
headroom today, and a real cluster removes some of it.

Send authoritative state every tick. Let the database write fall behind
when it must. The server stays authoritative either way, because
authority lives in the server's own state, and the database is where
that state is made durable.

### 7. ReBAC gates what a client may propose

`rfd/0092`'s use plane decides whether a client may act on an entity at
all. `class_owner` decides which entity a client speaks for.

Neither decides whether the result is correct. Decision 1 does.

## Stale numbers found while writing this

The tree names three tick rates. `webtransport_server.h:20` defines
`ZONE_TICK_HZ` as 64, which is what runs. `zf_zonetick.h:19` refers to a
"fixed 30Hz assumption", and `webtransport_server.h:12` mentions
`PBVH_SIM_TICK_HZ = 20` from the original Godot implementation.

64 is correct. The other two are stale comments. Correct them, because
every budget in `rfd/0096` through `rfd/0099` divides by 15625 us.

## Consequences

The client is a predictor and an input source. It is never a source of
truth.

Two rewind mechanisms exist, and they must not be confused. Lag
compensation rewinds 100-byte entity records in a 140 KB ring. Guest
rollback forks a libriscv machine under an 8 MB working-set budget.

Interest management moves from future work to a requirement, because
10.2 Mbit per second per client is not shippable.

Determinism of the core simulation does not depend on libriscv. It
depends on the integer packet format, which already exists and already
has golden vectors.

## Sources

- `src/gen/xr_grid_entity_packet.h`, the integer packet format
- `src/zf_zonetick.h`, the integer per-tick update rule
- [Deterministic Lockstep, Gaffer On Games](https://gafferongames.com/post/deterministic_lockstep/)
- [Floating Point Determinism, Gaffer On Games](https://gafferongames.com/post/floating_point_determinism/)
- [A Survey and Taxonomy of Latency Compensation Techniques for Network Computer Games](https://doi.org/10.1145/3519023)
- [Interest Management for Distributed Virtual Environments: A Survey](https://doi.org/10.1145/2535417)
