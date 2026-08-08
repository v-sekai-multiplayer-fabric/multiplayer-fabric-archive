# multiplayer-fabric-archive

Archived decision records for the multiplayer fabric stack. This repository is the
standing home for records that are no longer current. Nothing here is a live design.

For current records, read
[multiplayer-fabric-manuals](https://github.com/v-sekai-multiplayer-fabric/multiplayer-fabric-manuals),
published at
[the manuals site](https://v-sekai-multiplayer-fabric.github.io/multiplayer-fabric-manuals/).

## What is here

| Path                            | Contents                                                             |
| ------------------------------- | -------------------------------------------------------------------- |
| `_archive/decisions/`           | 26 MADR records, each one rejected or superseded.                     |
| `_archive/decisions/attachments/` | Images the records above cite.                                      |
| `rfd/0011-observability-stack-victoriatraces/` | Superseded on the observability stack.          |
| `rfd/0061-quadlets-on-fedora-44-instead-of-harvester/` | Superseded by `rfd/0089`, which moves the deployment target to Fly.io. |

These two RFDs keep their original numbers. The manuals repository does not reuse a
number after a move, so `rfd/0011` and `rfd/0061` stay reserved there.

## No site

This repository holds plain Markdown. It carries no Quarto site, no build, and no
checks. Archived content does not change, so there is nothing to keep green.

## History

The git history of this repository is the full history of
`multiplayer-fabric-manuals` up to the split on 2026-08-07. Every file here keeps
every commit that touched it, at its original path. One commit after that removes
the files that stay live in the manuals repository.

## Adding to the archive

Move the file here, at the same path it had in the manuals repository. Delete it
there. Keep no pointer stub, per `rfd/0000-conventions`. Repair any live Markdown
link that pointed at it, because a moved file leaves a 404 on the published site.
