---
title: "RFD 0096: Guest transport on Fly, measured"
rfd: "0096"
state: discussion
scope: zone-server-h2o, zone-guest-godot
---

## Problem

`rfd/0095` says an engine guest reaches its zone over one `AF_UNIX`
socket. It does not say why not something faster.

That gap invited a reasonable question. Shared-memory rings are much
faster than sockets in published benchmarks. io_uring can remove
syscalls from an event loop. uBPF exists. Each looked like a candidate.

The candidates were ranked twice from documentation. Both rankings were
wrong in places. Documentation describes Linux. This system runs on one
specific machine shape, and that shape decides the answer.

## Method

A probe went to Fly.io and measured the machine that runs production.

- App: a throwaway app, destroyed after the run.
- Machine: `shared-cpu-1x`, 256 MB, region `iad`.
- Image: `ubuntu:24.04`, which is also the CI image.
- Kernel: `6.12.91-fly`.
- Reported memory: 212188 kB. Reported `nproc`: 1.

Each latency figure is a round trip between two processes. Each test
does 20000 iterations. The table reports the median.

A second run on a 16-core development machine gives contrast.

## Data

| Probe                          | Fly `shared-cpu-1x` | Local, 16 cores |
| ------------------------------ | ------------------- | --------------- |
| `AF_UNIX` `SOCK_SEQPACKET`     | 8910 ns             | 59821 ns        |
| `AF_UNIX` `SOCK_STREAM`        | 9520 ns             | 50181 ns        |
| Busy-polled shared-memory ring | did not finish      | 110 ns          |

Capability results on Fly:

| Probe                             | Result                   |
| --------------------------------- | ------------------------ |
| `bwrap --unshare-all` as root     | OK                       |
| `bwrap --unshare-net`             | OK                       |
| `bwrap` unprivileged, uid 1000    | OK                       |
| Interfaces inside `--unshare-net` | 1                        |
| Interfaces outside                | 4                        |
| `kernel.io_uring_disabled`        | 0                        |
| `io_uring_setup`                  | OK, returned fd 3        |
| `memfd_create`                    | OK                       |
| `fly-global-services`             | 172.19.15.243, `AF_INET` |

## Decisions

### 1. `AF_UNIX` `SOCK_SEQPACKET` is the guest transport

The measured round trip is 8910 ns. One ZoneTick at 64 Hz is 15.6 ms.
Transport cost is therefore 0.06 percent of a tick.

There is no performance problem to solve at this tick rate.

### 2. No busy-polled shared-memory ring

The ring is the fastest option on hardware with spare cores. It
measured 110 ns on 16 cores, which is 81 times faster than the socket
on the same machine.

On Fly it did not complete 20000 round trips in more than 8 minutes.
The socket completed the same 20000 in less than one second.

`ps` shows the cause. Both processes stayed in `R` state at 24 percent
CPU each, on a machine where `nproc` is 1. They consumed the
shared-cpu burst balance and then spun against the throttle floor.

The published ring benchmarks assume a spare core to spin on.
`shared-cpu-1x` does not give one. A number measured on a workstation
does not transfer to this deployment.

This decision is conditional on the machine shape. A dedicated-CPU
machine would change it, and this RFD does not forbid revisiting the
choice there.

### 3. Keep epoll. Do not adopt io_uring for the event loop

io_uring is reachable on Fly. `io_uring_setup` returned a descriptor,
and `kernel.io_uring_disabled` is 0. Availability is not the obstacle.

Value is the obstacle. io_uring saves syscalls, and it pays back at
high event rates. This system ticks at 64 Hz, and its transport already
costs 0.06 percent of a tick.

`IORING_SETUP_SQPOLL` is the mode that removes syscalls completely. It
spawns a kernel poller thread. That is the same shape as the busy-poll
ring, so decision 2 applies to it unchanged.

### 4. Guests must not reach io_uring

A seccomp filter examines the syscalls a task makes. io_uring
operations run on `io-wq` kernel worker threads, outside the filter of
the task that submitted them. A filter cannot screen a call the task
never makes.

A guest that holds an io_uring descriptor can therefore perform work
that the rest of the filter denies.

