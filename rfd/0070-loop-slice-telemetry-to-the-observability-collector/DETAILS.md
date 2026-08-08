## The context

The loop-slice server emits OpenTelemetry through the `OpenTelemetry`
C++ engine module: spans for the loop's phases, counters and gauges
for grants and ticks, and log lines. Export is opt-in. The server
reads `OTEL_EXPORTER_OTLP_ENDPOINT`, and with no endpoint set it stays
idle — it records signals in process but exports nothing, which keeps
a server that has no collector from retrying against one that is not
there. The observability stack (`rfd/0011-observability-stack-victoriatraces`)
runs an OTEL collector that ingests OTLP on 4318 (HTTP) and 4317
(gRPC) and routes metrics to VictoriaMetrics, logs to VictoriaLogs,
and traces to VictoriaTraces. The collector and the server's deploy
both exist, so the telemetry the server already produces has
somewhere to go; by YAGNI times structure to the need
(`decisions/20260629-yagni-times-structure-to-need.md`), the need is
present and the wiring belongs here.

## The problem statement

A deployed loop-slice server with no configured endpoint is
unobservable: its spans, metrics, and logs stay in process and never
reach the collector. The export seam is a single environment
variable, `OTEL_EXPORTER_OTLP_ENDPOINT`, and the deploy carries the
address that a separately deployed container can actually reach.

## Design

The loop-slice deploy exports OTLP over HTTP to the observability
collector at `http://host.containers.internal:4318`, with
`OTEL_SERVICE_NAME=loop-server`.

- The collector listens for OTLP HTTP on 4318 and routes each signal
  to its Victoria backend, so the server's traces, metrics, and logs
  all land without further configuration. HTTP on 4318 matches the
  export protocol the rest of the fabric uses; the zone backend
  exports OTLP HTTP to the same port.
- The endpoint is `host.containers.internal`, not `127.0.0.1`. The
  loop-slice server runs as a separate rootless container that
  publishes its game port and does not share the observability pod's
  network namespace, so inside the container `127.0.0.1` is the
  container's own loopback. `host.containers.internal` resolves to the
  host, where the collector publishes 4318.
- Export stays opt-in in the engine and the image. The deploy opts in
  by setting the endpoint in its environment, because in the deploy
  the collector is a co-located, declared dependency. The image
  carries no default endpoint.
- A collector on another host overrides `host.containers.internal`
  with that host's address.

The values the deploy sets are
`OTEL_EXPORTER_OTLP_ENDPOINT=http://host.containers.internal:4318` and
`OTEL_SERVICE_NAME=loop-server`. Applying them to the deploy's
environment file and quadlet is follow-up work; this record fixes the
address and the protocol, not their application.

## The downsides

- An endpoint pointed at an absent collector returns the server to
  export-retry load, so the endpoint belongs only where the collector
  runs; the deploy sets it where the two are co-located.
- `host.containers.internal` holds for a same-host collector and needs
  the host address for a remote one.
- This record decides the wiring without applying it, so a deploy
  stays unobservable until its environment carries these values.

## The road not taken

- Bake a default endpoint into the image — rejected; a forced default
  drives connect-retry spam against a collector that may not be
  there, and it commits the address before the deploy knows it.
- Join the loop-slice container to the observability pod for a
  `localhost:4318` endpoint — rejected; it couples the game server's
  lifecycle and its published game port to the observability pod.
- Run the loop-slice container on host networking for a
  `127.0.0.1:4318` endpoint — rejected; it drops the published-port
  isolation the deploy relies on.
- Export over gRPC on 4317 — rejected; the fabric's OTLP export
  pattern is HTTP on 4318.

## Confirmation

With the endpoint set, the server prints `OTEL ready ->
http://host.containers.internal:4318` in place of `OTEL idle`, and the
collector receives its spans, metrics, and logs. An OTLP listener on
4318 confirms the connection; a VictoriaMetrics query for the
`loop-server` service confirms the full path once the observability
pod runs.
