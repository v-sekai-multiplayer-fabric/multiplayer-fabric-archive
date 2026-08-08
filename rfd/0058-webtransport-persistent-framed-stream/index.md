---
title: "RFD 0058: WebTransport reliable delivery on one persistent framed stream per session"
rfd: "0058"
state: published
scope: webtransportd WebTransport transport adapter
---

## Problem

The picoquic WebTransport server goes silent once four or more
sessions connect. A fresh bidirectional stream per reliable message
uses up the connection's stream credit. The used-up credit blocks the
connect-accepted response on the control stream, so a late session
never finishes its handshake.

## Decision

The picoquic WebTransport server goes silent once four or more sessions
connect. A fresh bidirectional stream per reliable message uses up the
connection's stream credit. The used-up credit blocks the
connect-accepted response on the control stream, so a late session
never finishes its handshake. Reliable traffic now rides one
persistent bidirectional WebTransport stream per session. The client
opens the stream once, right after connect-accepted, and appends
length-prefixed frames to it for the life of the session. Each frame
header carries a channel number and a reliable bit, so the receiver
reads the channel straight from the frame. Unreliable traffic still
rides datagrams. This design matches the picoquic `wt_baton` reference
and the webtransportd frame spec. The stream count per session stays
at one, so concurrent joins keep their stream credit and reach the
open state.

## References

- Full drivers, considered options, supporting invariants, and the
  confirmation record: `DETAILS.md`
- Original record:
  `decisions/20260612-webtransport-persistent-framed-stream.md`
- Lean+Plausible proofs:
  [http3-queue](https://github.com/v-sekai-multiplayer-fabric/http3-queue)

## Related

- `rfd/0049-fabric-channels-as-reliability-classes`: the channel and
  reliability-class model this stream carries.
- `rfd/0052-http3-listener-session-findings`: other http3
  multi-session findings.

## Detail

{{< include DETAILS.md >}}
