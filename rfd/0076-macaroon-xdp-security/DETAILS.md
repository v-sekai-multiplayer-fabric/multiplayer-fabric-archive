## Rationale

Directly validating a chained-HMAC Macaroon inside an XDP packet filter
is impossible and undesirable:

1. The eBPF verifier rejects it. The BPF verifier imposes strict limits
   on instruction count, loops, and stack size. HMAC-SHA256 with caveat
   chaining requires hundreds of instructions and variable-length parsing,
   so the verifier would reject the program.

2. Parsing complex tokens at the network interface destroys the sub-10µs
   latency target from RFD 0002. A single Macaroon validation costs
   ~50-100µs (HMAC + caveat walk); at 60Hz × 200 entities per zone,
   that's 15,360 validations/sec per zone, or 1.5 seconds of CPU per
   second on crypto alone.

3. It is the wrong layer. Macaroons are authorization tokens (who can do
   what). XDP is a packet filter (which packets are allowed). Mixing
   authorization logic into packet filtering violates separation of
   concerns.

## Architecture

```
                    CONTROL PLANE (once per connection)
                    ─────────────────────────────────────

  Player ──connect──► Matchmaker
                        │ authenticate (OAuth, password, etc.)
                        │ issue Macaroon with caveats:
                        │   "zone_id = 45"
                        │   "server_ip = 192.168.1.50"
                        │   "expires < 5min"
                        │ generate 64-bit session_key
                        │
                        ▼
  Player ──hello(Macaroon, session_key)──► User-space orchestrator
                                            (weft-warp-loop zone server)
                        │
                        │ validate Macaroon HMAC signature
                        │ verify caveats (zone_id, server_ip, expiry)
                        │ if valid:
                        │   bpf_map_update_elem(WHITELIST_MAP,
                        │     key=player_ip,
                        │     value={session_key, zone_id, core_id})
                        │
                        ▼
                     WHITELIST_MAP (eBPF)
                    ┌─────────────────────────────┐
                    │ player_ip → session_key      │
                    │             zone_id          │
                    │             core_id          │
                    │             expiry_timestamp │
                    └─────────────────────────────┘

                    DATA PLANE (every packet, 60Hz)
                    ──────────────────────────────

  Player ──UDP(session_key, payload)──► NIC
                                        │
                                        ▼
                                     XDP program
                                        │ lookup src_ip in WHITELIST_MAP
                                        │
                                        ├── valid: route to core_id
                                        │         (zero crypto, ~10ns)
                                        │
                                        └── invalid: DROP
                                                   (DDoS, spoof, expired)
```

## Three components

### 1. Matchmaker (control plane, infrequent)

The Matchmaker is a central service (or distributed with consensus)
that:

- Authenticates the player (OAuth, password, device attestation)
- Determines target zone via load balancing + spatial locality
- Issues a Macaroon attenuated with caveats:
  - `zone_id = N` — player is assigned to zone N
  - `server_ip = A.B.C.D` — player must connect to this server
  - `expires < T` — token valid until timestamp T
  - `rate_limit = R` — max packets/sec from this player
- Generates a cryptographically random 64-bit session key
- Returns Macaroon + session_key + server address to player

The Matchmaker runs in user space with full access to crypto libraries.
It is not in the hot path — it runs once per connection (every ~5
minutes for session renewal).

### 2. User-space orchestrator (control plane, once per session)

The zone server (weft-warp-loop) receives the player's initial Hello
packet containing the Macaroon. In user space:

- Validate the Macaroon's HMAC signature against the root key
- Walk the caveat chain: verify zone_id matches a local zone, server_ip
  matches this server, expiry is in the future
- If valid: call `bpf_map_update_elem()` to insert the player's IP +
  session_key + zone_id + core_id into the eBPF WHITELIST_MAP
- If invalid: reject the connection

This is the bridge between cryptographic security (user space) and
kernel-bypass enforcement (eBPF). It runs once per session, not per
packet.

### 3. XDP/eBPF (data plane, every packet)

The XDP program intercepts every UDP packet at the NIC, before the
Linux kernel does any allocation or processing:

```c
// XDP program (runs at line rate, ~10ns per packet)
SEC("xdp")
int zone_router(struct xdp_md *ctx) {
    // Parse packet headers (IP + UDP + custom session header)
    uint32_t src_ip = parse_src_ip(ctx);
    uint64_t session_key = parse_session_key(ctx);

    // Lookup in whitelist map (BPF hash map, O(1))
    struct session_info *info;
    info = bpf_map_lookup_elem(&whitelist_map, &src_ip);
    if (!info)
        return XDP_DROP;  // unauthenticated — drop instantly

    // Validate session key
    if (info->session_key != session_key)
        return XDP_DROP;  // wrong key — spoofed or stale

    // Check expiry
    if (bpf_ktime_get_ns() > info->expiry_ns)
        return XDP_DROP;  // session expired

    // Route to the CPU core handling this zone
    // (via CPU steering or redirect to the right ring buffer)
    return route_to_core(info->core_id, ctx);
}
```

**No cryptographic operations in the XDP path.** Only a hash map lookup
and integer comparison. This executes in ~10-50 nanoseconds per packet.

## Why this works for 25M CCU

### Packet rate analysis

At 25M CCU, 60Hz tick rate, 200 entities/zone:

| Metric                       | Value                        |
| ---------------------------- | ---------------------------- |
| Total players                | 25,000,000                   |
| Packets/player/sec           | 60                           |
| Total packets/sec            | 1,500,000,000 (1.5 Gpps)     |
| Zones (200 entities each)    | 125,000                      |
| Packets/zone/sec             | 12,000 (200 entities × 60Hz) |
| XDP cost/packet              | ~50ns                        |
| XDP CPU/zone/sec             | 0.6ms (12,000 × 50ns)        |
| XDP CPU/core (16 zones/core) | 9.6ms/sec (1% of one core)   |

The XDP filter consumes <1% of a single core's CPU. The remaining 99%
is available for ZoneTick computation and FDB batch writes.

### DDoS resistance

A DDoS attack sends millions of spoofed packets. Without XDP, each
packet traverses the full kernel network stack (allocation, socket
lookup, conntrack, userspace wakeup) — ~5µs per packet, saturating
all cores.

With XDP, spoofed packets are dropped at the NIC in ~50ns. A 100 Gbps
NIC can process 150M packets/sec in XDP. At 50ns/drop, that's 7.5ms
of CPU/sec — one core handles the entire DDoS while the other 31 cores
run the game simulation uninterrupted.

### Session key entropy

64-bit session key: 2⁶⁴ = 1.8 × 10¹⁹ possible keys. At 1.5 Gpps
brute-force rate, expected time to forge one key: ~385 years. With
session expiry at 5 minutes, the attacker has 300 seconds × 1.5 Gpps =
4.5 × 10¹¹ attempts, giving a forgery probability of 2.5 × 10⁻⁸ —
negligible.

### WHITELIST_MAP memory

Each entry: 4 bytes (IP) + 8 bytes (session_key) + 4 bytes (zone_id)

- 4 bytes (core_id) + 8 bytes (expiry) = 28 bytes. For 25M entries:
  700MB. BPF maps support up to 2GB on modern kernels. Alternatively,
  use a per-CPU map (per-core whitelist) to reduce each map to ~44MB.

## Macaroon caveat design

```
location = "zf://zone-server.example.com"
identifier = "player-12345-session"
caveats = [
    "zone_id = 45",
    "server_ip = 192.168.1.50",
    "expires < 1718984700",
    "rate_limit = 120",        // max 120 packets/sec (2x 60Hz)
    "entity_id = 12345",       // player's entity slot in zone
]
discharge = HMAC-SHA256(root_key, location || identifier || caveats)
```

The user-space orchestrator verifies:

1. HMAC signature matches (token not forged)
2. `zone_id` is a zone hosted on this server
3. `server_ip` matches this server's IP
4. `expires` is in the future
5. `entity_id` is a valid slot in the zone's slotmap (`rfd/0080-slotmap-entity-storage`)

The `entity_id` caveat ties the Macaroon to a specific slotmap entry.
If the player's entity is destroyed (slot freed, generation bumped),
the session is invalidated — the orchestrator removes the entry from
WHITELIST_MAP on the next tick.

## Session lifecycle

```
1. Player authenticates → Matchmaker issues Macaroon (5min TTL)
2. Player sends Hello + Macaroon → orchestrator validates → WHITELIST_MAP update
3. Player sends UDP packets at 60Hz → XDP validates + routes (every packet)
4. At 4min30sec: orchestrator sends renewal prompt to player
5. Player re-authenticates → new Macaroon → WHITELIST_MAP update (refresh)
6. If player disconnects: orchestrator removes from WHITELIST_MAP on next tick
7. If Macaroon expires without renewal: XDP drops packets (expiry check in XDP)
```

## Relationship to other RFDs

- `rfd/0001-zonefabric-roadmap-vs-mas-bandwidth-fps` (zonefabric): XDP
  routes packets to the core handling each zone. The zone's slotmap
  (`rfd/0080-slotmap-entity-storage`) stores the entity state. XDP
  does not touch the slotmap; it only routes packets.

- `rfd/0080-slotmap-entity-storage`: The `entity_id` caveat in the
  Macaroon maps to a slotmap handle. When the entity is destroyed, the
  slotmap generation bump invalidates the session.

- `rfd/0072-actor-lite-worker-pool`: The user-space orchestrator runs
  on the H2O event loop. The worker thread that handles the Hello
  packet makes the `bpf_map_update_elem()` call.

- `rfd/0084-zstd-compression-for-zone-state`: This is orthogonal. zstd
  compresses FDB values, and XDP handles packet routing. They operate
  on different layers.

- mas-bandwidth/fps: Glenn Fiedler's architecture mentions XDP for
  packet processing and relays but does not detail authentication.
  This RFD fills that gap: Macaroons provide the authentication, XDP
  provides the enforcement.

## What this RFD does NOT cover

- Macaroon implementation: use libmacaroons (C library) or
  py-macaroons. The Matchmaker implementation is out of scope; this
  RFD defines the interface (issue, validate, attenuate) and the
  eBPF handoff.

- eBPF program compilation: the XDP program is compiled with
  clang/llvm to BPF bytecode and loaded with libbpf. This is a build-
  time concern, documented in the CI pipeline RFD.

- TLS/mTLS: the control-plane connection (Hello + Macaroon) uses
  TLS. The data-plane (UDP game packets) does not; XDP enforces
  authenticity via the session key, and game state is not sensitive
  (positions, velocities). Encryption of game packets is a future
  concern if anti-cheat requires it.

- Player-to-player security: this RFD secures the server from
  unauthenticated clients. It does not prevent an authenticated
  player from sending malicious game actions (aimbots, speed hacks).
  Server-side validation of game actions is a game-logic concern, not
  a network-security concern.
