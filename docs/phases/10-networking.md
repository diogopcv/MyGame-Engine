# Phase 10 — Networking & multiplayer

> Status: Expanded **ahead of schedule** 2026-07-14 (full pre-expansion
> requested, deliberately overriding the template's just-in-time rule).
> Re-validate scope and tech decisions against the codebase when this phase
> actually begins — this doc leans hardest on systems (ECS, reflection,
> physics) that intervening phases may reshape. Not started.

## Goal
Authoritative-server multiplayer: UDP transport with a reliability layer,
session management, reflection-driven state replication with snapshot +
delta compression, client-side prediction with server reconciliation, basic
lag compensation, and RPCs — two clients sharing a server simulation with
smooth, predicted movement.

## Scope
- In scope: platform UDP sockets (Winsock behind `platform/socket.h`, raw
  `std::` types + `void*` handles per golden rules 2/3); a reliability
  layer over UDP (Gaffer-style sequence numbers + 32-bit ack bitfield,
  resend on timeout, unreliable-sequenced and reliable-ordered channels);
  connect/disconnect/timeout session management with a per-client state
  machine; reflection-driven replication of registered component types
  (Transform first) keyed by a `NetId` component; server snapshots at a
  fixed send rate, delta-encoded against each client's last-acked snapshot;
  client-side prediction of the local player capsule + reconciliation
  replay; interpolation buffering for remote entities; a position-history
  ring for basic lag-compensated queries; a small RPC/message registry;
  an in-process fake transport with loss/reorder/duplication injection for
  tests; client and server as modes of the sandbox exe (CVar/args).
- Out of scope (deferred): NAT traversal, matchmaking, encryption/DTLS,
  and any internet-facing hardening (LAN/localhost scope); interest
  management / spatial relevancy (two clients see everything); bandwidth
  prioritization; TCP fallback; server browser; voice; cross-version
  compatibility (protocol version handshake rejects mismatches, nothing
  more); full physics-world rollback (only the predicted body replays —
  see Tech decisions).

## Module layout
`platform` addition: `include/platform/socket.h` + `src/socket_win32.cpp`
— non-blocking UDP open/close/sendto/recvfrom + address struct; Winsock
confined to the .cpp.

New `engine/net` module (`ge_net`), **Layer 3 (Subsystems)**, depending on
`ge_core` + `platform` only — never sideways on `ge_world`/`ge_physics`.
Replication of *components* sounds like it needs the ECS, but it doesn't:
`ge_net` speaks `TypeInfo*` + byte pointers (Phase 2 reflection), and the
app-layer glue enumerates world entities into that shape (same isolation
pattern as anim/gfx palettes).

- `include/net/transport.h` + `src/transport.cpp` — the reliability layer
  over an abstract `PacketIo` interface (function pointers: send/recv), so
  the real socket and the test fake are interchangeable. Sequence/ack
  bookkeeping, resend queue, RTT estimate, channels.
- `include/net/connection.h` + `src/connection.cpp` — handshake
  (hello/challenge/accept, protocol version), keepalive, timeout, per-peer
  connection state.
- `include/net/replication.h` + `src/replication.cpp` — replicated-type
  registry (`TypeInfo*` + net type id), snapshot writer/reader (per-entity:
  NetId, type bits, field data), delta encode/decode vs. a baseline
  snapshot (per-field memcmp dirty bits → only changed fields on the
  wire), baseline storage per client keyed by acked snapshot id.
- `include/net/prediction.h` + `src/prediction.cpp` — input-command ring
  (sequence-stamped), reconciliation bookkeeping (last server-acked input,
  replay window), interpolation buffer (snapshot pairs + render-time
  offset), position-history ring for lag-compensated rewind queries.
- `include/net/rpc.h` — message registry: reflected message structs +
  handler function pointers, sent through the reliable channel.

App-layer glue (sandbox): `net_server_system` (gather world state →
snapshots; apply client inputs to the player capsules; tick at the fixed
rate) and `net_client_system` (send inputs; apply snapshots; predict the
local capsule; interpolate remotes). Lives in the sandbox like all glue
(ADR-0009 precedent).

## Tech decisions
- **Authoritative server, fixed 60 Hz sim, 20 Hz snapshot send rate**
  (constants, tunable via CVars). Client sends input commands every fixed
  tick, stamped with tick sequence.
- **Reflection-driven replication** — the Phase 2 keystone paying off: a
  type is replicated by registering its `TypeInfo`; the snapshot/delta
  writer walks fields generically. No per-type hand-written packers unless
  profiling demands specialization.
- **Delta against last-acked, not last-sent** — the server keeps per-client
  baselines only for snapshot ids the client confirmed, so loss never
  desyncs the delta chain; a client too far behind gets a full (keyframe)
  snapshot.
- **Prediction replays the local body only.** On a corrective snapshot the
  client rewinds its capsule to the server state and re-applies unacked
  inputs through `PhysicsWorld` stepping — accepting that other dynamic
  bodies aren't rewound (their divergence is corrected by their own
  interpolation). Full-world rollback is deliberately out of scope.
