---
title: "RFD 0047: webtransportd as the out-of-process core transport adapter"
rfd: "0047"
state: published
scope: transport adapter for the event-driven hexagonal cores
---

## Problem

The cores expose a flat C ABI and need a transport adapter to carry
their port traffic to clients. The in-engine `feat/module-http3`
transport fits the in-process authoritative tick core. It does not
fit the event-driven cores, which need a piped child process instead.

## Decision

The cores expose a flat C ABI and need a transport adapter to carry
their port traffic to clients. The fabric already runs WebTransport
over QUIC for game traffic and ships an in-engine implementation. A
standalone bridge, `webtransportd`, pipes a connection's bytes to a
child program over stdin and stdout. The project uses `webtransportd`
as the out-of-process transport adapter for the event-driven cores,
and keeps the in-engine `feat/module-http3` for the in-process
authoritative tick core, because the tick core needs shared state and
low latency in the `zone-server`, while the event-driven cores fit a
piped child cleanly. A driving port reads frames from stdin and a
driven port writes frames to stdout, framed as
`[flag | varint len | payload]`; the flag bit selects a reliable
stream or an unreliable datagram and maps to port reliability —
combat input stays reliable, pose updates stay lossy. One
authoritative core per instance funnels the four connections, so a
per-connection child relays to that single core rather than holding
state itself. The cores stay transport-agnostic behind their ports,
so a fixture adapter replays frames for CI with no daemon and no
network, reproducing the same exchange a `webtransportd` CLI smoke
test round-trips.

## References

- Original record:
  `decisions/20260611-webtransportd-core-transport-adapter.md`

## Related

- `rfd/0008-webtransport-over-quic-for-game-traffic`: the transport
  choice this adapter carries.
- `rfd/0023-webtransport-http3-transport`: the in-engine transport
  this adapter complements.
