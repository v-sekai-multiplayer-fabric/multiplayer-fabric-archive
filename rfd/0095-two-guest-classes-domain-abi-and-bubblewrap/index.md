---
title: "RFD 0095: Two guest classes, a domain ABI, and Bubblewrap"
rfd: "0095"
state: discussion
scope: zone-server-h2o, zone-guest-godot, fabric-godot-core
---

## Problem

`rfd/0094` makes `zone-server-h2o` the host of the minimum UGC game
loop. It says guests are CDN-delivered riscv64 ELFs. It does not say
how a guest reaches the host, and it does not say that all guests are
alike.

The implementation found that both gaps are real.

`zone-server-h2o` PR #23 gave the host a POSIX filesystem on top of
FoundationDB. The filesystem worked. A Godot engine ELF started under
it, ran 57.9 million instructions, and read `project.godot` out of
FoundationDB.

The cost was 17 syscall handlers. The host supplied `/dev/urandom`,
`/dev/null`, `/dev/zero`, `getrandom`, `chdir`, `getcwd`, `mkdirat`,
`unlinkat`, and process-spawn stubs. The next two failures in the log
were the mbedTLS entropy pool and the Godot scene loader. The list
after those has no end. It ends when `zone-server-h2o` contains a
Linux kernel.

There are two causes. Neither one is libriscv.

_The file abstraction forces the emulation._ FoundationDB is a
key-value store. The guest layer made it look like POSIX. It added
handles, offsets, `lseek`, a working directory, and device nodes.

This codebase invented every one of those concepts. This codebase then
defends every one of them. The guests only ever wanted get and put.

_One runtime served two unlike guest classes._ A UGC script needs
approximately ten host operations. It runs on every tick. A complete
game engine needs a userland.

libriscv calls a guest function 5.25 to 7.25 times in the time
Wasmtime calls one. It is the best tool available for the first class.
It is the wrong tool for the second.

## Decision

There are two guest classes. Each class gets the runtime that suits
it. The host gives guests a domain ABI. The host does not emulate
Linux.

Stated as deployment, which is how it is built and paid for:

- Zone guests run under Bubblewrap on Fly.io.
- libriscv `.elf` programs carry hybrid rollback-kinematic networking
  between Godot zone servers and Godot zone clients.

### Guest classes

A _script guest_ is user-generated content. It is small. It runs on
every tick. It runs in-process under libriscv. This is the class that
carries rollback-kinematic prediction and reconciliation.

An _engine guest_ is a complete application, such as a Godot server
build. It is large. It runs for the life of a zone. It runs as a
separate process under Bubblewrap.

### The dividing line is rollback determinism

The two runtimes are not a compromise. Each one answers a different
requirement.

libriscv re-executes bit-exactly. `machine.serialize_to`
(`machine.hpp:431`) writes complete machine state. Machine forking
(`machine.hpp:61`) copies it. Together they give true snapshot and
replay of a running guest.

Hybrid rollback-kinematic networking needs exactly this. It runs
between a Godot zone server and a Godot zone client. Both sides predict
state from a kinematic model. Both sides then reconcile.

Reconciliation replays inputs from a snapshot. If the replay diverges
from the recorded run, the reconciliation is wrong. It is wrong
silently, and it is wrong on the client that did nothing unusual.

A Bubblewrap process cannot do this. It is a Linux process. It holds
kernel state, timers, file descriptors, and scheduler position.
Nothing snapshots it faithfully.

Therefore, hybrid rollback-kinematic networking between Godot zone
servers and Godot zone clients runs as libriscv `.elf` programs, in the
style of godot-sandbox. Bubblewrap covers everything off that path,
where isolation is the requirement and replay is not.

### The domain ABI

`src/sandbox/zone_abi.h` is the complete set of host operations a
script guest can reach. The host and every guest include the same
header, so the two cannot disagree about a number.

The precedent is godot-sandbox. It is libriscv's own production Godot
integration. It numbers its ABI from 500. It emulates no Linux. Its
guests call `ECALL_VCALL` and `ECALL_GET_NODE`, which are domain
operations. This ABI numbers from 600, which leaves the 500 block free
so one guest can speak both.

| Call           | Purpose                    |
| -------------- | -------------------------- |
| `ZONE_KV_GET`  | Read one key               |
| `ZONE_KV_SET`  | Write one key              |
| `ZONE_KV_DEL`  | Delete one key             |
| `ZONE_KV_LIST` | List keys under a prefix   |
| `ZONE_PRINT`   | One zone-tagged log line   |
| `ZONE_ENTROPY` | Random bytes from the host |
| `ZONE_OBJ_GET` | Read object bytes          |
| `ZONE_OBJ_PUT` | Publish an object          |

