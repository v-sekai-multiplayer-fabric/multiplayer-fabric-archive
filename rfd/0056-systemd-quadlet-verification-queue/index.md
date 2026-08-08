---
title: "RFD 0056: Verification smokes as a systemd podman quadlet queue"
rfd: "0056"
state: published
scope: CI/local verification queue for the loop smokes
---

## Problem

The slice has four smokes: headless OpenXR, loot wire parity, combat
wire parity, and four-player contention. They need to run as a
repeatable queue on the workstation and on self-hosted runners. The
queue also needs ordering, logs, and per-job status. No orchestrator gave this
yet, on top of the project's podman quadlet deployment choice.

## Decision

The slice's smokes — headless OpenXR, loot wire parity, combat wire
parity, four-player contention — need to run as a repeatable queue on
the workstation and on self-hosted runners, with ordering, logs, and
per-job status, and the deployment decision already standardizes on
podman quadlets. Each smoke runs as a oneshot quadlet `.container`
unit, serialized with `After=` into a `fabric-verify.target`, because
systemd supplies the queue, the journal, and the status surface with
no extra orchestrator. A small `fabric-smoke` image (Fedora plus
fontconfig) runs the headless smokes; the Godot binary, the smoke
scripts, and the Lean golden vectors bind-mount read-only, so the
image stays generic and the artifacts stay host-owned. Every smoke
asserts against Lean-emitted golden vectors, pinned by Plausible
properties in the cores. `systemctl --user start fabric-verify.target`
runs the whole queue; `systemctl --user status 'fabric-smoke-*'` and
the journal carry the results. The units install by file copy into
`~/.config/containers/systemd`, matching the quadlet deployment
convention, and a failing smoke fails only its own unit, so the queue
surfaces regressions per stage rather than as one opaque script. The
target runs all four smokes to success under systemd on the
workstation.

## References

- Original record: `decisions/20260612-systemd-quadlet-verification-queue.md`
- Repository: `https://github.com/v-sekai-multiplayer-fabric/fabric-verify`

## Related

- `rfd/0051-headless-openxr-testing-with-monado`: one of the four
  smokes this queue runs.
- `rfd/0030-cicd-runners-as-queueing-system`: the runner model this
  quadlet queue feeds into.
