## Summary

`rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps/index.md`, item 4, commits
CastSpell's effect step to a sandboxed `libriscv` package. This RFD is
the detail that item pointed to. It settles three things. The package
stays a single `.elf` file. Its manifest is CBOR-LD. Its runtime FFI
boundary is the same bitpacked struct format `RFD 0010` already uses
for the zone tick.

## Background

`decisions/20260611-generated-behavior-sandboxed-riscv.md` already runs
generated enemy and ability behavior as sandboxed RISC-V programs
through `feat/sandbox`. `fabric-godot-core`'s `modules/sandbox` already
embeds `libriscv` for this in the Godot client and editor tooling.
`zone-server-h2o` embeds the same sandbox directly, and lets designers
author CastSpell effects in GDScript, compiled to a RISC-V ELF binary
through `godot-sandbox-gdscript-compiler`.

This generalizes beyond CastSpell. Any non-core code the server runs,
generated behavior, CastSpell effects, and future mod or third-party
content alike, loads as a sandboxed `libriscv` ELF binary. None of it
ever runs as code the host process links or interprets directly.

## Motivation

Keep `zone-server-h2o` itself, the deployed process, down to the fewest
components possible. This is why the CastSpell FFI reuses `RFD 0010`'s
existing bitpacked struct instead of adding FlatBuffers or protobuf.
This is also why the manifest's JSON-LD processing runs in an offline
authoring tool and never inside the deployed server. It is also why
the server-side manifest mechanism stays a small, purpose-built ELF
section, not an imported convenience API.

## Proposal

### Single-file package, embedded manifest

Keep the package a single `.elf` file, not an ELF plus a side-car
manifest file. Embed the manifest, the declared helper functions and
host capabilities a package needs, its entry points, and a version, as
metadata inside the ELF itself. `libriscv`/`godot-sandbox` already
loads a single `.elf` per package for its own Godot-side use. This
keeps `zone-server-h2o`'s loader shaped the same way. It also avoids a
second file the host must locate, version, and keep paired with the
right ELF.

`libriscv`/`godot-sandbox` itself carries no existing static,
pre-execution ELF metadata to reuse, confirmed by reading its own
`program/cpp/docker/api/api.hpp`. Its `ADD_API_FUNCTION` and
`SANDBOXED_PROPERTIES` macros do not write to a custom ELF section or
an ELF note at build time. `ADD_API_FUNCTION` expands to
`add_sandbox_api_function`, which calls `sys_sandbox_add`. This is a
syscall the guest program issues to the host at runtime, during its own
initialization, after the host already started running it.
`SANDBOXED_PROPERTIES` expands to a plain exported symbol, an
`extern "C" const Property properties[]` array, which still needs the
host to run the program (or at minimum resolve its symbol table) to
read.

Neither mechanism fits `zone-server-h2o`'s manifest. Both require the
host to already trust and run, or at least link against, the guest
program before learning what it declares. A hostile package could
misreport its own capabilities through `sys_sandbox_add`, or skip the
call outright, and the host would have already started executing it
by then.

`zone-server-h2o`'s manifest exists specifically to let the host
decide whether to run a package at all, before any guest code runs. It
needs its own genuinely static mechanism. That mechanism is a real
custom ELF section, or an ELF note the host parses before the sandbox
ever starts the program. It does not borrow a design from
`godot-sandbox`'s trusted-content, Godot-side convenience API.
`zone-server-h2o` treats the embedded manifest, not the ELF's code
alone, as the unit the host validates and grants capabilities to.

### Runtime FFI stays the bitpacked struct format

The sandbox FFI boundary between the host and each loaded package
reuses the same manually written, bitpacked struct format
`zf_zonetick.c` already stores entities in. It does not add a third
binary format such as FlatBuffers or protobuf alongside it. The
project keeps exactly two encodings, not four: this bitpacked struct
format for every nasty-layer surface (the zone tick and the CastSpell
FFI alike), and CBOR-LD, below, for every cheap-layer surface.

A host binary and a sandboxed package binary still come from separate
builds, often from separate sources (a designer's GDScript compile, a
future third-party mod). A reader compiled against an older struct
layout still needs a way to detect a newer, incompatible writer.

