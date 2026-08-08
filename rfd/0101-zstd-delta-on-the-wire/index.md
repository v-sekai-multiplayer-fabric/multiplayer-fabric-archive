---
title: "RFD 0101: zstd delta on the wire, and the packet stays as it is"
rfd: "0101"
state: discussion
scope: zone-server-h2o, zone-guest-godot
---

## Problem

`rfd/0100` found that a rotation-only bone carries 24 bytes of unused
position in `xr_grid_entity_packet_t`. Only the root and the hips use
translation, so 54 of 56 entities per avatar pay for a field they never
set.

It left the fix open, as either a second packet type or a flag on the
existing one. Both are format changes, and a format change costs
`lean-entity-packet`'s generated codec and its golden vectors.

There is a third option that `rfd/0100` did not consider. Compress the
frame, and use earlier state as a dictionary. Unused position bytes are
identical on every tick, so a delta should remove them without any
format change at all.

This RFD measures that option.

## Prior art in this project

Godot took the same decision, and the comparison is already recorded.

`godotengine/godot-proposals#13404` proposed delta encoding for patch
PCK files, first with bsdiff plus zstd. The author then compared it
against zstd's own `--patch-from`, which is dictionary delta.

zstd won, and `godotengine/godot#112011` shipped it in Godot 4.6. The
stated reasons were 2 to 3 percent better size, far lower compression
memory, and no vendored bsdiff fork to maintain.

One detail in that thread is worth repeating. The first measurement
showed zstd was slower, and it was wrong. `CryptoCore::md5` checksumming
dominated the timing and ran through the whole file again, which also
hurt the cache. Switching to zstd's own checksum reversed the result.

The project already runs zstd for casync chunk compression, so this
adds no dependency.

## Method

A probe built one tick of realistic state and compressed it.

- 8 avatars, 56 entities each, 100 bytes per `XR_PACKET_SIZE`, so
  44800 bytes raw per tick.
- Root and hips translate, walking forward with a vertical bob.
- The other 54 bones rotate only, and their position never changes.
- 400 ticks, sent at 20 Hz.

Three dictionary strategies were compared: none, the previous tick, and
a baseline that lags by 2, 4 or 8 ticks. The lagging baseline models
delta against the last ACKED tick, which is what an unreliable
datagram transport requires.

## Data

| Strategy                         | Bytes per tick | Rate at 20 Hz | Compress CPU |
| -------------------------------- | -------------- | ------------- | ------------ |
| Raw, no compression              | 44800          | 7168.0 kbps   | none         |
| zstd level 1, no dictionary      | 3727           | 596.3 kbps    | 42.4 us      |
| zstd level 3, no dictionary      | 3239           | 518.3 kbps    | 44.8 us      |
| zstd L1, prefix is previous tick | 2980           | 476.9 kbps    | 63.3 us      |
| zstd L3, prefix is previous tick | 2774           | 443.9 kbps    | 66.5 us      |
| zstd L1, prefix is acked minus 2 | 3014           | 482.3 kbps    | 68.0 us      |
| zstd L1, prefix is acked minus 4 | 3015           | 482.5 kbps    | 68.2 us      |
| zstd L1, prefix is acked minus 8 | 3006           | 480.9 kbps    | 63.9 us      |

Client-side decompression is 17.3 us, and the result verifies as exact.

## Decisions

### 1. Do not change the packet

zstd at level 3 with a previous-tick prefix takes 44800 bytes to 2774,
which is 16.1 times smaller.

Compare that against `rfd/0100`'s hand-packed alternative. That design
sends 384 bytes per avatar, so 8 avatars at 20 Hz cost 491.5 kbps.
Compressing the unmodified 100-byte packets costs 443.9 kbps.

So compression beats hand-packing, and it needs no format change. The
second packet type and the flag are both unnecessary.

`lean-entity-packet`'s generated codec and its golden vectors stay
untouched. That is the point.

### 2. Delta against the last acked tick, not the previous tick

An unreliable datagram can be lost. A delta against the previous tick
cannot be decoded when the previous tick never arrived.

The standard answer is to delta against a baseline that the client
acknowledges. The concern is that an older baseline produces a bigger
delta.

Measurement says the concern is small. A baseline lagging 2, 4 or 8
ticks costs 3014, 3015 and 3006 bytes, against 2980 bytes for the
previous tick. That is approximately 1 percent.

So loss tolerance is close to free. Use the acked baseline.

The lag does not grow the delta because the redundancy this exploits is
mostly WITHIN a frame, not between frames. 54 bones times 24 zero bytes
repeat inside one tick, and they repeat whether the baseline is 1 tick
old or 8.

### 3. Level 1 for the send path

Level 3 saves 206 bytes per tick against level 1, which is 33 kbps, and
costs 3.2 us more.

Take level 1 for now. Revisit it only with a CPU measurement on
`shared-cpu-1x`, because `rfd/0096` showed that machine shape punishes
CPU-bound choices in ways a workstation does not show.

### 4. The CPU cost fits, and it is per client

Each client acknowledges different ticks, so each client needs its own
delta. The cost is per client, not per zone.

At 68 us per tick and 20 Hz, one client costs 1.36 ms of CPU per
second, which is 0.14 percent of a core. Thirteen concurrent clients
cost 1.8 percent.

That fits on `shared-cpu-1x`. Confirm it there before relying on it,
since every number in this RFD came from a development machine.

## What this does not fix

Compression does not solve the cost cap in `rfd/0100`.

At 443.9 kbps a client uses 0.1997 GB per hour. A 548 GB monthly budget
then buys 2744 client-hours, which is 3.8 concurrent users running
continuously.

`rfd/0100` measured 2.6 concurrent users at 648.6 kbps. Compression
moves that to 3.8. It does not reach 40.

So interest management, the send rate, and the voice bitrate all remain
load-bearing. zstd is a large constant-factor win, and the cost problem
needs an order of magnitude.

## Consequences

`rfd/0100`'s open question is closed. The wire format does not change.

The send path gains a compression step with a per-client baseline, and
the acknowledgement of that baseline becomes part of the protocol.

The server holds one baseline frame per client. At 44800 bytes per
frame and 13 clients that is 582400 bytes, which is small against
`rfd/0096`'s measured 212188 kB of usable memory.

Compression runs per client, so its cost grows with client count rather
than with zone size.

## Sources

Probe source is `wire.c` beside this RFD.

- [godotengine/godot-proposals#13404](https://github.com/godotengine/godot-proposals/issues/13404), the bsdiff against zstd comparison
- [godotengine/godot#112011](https://github.com/godotengine/godot/pull/112011), which shipped zstd delta in Godot 4.6
- `src/gen/xr_grid_entity_packet.h`, for `XR_PACKET_SIZE` and the field layout
- `rfd/0100`, for the 56-entity avatar and the cost cap
