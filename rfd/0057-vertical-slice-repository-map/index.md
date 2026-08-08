---
title: "RFD 0057: Repository map of the loot-action vertical slice"
rfd: "0057"
state: published
scope: repository ownership index for the whole fabric organization
---

## Problem

The loot-action core-loop slice spans more than a dozen repositories
in the organization. Without an index, a contributor cannot tell
which repository owns which concern, or which name is current. A
contributor also cannot tell whether the playable slice imports the
cores or carries its own copies.

## Decision

The loot-action core-loop slice spans more than a dozen repositories
in the `v-sekai-multiplayer-fabric` organization: a playable app, one
proven core per loop concern, the wire and transport specs, the
engine fork and its assembly recipe, the backend and infrastructure
services, the verification queue, and these docs. Without an index, a
contributor cannot tell which repository owns which concern, which
name is current, or how the pieces relate — in particular whether the
playable slice imports the cores or carries its own copies. One
repository owns each concern; this record is that index. The playable
slice (`godot-loop-slice`) does not import the core repositories. It
transcribes the proven cores into GDScript reducers, with the cores'
wire-parity vectors pinning the behavior; the core repositories stay
the canonical, proven reference. The full map — playable slice,
proven cores, wire and determinism specs, the engine and its
assembly, backend and infrastructure services, platform tooling, and
verification and docs — lives in `DETAILS.md`, one repository link
per concern.

## References

- The full repository map, one entry per concern, plus rejected
  alternatives and the confirmation record: `DETAILS.md`
- Original record: `decisions/20260612-vertical-slice-repository-map.md`
- Organization: `https://github.com/v-sekai-multiplayer-fabric`

## Related

- `rfd/0045-loot-action-core-loop-mvp-vertical-slice`: the slice this
  map indexes.

## Detail

{{< include DETAILS.md >}}
