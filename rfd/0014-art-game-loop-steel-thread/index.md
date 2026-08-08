---
title: "RFD 0014: Recursive art-game loop and its minimal steel thread"
rfd: "0014"
state: prediscussion
scope: friends-art-game-loop
---

## Problem

The friends-art-game-loop workspace had no proof that its recursive
loop worked end to end. Building the full tool-chain first, or running
the loop by hand with no tooling, both skip that proof. The project
needed a minimal way to test every link in the loop once, before the
workspace grew further.

## Decision

Before the friends-art-game-loop workspace grows, the project proves the
loop end to end with a minimal steel thread rather than building the
full tool-chain first or hand-running the loop with no tooling. The
steel thread exercises every link once: start a local Godot
runtime/editor, connect Godot MCP, run one Godot Sandbox command, create
or modify one visible art object, record one friction note, patch one
tiny behavior, then re-run and verify the visible result changed.

The recursive loop this thread proves is: friends create a tiny
art-game, play and observe it while capturing friction, the team patches
the art-game or its tools, then replays the improved version, and
repeats. Players appear as tracker orbs; drawing pens (cassie) are a
stretch goal layered on once the loop turns.

## References

- Decision drivers, rejected options, and confirmation: `DETAILS.md`
- Original record: `decisions/20260606-art-game-loop-steel-thread.md`
- https://github.com/v-sekai-multiplayer-fabric/vsekai-godot-mcp
- https://github.com/v-sekai-multiplayer-fabric/godot-sandbox-programs
- https://github.com/v-sekai-multiplayer-fabric/friends-art-game-loop

## Related

`rfd/0018-feature-classification-poc-baseline-stretch/index.md`

## Detail

{{< include DETAILS.md >}}
