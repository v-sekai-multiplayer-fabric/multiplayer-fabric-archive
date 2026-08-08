## Context and problem statement

The fabric's reliability classes ([fabric channels](../0049-fabric-channels-as-reliability-classes/README.md))
run over ENet for the local slice. WebTransport carries the same
classes for the browser and Quest paths over one QUIC connection per
client. The picoquic WebTransport server goes silent within seconds
once four or more sessions connect: some clients never finish the
extended-CONNECT handshake, and once a few sessions are open every
client stops sending and receiving while the network thread stays
alive in `poll()`.

## Decision drivers

- A client sends a transform every tick and a control event rarely, so
  the reliable path takes a high message rate.
- picoquic caps concurrent streams per connection, and the
  extended-CONNECT response rides the session's control stream.
- The same `set_transfer_channel` plus `set_transfer_mode` lane
  selection has to read the same on both transports.

## Considered options

- A fresh bidirectional stream per reliable message, FIN'd after the
  payload.
- One persistent bidirectional stream per session, carrying
  length-prefixed frames.

## Decision outcome

Chosen option: reliable traffic rides one persistent bidirectional
WebTransport stream per session, opened once after connect-accepted
and appended to without a per-message FIN; unreliable traffic rides
datagrams. Each reliable message carries the webtransportd frame
header (`flag | len_varint | payload`, where the flag holds a 0–7
channel and a reliable bit), so the receiver delimits messages on the
shared stream and recovers the channel. This matches the picoquic
`wt_baton` reference and the webtransportd frame spec.

A fresh stream per message exhausts the connection's stream credit,
and the exhaustion starves the connect-accepted response on the
control stream, so the late session stays in connecting. The
persistent stream holds the stream count at one per session regardless
of message rate.

Five supporting invariants keep the server alive under concurrent
load:

- The inbound packet queue takes a mutex shared by the picoquic
  network thread and the main thread. A Lean+Plausible model proves
  the locked queue stays size-honest for every schedule, and finds the
  unlocked interleaving that strands the newest session's packets
  behind a stale cached length.
- The packet-loop wake branch services receive and send on the same
  iteration, and the work queue drains on every loop callback,
  including timeouts and receives. A steady wake stream otherwise
  holds the loop in the wake branch and starves both directions. The
  same Lean+Plausible model proves the inclusive iteration stays
  starvation-free for every schedule.
- `get_packet_peer`, `get_packet_mode`, and `get_packet_channel` peek
  the front of the queue, the MultiplayerPeer contract, rather than
  the last-popped value. Reading the popped value mis-attributes
  interleaved sessions, so a late client's join credits another peer
  and the client never receives its welcome.
- The server connection table sizes for the entity and interest
  fanout, above a single slot.
- The picoquic textlog stays behind the engine verbose flag, because
  its synchronous stdout writes stall the network thread under load.

## Consequences

- A client opens one reliable stream for its whole session, so
  concurrent joins keep their stream credit and reach open.
- The channel-plus-reliability flag the ENet peer reads from the
  transfer channel and mode rides inside the frame, so both transports
  realize the channels decision with one mental model.
- The slice runs identically on `TRANSPORT=enet` and `TRANSPORT=wt`;
  the text protocol stays transport-agnostic across both.

## Confirmation

The [http3-queue](https://github.com/v-sekai-multiplayer-fabric/http3-queue)
Lean+Plausible workspace proves the queue and wake-loop properties and
exhibits the failing schedules. Five concurrent WebTransport sessions
stay healthy for ninety seconds and complete full Hub-to-Field-to-Loot
rounds, on the fork's `feat/module-http3`.
