---
title: "RFD 0070: Export loop-slice server telemetry to the observability collector"
rfd: "0070"
state: published
scope: loop-slice server OpenTelemetry export
---

## Problem

The loop-slice server already emits spans, counters, gauges, and logs
through its `OpenTelemetry` engine module. Export stays idle with no
endpoint set. A deployed server with no configured endpoint stays
unobservable.

## Decision

The loop-slice deploy exports OpenTelemetry over HTTP to the
observability collector at `http://host.containers.internal:4318`,
with `OTEL_SERVICE_NAME=loop-server`. The server already emits spans,
counters, gauges, and logs through its `OpenTelemetry` engine module,
but export stays idle with no endpoint set, so a deployed server with
no configured endpoint stays unobservable. `host.containers.internal`
resolves to the host, not the loop-slice container's own loopback,
because the server runs as a separate rootless container outside the
observability pod's network namespace. HTTP on 4318 matches the
export protocol the rest of the fabric already uses. Export stays
opt-in in the engine and the image; only the deploy's environment sets
the endpoint, because only there is the collector a co-located,
declared dependency. Applying these values to the deploy's environment
file and quadlet is follow-up work; this record fixes the address and
the protocol.

## References

- Full context, downsides, rejected alternatives, and confirmation:
  `DETAILS.md`
- Original record:
  `decisions/20260629-loop-slice-telemetry-to-the-observability-collector.md`

## Related

- [`rfd/0011-observability-stack-victoriatraces`](https://github.com/v-sekai-multiplayer-fabric/multiplayer-fabric-archive/tree/main/rfd/0011-observability-stack-victoriatraces):
  the collector and Victoria backends this export reaches. It now
  lives in `multiplayer-fabric-archive`, per `rfd/0106`.

## Detail

{{< include DETAILS.md >}}
