---
title: "RFD 0088: zone-server-h2o's transport is picoquic + picotls, not h2o's own QUIC"
rfd: "0088"
state: published
scope: zone-server-h2o transport
---

## Problem

`zone-server-h2o` needs WebTransport and datagrams to talk to the
Godot client. `h2o` (the base this repo ports from) has no
WebTransport or datagram support at all — a search of its source tree
confirms this. What QUIC stack should carry that traffic?

## Decision

Use `picoquic` + `picotls`, the same QUIC stack the Godot client's
`WebTransportPeer` (`godot/modules/http3/`) already builds on, not
`h2o`'s own stack. `picoquic` has working WebTransport, datagrams,
and MASQUE support that `h2o` lacks entirely. Client and server share
one proven QUIC stack instead of two different, independently
verified ones.

`thirdparty/picoquic` and `thirdparty/picotls` are git subtrees,
checked in at the exact commits that fork vendors.
`src/transport/webtransport_server.c` bridges this QUIC transport
into `h2o`'s own event loop. `src/transport/wt_session.c` handles
H3/WebTransport session negotiation on top of it.

## References

- `cmake/picoquic.cmake` (mirrors the Godot module's own `SCsub`)
- `v-sekai-multiplayer-fabric/zone-server-h2o`

## Related

- `rfd/0023-webtransport-http3-transport`: the client-side
  `modules/http3` decision this shares a QUIC stack with.
- `rfd/0083-zone-server-h2o-replaces-godot-fabriczone`