Below the ABI, the host installs 7 Linux syscalls. These are
libriscv's `setup_minimal_syscalls()`: `close`, `lseek`, `write`,
`fstat`, `exit`, `brk`, and `ebreak`. They are a guest's `printf` and
heap. They are not a userland.

### Two stores, and the split is deliberate

`ZONE_KV_*` reaches FoundationDB. FoundationDB is used as what it is,
which is a linearizable transactional datastore. It holds small,
mutable, hot state. Values cap at 32768 bytes, which is well below
FoundationDB's 100000-byte limit. No chunking layer is needed. A value
over the cap returns `-E2BIG`, because at that size the data is
content and not state.

`ZONE_OBJ_*` reaches the object store. The format is casync
content-defined chunking, with `.caibx` indexes over a CDN and an
S3-shaped backend. Objects are immutable, deduplicated, and cached.

Do not virtualize a filesystem over either store. The filesystem is
what forced the 17 handlers.

Content in FoundationDB would pay transaction cost, replication, and
quota for bytes that never change and are identical in every zone.
Hot state in an object store would give up linearizability. Each store
does the one job it is good at.

Two implementations of the casync format already exist. `aria-storage`
is Elixir, and it is the publish side. `fabric-godot-core`'s
`modules/multiplayer_fabric_asset` is C++, and it is the fetch and
verify side. Its header states that its constants are canonical.

Those constants are:

- SHA-512/256 chunk identifiers
- Chunks of 16 KB to 256 KB
- AES-128-GCM with a 24-hour key lifetime
- Uro for the access check

The C++ module is the reference for this host. Do not write a third
implementation.

### Quota pressure is latency, not an error

A write past a zone's storage quota blocks. It does not fail. Storage
pressure is a slow disk. It is not a condition a guest must code
around.

The consequence is stated plainly. A single guest with nothing else
freeing space stays blocked until an administrative actor deletes keys
or raises the quota. That is `rfd/0092`'s budget extension as a ReBAC
relation. It is not an error path.

### Host-supplied entropy is a replay requirement

`ZONE_ENTROPY` is not only a way to avoid emulating `/dev/urandom`.

A zone server that replays a journal, in the shape `rfd/0083` gives,
cannot let guests draw entropy from the host random number generator
behind its back. The replay would diverge from the recorded run.
Routing every guest random byte through one host call makes guest
randomness reproducible, because the host owns the seed.

### A guest cannot write an object directly

`ZONE_OBJ_PUT` is an administrative-plane action. It is `rfd/0092`'s
`CAN_GRANT` plane. A guest never holds that plane.

A guest can hold a _delegation_. A delegation is a ReBAC edge from the
guest subject to a principal that does hold administrative capability.
The edge permits the guest to publish on that principal's authority.

The host answers `ZONE_OBJ_PUT` by checking for that edge. It does not
check the guest. Without an edge the answer is `-EPERM`. With an edge
the publish happens, attributed to the delegating principal, because
authority stays with whoever holds it.

This is the mechanism that lets UGC scripting produce content. The
alternative is a host that refuses every guest write, or a host that
gives guests administrative capability. Both are wrong.

### Zone guests under Bubblewrap on Fly.io

Zone guests run under Bubblewrap, and they run on Fly.io. Name the
deployment, because the deployment is what makes the choice work.

A Fly machine is a Firecracker VM. The process is root in its own
kernel. Unprivileged user namespaces are therefore available, and
`bwrap` needs them. `rfd/0089` already selects Fly.

Bubblewrap is one unprivileged binary. It is built on Linux
namespaces. Flatpak uses it. There is no daemon and no userspace
kernel.

`--unshare-net` gives the guest no network namespace. `rfd/0094`'s
rule that guests never reach the networking loop stops being a
property this codebase maintains by omission. It becomes a property
the kernel enforces. The engine guest cannot reach the h2o loop or the
FoundationDB port, because it has no interfaces.

| Requirement            | Flag                    |
| ---------------------- | ----------------------- |
| No network             | `--unshare-net`         |
| Read-only content pack | `--ro-bind <pack> /app` |
| Writable scratch only  | `--bind <dir> /tmp`     |
| No host filesystem     | `--unshare-all`         |
| Dies with the zone     | `--die-with-parent`     |
| Syscall limits         | `--seccomp <fd>`        |

An engine guest is a separate operating-system process. It cannot
share a `libriscv::Machine` address space, so the in-process ABI does
not apply to it. It reaches the zone over one `AF_UNIX` socket that is
bind-mounted into the sandbox.

Stated plainly: an engine guest is a subprocess with one pipe to the
zone. This is a simpler system than an in-process engine guest. It is
the same shape `rfd/0083` chose when it moved the server off Godot.

