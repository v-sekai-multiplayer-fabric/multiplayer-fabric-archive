# Details

## Context and Problem Statement

We want a small workspace where friends make tiny art-games together,
watch them, and improve them on a tight loop. Players show up in the
space as tracker orbs (the presence demo), with drawing pens (cassie) as
a stretch goal. Before building the workspace out, we need the smallest
end-to-end thread that proves the loop runs at all. What is that thread?

## Decision Drivers

- A tight, local-first feedback loop friends can actually turn.
- Visible results each pass, so friction is obvious.
- One friction note and one patch at a time, not a backlog.
- The smallest path that touches every link from runtime to verified
  change.

## Considered Options

- Build the full tool-chain and content pipeline first.
- A minimal steel thread that exercises every link once, then grow it.
- Hand-run the loop with no tooling.

## Consequences

- Good: every link from runtime to verified change is exercised once,
  so gaps surface early.
- Good: the loop produces a visible change and one friction note per
  pass, which keeps scope honest.
- Bad: the first thread is deliberately thin and does no real game
  design yet.
- Bad: it leans on Godot MCP and the sandbox, so a break in either
  stalls the loop.

## Confirmation

The steel thread completes from a cold local editor through to a re-run
that shows a changed visible result, with one friction note and one
patch recorded.

## More Information

This composes the presence demo (tracker orbs) and the cassie pen
(stretch) into a working loop, run through Godot MCP and the Godot
Sandbox.
