---
title: "RFD 0065: fabric-platform-central as Elixir OTP app with Burrito and casync"
rfd: "0065"
state: published
scope: Windows platform-manager packaging (fabric-platform-central)
---

## Problem

The platform manager that installs and updates the game client and
dedicated server ran as a Godot placeholder. An earlier .NET 8
self-contained launcher added a dependency the team does not
otherwise use, and it only wrapped a shell invocation. Neither
approach reused the content-sync protocol the `zone-backend` already
uses.

## Decision

The platform manager that installs and updates the game client and
dedicated server rebuilds as an Elixir OTP application,
`fabric_platform_central`, in place of a Godot placeholder. It depends
on `aria_storage`, the existing, tested Elixir casync
content-addressable-sync library the `zone-backend` already uses, so
the updater does not duplicate that protocol in GDScript or C++.
Burrito wraps the `mix release` into one self-extracting Windows
binary, `fabric-platform-central.exe`, bundling ERTS and all BEAM
code with no separate shim or sidecar file. That single binary
satisfies the MSIX `Executable=` entry-point requirement directly. A
release workflow builds the binary with OTP 27, Elixir 1.18, and Zig
0.14.0, packs it into a signed MSIX, and attaches it to the GitHub
Release. This replaces an earlier .NET 8 self-contained launcher,
which added a dependency the team does not otherwise use and only
wrapped a shell invocation.

## References

- Full design, MSIX layout, workflow YAML, code samples, downsides,
  and rejected alternatives: `DETAILS.md`
- Original record:
  `decisions/20260624-fabric-platform-central-elixir-burrito-casync.md`
- Burrito: [burrito-elixir/burrito](https://github.com/burrito-elixir/burrito)

## Related

- `rfd/0067-release-tag-progression-dev-beta-rc`: the version tags this
  release workflow produces.

## Detail

{{< include DETAILS.md >}}