- **Determinism stance: convergence, not bit-exactness.** Same-machine
  float determinism is assumed (same build, same CPU); the tests assert
  prediction *converges within tolerance* rather than bit-equal state, so
  the phase doesn't stall on FP-model rabbit holes. Revisit only if
  visible corrections persist.
- **Interpolation delay ~100 ms** (2 snapshot intervals) for remote
  entities: render remotes between the two buffered snapshots.
- **Lag compensation, minimal:** a ring of per-tick positions
  (~1 s); a rewind query returns where an entity was at (server_time −
  client_rtt/2) for hit-style raycasts. No full rewound-physics-world.
- **Test transport is first-class:** the fake `PacketIo` with seeded
  loss/reorder/duplication drives most of the suite in-process; real
  sockets appear only in a loopback smoke test.
- **ADR:** the replication model (reflection-driven, acked-baseline delta,
  predict-local-only) is cross-cutting — record as an ADR at phase start.

## Design
Server, per fixed tick:

```
recv -> transport -> {input cmds -> apply to that client's player capsule}
PhysicsWorld::step + gameplay/AI systems (the one true simulation)
every 3rd tick: for each client:
    world glue -> entity/component byte views -> snapshot
    delta vs last-acked baseline (or keyframe) -> transport -> send
```

Client, per fixed tick:

```
sample input -> stamped command -> send (also into the local input ring)
predict: apply own input to the local capsule, step it
on snapshot: apply authoritative state to remotes' buffers;
    if local capsule differs beyond tolerance:
        rewind to server state, replay unacked inputs (reconciliation)
render frame: remotes interpolated at t - 100ms; local = predicted state
```

## Milestones
1. **Sockets + transport.** `platform/socket.h`; reliability layer over
   the fake `PacketIo`; soak tests under seeded loss/reorder/dup; loopback
   echo smoke over real UDP.
2. **Sessions + RPC.** Handshake, timeout, version check; message registry;
   two sandbox instances connect and exchange chat-style RPCs.
3. **Snapshot replication.** Registry + snapshot/delta round-trip; server
   + client modes in the sandbox; client renders server-owned moving
   entities (interpolated) — no prediction yet, input runs server-side
   only.
4. **Prediction + reconciliation.** Input ring, local capsule prediction,
   reconciliation replay; convergence baseline test; playable two-client
   movement on localhost.
5. **Lag compensation + polish + sign-off.** History ring + rewind query
   (demoed via a raycast "tag" action); stats overlay (RTT, loss, resend,
   snapshot bytes — Phase 8 UI/ImGui); manual two-client play session;
   phase DoD sign-off.

## Test plan
Per plan.md's Phase 10 critical-test line:
- **Unit (transport):** ack bitfield bookkeeping (exact ack sets under
  crafted sequences); resend fires exactly for unacked reliable packets;
  duplicate delivery is suppressed; out-of-order unreliable-sequenced
  drops stale packets; a seeded 1000-packet soak at 20% loss + reorder
  delivers every reliable message exactly once, in order.
- **Unit (replication):** snapshot round-trip — write a set of reflected
  components, read back, bit-equal fields; delta round-trip — baseline +
  delta reproduces the target exactly, unchanged fields absent from the
  wire; keyframe fallback when the baseline is unknown; unknown-type and
  truncated-buffer inputs rejected gracefully (no asserts across the
  trust boundary).
- **Unit (prediction bookkeeping):** input ring stamps/evicts correctly;
  the replay window after an ack contains exactly the unacked inputs.
- **Baseline (convergence):** scripted-input client over the fake
  transport with loss/latency — post-reconciliation position matches a
  server-only run of the same inputs within tolerance after every
  correction window; client/server capsule paths agree within tolerance
  end-to-end (the plan's "determinism" line, as convergence).
- **Smoke:** two in-process peers over real loopback sockets: connect,
  replicate N ticks, disconnect cleanly.
- **Manual:** two-client feel pass — corrections invisible at localhost
  RTT, remotes smooth. Recorded sign-off.

## Open questions
- Where entity *creation/destruction* replication lives (in scope
  implicitly — snapshot set membership implies create/destroy; the open
  part is spawn-data payloads for non-component state) — resolve in
  Milestone 3.
- Whether AI (Phase 9) ticks server-side only from day one (almost
  certainly yes — it's the authority model) — confirm when wiring
  Milestone 3's demo scene.
- Clock sync: snapshot-id-based timing may suffice at LAN RTTs; add an
  explicit offset estimator only if interpolation stutters.

## Cross-phase watch items
- This phase is why ADR-0007/0008 stayed "replication-aware": if field
  enumeration or component iteration turns out to be awkward here, the fix
  belongs in reflection/ECS, not in workarounds inside `ge_net`.
- Snapshot bytes/tick is the first real serialization-perf pressure;
  measure before optimizing (bit-packing, quantization are all deferred
  until the stats overlay shows a number that matters).

## Depends on
- Phase 2 (reflection/serialization — the replication substrate; jobs not
  required).
- Phase 4 (ECS — entity/component enumeration via app glue; `NetId`
  component).
- Phase 5 (physics — the predicted capsule; `PhysicsWorld` stepping for
  replay).
- Phase 8 (UI — the net stats overlay; ImGui acceptable).