Solve that with an explicit ABI version field inside the manifest
(below), not with a self-describing runtime format. The host reads
that field. It refuses to load a package whose declared ABI version
does not match the struct layout the host itself uses.
`RFD 0010` already accepted the matching tradeoff for the zone tick:
manual struct versioning instead of a self-describing format. This
extends the same discipline to the CastSpell FFI. It avoids a second
binary format and a `flatc` build dependency that would solve the
same problem a different way.

### Manifest encoding: CBOR-LD

The manifest itself, separately from the bitpacked-struct runtime
data it describes, is a good fit for CBOR-LD, decoding to plain
JSON-LD, itself plain JSON. Two properties argue for that choice over
a bespoke manifest format. `zone-server-h2o` and its neighboring repos
already use MCP tooling (`vsekai-godot-mcp`), and MCP's own wire
format is JSON-RPC. A manifest that decodes straight to JSON needs no
translation layer to list, validate, or inspect through an MCP tool.

Second, a manifest's capability declarations name concepts a package
needs from the host. JSON-LD's context mechanism gives those concept
names a shared, linkable vocabulary across packages and across repos.
CBOR-LD's designers built it for that same property, in
verifiable-credential use cases. This is a narrower, metadata-only use
of CBOR-LD. It does not touch the entity or effect data itself, which
stays bitpacked-struct-encoded for the reasons above.

### Determinism requirement, for `aria-storage`

Record this determinism requirement as settled, not hypothetical.
Packages, and their embedded manifests, land in `aria-storage`, this
project's own casync-based content-addressed store. Content-addressed
storage needs a stable hash for the same logical package every time it
builds. The base CBOR-LD 1.0 specification is not enough by itself for
that. It defines no canonical or deterministic encoding of its own. It
reuses JSON-LD's processing determinism for term mapping only, not RFC
8949's byte-level deterministic CBOR rules (shortest-form integers, no
indefinite lengths, lexicographically sorted map keys).

RFC 8949 section 4.2's deterministic CBOR rules, layered on top of
CBOR-LD's compression, already give `aria-storage` what it needs.
Identical bytes in give identical `casync` chunks out, for the same
manifest. `w3c.github.io/vc-barcodes` uses a fuller pattern instead, a
fixed term registry plus an explicit hash computed over a defined set
of fields. That pattern solves a different problem: proving a
credential's signature to a verifier, not deduplicating stored
content. This RFD does not adopt that fuller pattern here. RFC 8949
determinism alone is the requirement for `aria-storage`.

### Implementation constraint: pure C at runtime

Record one implementation constraint alongside the encoding choice.
`zone-server-h2o` builds under Fil-C, a memory-safe C toolchain, not
Rust or C++. The mainstream CBOR-LD ecosystem runs on JavaScript,
Java, and Rust instead. No mainstream, production-grade C library
implements the full W3C CBOR-LD compression algorithm today.

Reject a hand-maintained, fixed term-to-integer table as a substitute
for real CBOR-LD, even though it would run in pure C with no
dependency added. A fixed table encodes plain CBOR with a private
dictionary, not CBOR-LD. It drops JSON-LD's actual context processing.
This RFD picked CBOR-LD for that processing in the first place. It
gives a shared, linkable vocabulary any conformant JSON-LD tool can
decode, not only this project's own tooling.

`w3c.github.io/vc-barcodes`
still uses a real, registered CBOR-LD term mapping, not an ad hoc one
invented outside the spec. A private table here would not match that
precedent either.

Split the manifest pipeline across two roles instead, so a real
JSON-LD processor never has to run inside `zone-server-h2o`'s own
pure-C runtime process. At package-build time, alongside
`godot-sandbox-gdscript-compiler`'s own compile step, a separate
authoring tool assembles `jsonld-cpp` (a C++14, spec-compliant W3C
JSON-LD 1.1 processor) with `QCBOR`. That tool writes the W3C CBOR-LD
compression algorithm's term-mapping step by hand, on top of
`jsonld-cpp`'s real context processing. It produces a genuinely
spec-compliant CBOR-LD manifest as build output. This tool runs
offline, outside the deployed server, so its C++ dependency never
touches `zone-server-h2o`'s own build or runtime.