This is settled practice. Android 12 and later block `io_uring_setup`
in the default application policy. Chrome OS does the same. The Chrome
renderer and GPU sandboxes reject it. gVisor rejects it.

`rfd/0095`'s seccomp filter must deny `io_uring_setup`,
`io_uring_enter`, and `io_uring_register` by name.

### 5. uBPF is not a transport

uBPF is a userspace virtual machine that executes eBPF bytecode. It has
no sockets, no I/O, and no packet path. It exists because the kernel
implementation is GPL, and uBPF carries an Apache license.

uBPF is therefore an alternative to libriscv, not an alternative to an
event loop. `rfd/0095` already selects libriscv, and it gives a reason
that uBPF does not answer: `serialize_to` and forking give bit-exact
snapshot and replay.

Kernel eBPF does accelerate networking, through `SOCKMAP` and AF_XDP.
Neither applies here. A guest under `--unshare-net` has no sockets to
redirect, and decision 4 and `rfd/0095` both deny the bpf family to
guests.

### 6. UDP must bind `fly-global-services`, and it is IPv4

Fly requires a UDP service to bind `fly-global-services`. `0.0.0.0`,
`*`, and `INADDR_ANY` do not work, because Linux then selects the wrong
source address for replies.

`src/transport/webtransport_server.c:233` binds `in6addr_any`. The
socket binds, packets arrive, and replies leave with the wrong source
address. `fly/fly.toml` already declares `protocol = "udp"` on
`internal_port = 7443`, so this is live configuration.

The probe found a second fact that a documentation-only fix would miss.
`fly-global-services` resolves to `172.19.15.243`, which is `AF_INET`.
The current socket is `AF_INET6`. The fix needs an `AF_INET` socket or
a v4-mapped address. It is not an address substitution.

TCP cannot use `fly-global-services` and must bind the wildcard. The
application therefore needs asymmetric binds.

### 7. Bubblewrap works on Fly, including unprivileged

All three `bwrap` probes passed, including the unprivileged case at uid 1000. `--unshare-net` left 1 interface inside the sandbox against 4
outside.

`rfd/0095` predicted this, because a Fly machine is a Firecracker VM
and the process is root in its own kernel. The prediction is now a
measurement.

Note that Docker is the harder case, not Fly. The same probe failed
every `bwrap` test under Docker, where user namespaces are blocked by
default. CI must therefore grant `--cap-add SYS_ADMIN` and
`--security-opt seccomp=unconfined`, or use podman.

## Consequences

The engine guest transport is a plain `AF_UNIX` `SOCK_SEQPACKET`
socket. It needs no new dependency and no tuning.

`memfd_create` works, so a bulk path over `SCM_RIGHTS` stays available
if a future measurement justifies it. Size any such buffer against 212
MB of usable memory, not against 256 MB.

Two guest-facing denials are now mandatory rather than advisory:
io_uring and bpf.

The UDP bind defect is real and unfixed. Nothing that this RFD decides
matters while replies leave with the wrong source address.

## What this RFD corrects

Three claims in earlier rankings were wrong, and the measurement
corrected each one.

Shared-memory rings were ranked as the fastest option. They are the
fastest option on a machine with spare cores, and they are unusable on
this one.

io_uring was ranked as unverified. It is available, and it is
undesirable for separate reasons.

Dropping h2o for `picoquic_packet_loop()` was ranked first at one
point. The asymmetric bind requirement needs a TCP listener as well, and
`picoquic_packet_loop()` serves UDP only.

## Sources

- [Running apps on UDP and TCP, Fly Docs](https://fly.io/docs/networking/udp-and-tcp/)
- [Fly Machines Security Update, kernel v6.12.91](https://community.fly.io/t/fly-machines-security-update-dirtyfrag-fragnesia-and-copyfail/28018)
- [io_uring security model](https://kernel-internals.org/io-uring/security/)
- [io_uring and seccomp](https://blog.0x74696d.com/posts/iouring-and-seccomp/)
- [Documentation for /proc/sys/kernel](https://docs.kernel.org/admin-guide/sysctl/kernel.html)
- [uBPF](https://github.com/iovisor/ubpf)
- [Linux IPC Shootout, shared memory against Unix domain sockets](https://victoranderssen.com/blog/linux-ipc-benchmark/)
