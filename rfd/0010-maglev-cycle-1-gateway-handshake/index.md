---
title: "RFD 0010: Godot client transport handshake against the authoritative server"
rfd: "0010"
state: published
scope: zone-server-h2o
---

## Problem

The project had no proof that a real Godot client could complete a
transport handshake against the authoritative server. A non-Godot
ping, such as curl or a Python script, tests only the wire. It does
not test Godot's own multiplayer peer, thread model, and datagram
path. Engine-specific bugs could then surface later, deep in a
gameplay scene, where they are harder to isolate.

## Decision

A minimal headless Godot client proves the client side of the network
stack. It opens a transport connection to the authoritative server,
waits for the server's first message, logs it, and exits cleanly.
Running headless isolates client networking from display and input. A
non-Godot ping (curl, Python, an Elixir harness) cannot substitute: it
tests the wire, not Godot's own multiplayer peer, thread model, and
datagram path, so engine-specific bugs would surface later, deep in a
gameplay scene, where they are harder to isolate.

Transport stays switchable: ENet as the stable local default, or
WebTransport/QUIC under `TRANSPORT=wt`. The client connects straight to
the authoritative server; no proxy tier sits in the path. Pass criteria:
the connection completes without a TLS or handshake error, the client
receives and logs the server's first message, and the client exits
cleanly with no orphaned process or open port.

`DETAILS.md` records the rejected alternatives and the confirmation run.

## References

- Rejected alternatives and confirmation log: `DETAILS.md`
- Original record: `decisions/20260506-maglev-cycle-1-gateway-handshake.md`

## Related

`rfd/0008-webtransport-over-quic-for-game-traffic/index.md`

## Detail

{{< include DETAILS.md >}}