Bubblewrap ships no seccomp filter by default. Namespaces alone do not
restrict system calls. The filter is this project's to write, and it
is the piece that makes this a sandbox and not a chroot. Deny at
minimum: `socket`, `execve` beyond the entry point, `ptrace`, `mount`,
and the keyring and bpf families.

## Evidence

`zone-server-h2o` PR #24 implements this decision. The test guest
`test/guest/kv_smoke.c` ran against a real FoundationDB. All 8
assertions passed.

```
zone 0: guest booting (4016 byte ELF, 512 MB mem, 16000 Minstr budget)
zone 0 guest: ok: set/get round-trip
zone 0 guest: ok: missing key reports an error
zone 0 guest: ok: multi-chunk value round-trip
zone 0 guest: ok: short read reports the full length
zone 0 guest: ok: list found the written keys
zone 0 guest: ok: delete removes the key
zone 0 guest: ok: entropy
zone 0 guest: ok: openat/socket/execve denied, guest still running
zone 0 guest: kv_smoke: PASS
zone 0: guest exited, status 0, 221987 instructions
```

Storage is real and not an in-process buffer that behaves like one.
`fdbcli getrange` shows the 20000-byte value. It shows a usage counter
of `0x4e20`, which is 20000, and which is correct after the delete.
The counter and the data agree because one transaction writes both.

Isolation holds by omission. `openat` (56), `socket` (198), and
`execve` (221) reach the `-ENOSYS` catch-all, and the guest survives.
An unimplemented call is not a crash.

The test guest is 4016 bytes and is built with `-nostdlib`. An earlier
build linked static glibc. `__libc_start_main` then reached `writev`,
`exit_group`, and `set_tid_address` before `main`. Those three looked
like guest requirements. They were startup code for a userland the
test does not use. An implementation of those three repeats this RFD's
own mistake, one layer lower.

## What rfd/0094 keeps

This RFD amends `rfd/0094`. It does not replace it.

`rfd/0094` is correct that `zone-server-h2o` hosts the loop. It is
correct that guests arrive over a CDN as riscv64 ELFs and are not
in-tree code. It is correct that administrative capability loads
guests and normal capability interacts with them. It is correct that
guests never reach the networking loop.

`rfd/0092`'s two planes are unchanged.

## Consequences

The host implements 8 domain calls and installs 7 minimal Linux
syscalls. It does not grow toward a kernel, because there is no next
syscall to add.

This project operates two runtimes instead of one. That is the cost of
the rollback-determinism requirement, and this RFD accepts it.

`ZONE_OBJ_GET` and `ZONE_OBJ_PUT` return `-ENOSYS` until
`modules/multiplayer_fabric_asset` is extracted from the Godot build.
The alternative is a third casync implementation, which is worse.

Bubblewrap needs unprivileged user namespaces. On Fly this is
available, because a Fly machine is a Firecracker VM and the process
is root in its own kernel. Inside Docker, `clone(CLONE_NEWUSER)` is
blocked by default, so local runs need `--cap-add SYS_ADMIN` and
`--security-opt seccomp=unconfined`, or podman with a user namespace.
Ubuntu 24.04 and later restrict namespace creation through AppArmor,
and the CI image is `ubuntu:24.04`. Verify this early.

## ReBAC is the one authorization model

`modules/multiplayer_fabric_asset` performs an `acl_check` against Uro.
That looked like a second authorization system next to `rfd/0092`'s
ReBAC planes. It is not one.

`acl_check` POSTs a tuple to Uro's `/acl/check` endpoint. The tuple is
`(object, relation, subject)`, and Uro resolves it against the relation
graph. Its components are strings such as `"asset:123"`, `"viewer"`,
and `"user:456"`. That is ReBAC.

There is one model and two evaluators. Each evaluator answers a
different question. The split is this:

- Uro resolves the graph. It answers which relations a subject holds on
  an object. It is the source of truth for the relation graph.
- `zone-server-h2o` decides the action. `rebac_check`
  (`src/gen/rebac.h:62`) takes a resolved relation set and one of
  `REBAC_ACTION_OBSERVE`, `REBAC_ACTION_INTERACT`, or
  `REBAC_ACTION_MODIFY`. It does not walk a graph.

ReBAC is the model everywhere. Where the two evaluators overlap, the
host is authoritative for host actions. `rebac_check` decides whether
the host performs an action. An `acl_check` boolean alone does not gate
a host-side action.

`lean-rebac-core` generates `src/gen/rebac.c`. Keep the host decision
there, and keep it generated. Do not add a second decision procedure
that Uro and the host must then agree about.

There is one consequence for the guest path. Guest calls must not block
on an HTTP round trip to Uro. Relations resolve when the host loads a
guest, and the host holds the resolved set for the life of that guest.
