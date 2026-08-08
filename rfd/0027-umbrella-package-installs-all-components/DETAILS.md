## Context and problem statement

A project ships as several components, each with its own install
entry. The sinew mocap stack publishes one scoop manifest and one
Homebrew formula per app (`sinew-tui`, `sinew-vr-bridge`,
`sinew-viewer`), and the zone backend runs several services
(cockroach, redis, uro, nextjs, caddy). A person who wants the whole
set runs one install command per component and needs to know every
component name. How does a person install or run the full set in one
step?

## Decision drivers

- A newcomer knows the project name, not the name of each component.
- A list of the components lives in one place, so a reader sees the
  full set without assembling it.
- The package managers and the compose runtime already resolve
  dependencies, so an aggregate entry reuses machinery that exists.

## Considered options

- Documentation that lists every per-component install command.
- A shell script that runs the per-component commands in order.
- An umbrella entry per channel that declares the components as
  dependencies.

## Decision outcome

Chosen option: an umbrella entry per channel that declares the
components as dependencies, because the channel's own resolver
installs the set and the umbrella holds the full list in one file that
a reader and a tool both read.

Each distribution channel carries one thin umbrella that depends on
every component and installs nothing of its own:

- Scoop carries a metapackage manifest whose `depends` names each app,
  so `scoop install sinew/sinew` installs them all.
- Homebrew carries a formula whose `depends_on` names each app, so
  `brew install sinew-mocap/sinew/sinew` installs them all.
- The zone backend carries a `docker-compose.yml` whose `services`
  name each backend process, so `docker compose up` starts the whole
  stack.

The umbrella holds the component list and no build steps, so adding a
component touches one line in the umbrella beside the new component's
own entry.

## Consequences

- Good, because a newcomer installs or runs the whole project from its
  name alone.
- Good, because the umbrella reads as the canonical list of what the
  project contains.
- Bad, because the umbrella tracks the component set by hand, so a new
  component that skips the umbrella stays out of the one-command
  install.
- Bad, because the umbrella version and the component versions drift
  apart unless a release updates both.

## Confirmation

A one-command install on a clean machine brings up every component:
`scoop install sinew/sinew`, `brew install sinew-mocap/sinew/sinew`,
and `docker compose up` each produce the full set.

## More information

The sinew mocap buckets carry the metapackage at
`scoop-bucket/bucket/sinew.json` and `homebrew-sinew/Formula/sinew.rb`,
and the zone backend carries the compose file. Each umbrella sits
beside the per-component entries it aggregates.
