---
title: "RFD 0027: An umbrella package installs every component in one command"
rfd: "0027"
state: published
scope: package distribution (scoop, Homebrew, docker compose)
---

## Problem

The project ships as several components, such as the sinew mocap apps
and the zone backend services. A newcomer had to know and install
each component name one at a time. No single file listed the full set
of components the project contains.

## Decision

A project that ships as several components (the sinew mocap apps, the
zone backend services) forces a newcomer to know and install each
component name one at a time. Each distribution channel now carries
one thin umbrella entry that depends on every component and installs
nothing of its own: a scoop metapackage manifest, a Homebrew formula,
and a `docker-compose.yml`, each naming the full component set in its
dependency list. The channel's own resolver installs the whole set
from a single command, and the umbrella file doubles as the canonical
list of what the project contains. Adding a component touches one line
in the umbrella beside the new component's own entry.

## References

- Full context, decision drivers, considered options, consequences,
  and confirmation steps: `DETAILS.md`
- Original record:
  `decisions/20260609-umbrella-package-installs-all-components.md`
- `scoop-bucket/bucket/sinew.json`, `homebrew-sinew/Formula/sinew.rb`,
  the zone backend `docker-compose.yml`

## Detail

{{< include DETAILS.md >}}