### `QCBOR` over `zcbor`

Pick `QCBOR` over `zcbor` for both the authoring tool and the in-host
decoder, on production track record. Qualcomm open-sourced `QCBOR` in
2018, and its stable 1.x line stayed production-stable for years.
Arm's `t_cose` and `ctoken` already run it in COSE, CWT, and EAT
attestation-token implementations, with a Trusted Firmware-M port.
`zcbor` sees real production use too, in MCUboot, Zephyr's `mcumgr`
and LwM2M stacks, and the nRF Connect SDK. It stays newer, though,
and more confined to the Nordic/Zephyr ecosystem specifically. Given a
choice, pick the option with more hours of production usage behind it.

Resolve `QCBOR`'s earlier-noted determinism gap without waiting on
`QCBOR`'s development branch. That gap is that its stable 1.x release
does not sort map keys. The manifest comes from a fixed, hand-written
schema, not a runtime-built, unordered map. The authoring tool always
writes fields in the same fixed order in code. This is the
same manual-discipline pattern `RFD 0010` already accepted for the
zone tick's struct layout, instead of a self-describing format. Fixed
field order in code gives deterministic bytes without needing
`QCBOR`'s own generic map-key-sorting feature at all.

At load time, inside `zone-server-h2o` itself, the host only decodes
already-produced CBOR-LD bytes. It never re-runs JSON-LD context
resolution. Decoding plain CBOR back out needs no JSON-LD processor at
all. The pure-C runtime constraint holds here as a result. Use `QCBOR`
inside the host process, in pure C under Fil-C. Read the manifest's
fields once a package build already produced them.

This keeps the manifest genuinely CBOR-LD. It keeps the C++ dependency
confined to an offline authoring tool this project's deployed binaries
never link. It keeps the deployed runtime pure C throughout.

Also rejected: `gitlab.com/coswot/cborld-c`, the one existing pure C,
batteries-included implementation of the full CBOR-LD compression
algorithm, from the CoSWoT research group. This is the one path that
would have kept even the offline authoring tool free of a C++
dependency. It ships under the CeCILL v1.1 license, a strong-copyleft
license, which conflicts with this project's preference for permissive
licensing elsewhere (see `iovisor/ubpf` in
`rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps/index.md`). It also reads
as a research and embedded prototype rather than a vetted production
dependency. Revisit it only if `jsonld-cpp` plus `QCBOR` turns out not
to fit the offline authoring tool well in practice.

One further risk stays recorded, not resolved, on the offline
authoring tool. It may make sense for that tool itself to run inside a
sandbox someday, the same protection CastSpell effects get. Doing so
now would create a chicken-and-egg problem, though. The tool exists to
produce the packages the sandbox infrastructure loads. Sandboxing the
tool first needs the sandbox infrastructure the tool has not finished
producing yet. Leave the authoring tool unsandboxed for now, and
revisit this once task I's sandbox path is itself proven.

### Naming the pattern: Cheap or Nasty

Name the design principle behind the bitpacked-struct-versus-CBOR-LD
split, rather than leave it implicit: the "Cheap or Nasty" pattern
from the ZeroMQ guide (`zguide.zeromq.org`, chapter 7). That pattern
splits any protocol into two layers. A cheap layer is
self-describing, synchronous, low-volume, and tolerant of frequent
change. A nasty layer is hand-optimized binary, asynchronous,
high-volume, and resistant to change. The guide warns against
compromising between the two inside one format, since the tradeoffs
each layer needs run in opposite directions.

This project already applies that split once, at the transport level.
`decisions/20260612-fabric-channels-as-reliability-classes.md` gives
`CH_MIGRATION` reliable-ordered delivery for control and state, and
`CH_INTEREST` unreliable delivery for transforms, so control traffic
and high-volume data traffic never share one reliability model. The
manifest-versus-runtime-data split above extends the same principle
one layer down, to the encoding itself. CBOR-LD serves the cheap
layer, low-volume manifests an MCP tool might inspect. The bitpacked
struct serves the nasty layer instead. It carries the zone tick and
the CastSpell FFI data, which a host and a sandboxed package exchange
many times a second.

