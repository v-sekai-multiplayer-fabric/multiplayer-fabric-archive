# Details

## The Downsides

A minimal Godot client is more work than a curl or harness ping, and
the work cannot be skipped — a non-Godot client would not catch
Godot-specific datagram handling before it reaches a gameplay scene.

## The Road Not Taken

- A bare non-Godot client (curl, Python, or Elixir harness): it tests
  the server in isolation and leaves the Godot client's transport path
  unverified until a gameplay scene exercises it, where a failure is
  much harder to isolate.
- A separate gateway proxy fronting the server on a privileged port: the
  as-built path connects the client straight to the authoritative
  server, so a proxy tier adds an unproven hop the slice does not need.

## Confirmation

The loop-slice client (`godot-loop-slice/client.gd`) completes this
handshake against the authoritative server
(`godot-loop-slice/server.gd`). The playable-loop smoke runs four real
Godot clients through it end to end on ENet: each connects, joins, runs
the loop, and exits cleanly. The run on 2026-06-29 passes — all four
clients complete and exactly one loot grant lands. The WebTransport/QUIC
path is selectable with `TRANSPORT=wt` over the same handshake.