Protobuf and FlatBuffers both sit outside this project's two chosen
formats. Protobuf's build-time compiler step duplicates what CBOR-LD
already gives the cheap layer. FlatBuffers' schema evolution
duplicates what the manifest's ABI version field already gives the
nasty layer. Neither earns a third format's added build dependency.

## Recommendation and next steps

1. Implement the manifest's ELF section layout and its fixed field
   order, matching `RFD 0010`'s own manual-struct-versioning
   discipline.
2. Build the offline authoring tool (`jsonld-cpp` plus `QCBOR`) as a
   separate CMake target, confirmed never linked into
   `zone-server-h2o`'s own binary.
3. Build the in-host `QCBOR` decoder path inside `zone-server-h2o`,
   reading only already-produced manifest bytes.
4. Wire the ABI version check into the package loader, refusing any
   package whose declared version does not match the host's own
   struct layout.

## Open questions and verification

Studied: `jsonld-cpp` (`dcdpr/jsonld-cpp` on GitHub) is a real, standard
CMake C++ project, BSD-3-Clause licensed, depending only on a normal
toolchain (`make`, `cmake`, `g++`, `libssl-dev`). It carries a real
maintenance risk, though. It has 8 stars, 1 open issue, 0 open pull
requests, and no push since May 2024.

Accept this risk. The offline authoring tool keeps `jsonld-cpp` fully
isolated from `zone-server-h2o`'s own runtime and build. A stale or
eventually-abandoned dependency here costs a build-tool maintenance
burden, not a deployed-server one. Confirm the offline tool's own
`CMakeLists.txt` never becomes part of `zone-server-h2o`'s own CMake
target. Revisit `jsonld-cpp` if it goes fully unmaintained before task
I lands.

Recorded, not resolved: the offline authoring tool may deserve sandboxing
itself someday, the same protection CastSpell effects get. Sandboxing
it before task I's own sandbox path proves out creates a
chicken-and-egg problem. The tool would need to run inside the very
sandbox infrastructure it exists to produce packages for. Leave it
unsandboxed until task I's sandbox path is itself proven, then revisit.

Resolved: `QCBOR` is the pick over `zcbor`, for both the authoring tool
and the in-host decoder, on production track record (Qualcomm, 2018,
and Arm's `t_cose`/`ctoken`, Trusted Firmware-M). The manifest's fixed,
hand-written schema resolves `QCBOR`'s map-key-sorting gap without
waiting on its development branch. The authoring tool always writes
fields in the same fixed order in code, giving deterministic bytes
without needing a generic sorting feature at all.

Resolved: the manifest lives inside a single `.elf` file, as embedded
metadata, not a separate side-car file, matching how
`libriscv`/`godot-sandbox` already ships one `.elf` per package. This
RFD also records that `libriscv`/`godot-sandbox` itself has no existing
static ELF metadata mechanism to reuse. Its `ADD_API_FUNCTION` and
`SANDBOXED_PROPERTIES` macros both need the host to run or link the guest
program first, a syscall and a resolved symbol respectively. Neither is
usable for a manifest meant to gate execution before any guest code runs.
`zone-server-h2o` designs its own static ELF section or ELF note for
this, rather than reuse either godot-sandbox mechanism.

Resolved: manifests need write-once, content-addressed storage, not
hypothetically. Packages and their manifests land in `aria-storage`, this
project's own `casync`-based content-addressed store. RFC 8949 section
4.2's deterministic CBOR rules, layered on top of CBOR-LD, already give
`aria-storage` what it needs for stable dedup. `w3c.github.io/vc-barcodes`'s
fuller, hash-over-fields pattern solves a different problem, proving a
credential's signature. This RFD does not adopt it here.

Resolved: the sandboxed-CastSpell approach does need this follow-up RFD.
The scope differs between a client-side sandbox and a server-side one
running under load, and this document is that follow-up.
