# M8 Rollback Networking Core Design

## Goal

Close the M8 roadmap contract with a portable deterministic rollback core and UDP-oriented datagram protocol that can later be bound to platform sockets or an online service without allowing networking code to own gameplay state.

## Scope

M8 owns:

- deterministic frame stepping with one local and one remote input;
- snapshot capture and restoration around simulated frames;
- late-input rollback and deterministic re-simulation;
- deterministic state hashes and desync detection;
- deterministic RNG with explicit serializable state;
- side-effect suppression during rollback re-simulation;
- a compact versioned datagram packet format suitable for UDP;
- unreliable input/ping traffic and reliability only for session/control packets;
- retransmission/ack bookkeeping driven by caller-supplied monotonic network time;
- telemetry for RTT, jitter, packet loss, prediction, rollback depth and connection lifecycle.

M8 does not own matchmaking, accounts, relay/NAT traversal, lobby UI, production socket threading, encryption/key exchange or the commercial game's state adapter. Those belong to later product/integration work.

## Chosen architecture

Three approaches were considered:

1. **OS socket implementation inside the rollback engine.** This proves actual UDP calls early, but couples deterministic simulation tests to WinSock/POSIX lifetime and makes the core harder to reuse.
2. **Pure rollback with no wire protocol.** This is very testable, but leaves the roadmap's transport/reliability/telemetry contract unfinished.
3. **Deterministic rollback core + portable datagram protocol/reliability layer + transport boundary.** This keeps gameplay state isolated while fully defining what must travel over UDP and which packets may be retransmitted.

M8 uses approach 3. A platform host may send/receive the produced byte datagrams through UDP without changing simulation code.

## Rollback simulation contract

`src/core/rollback.h` defines:

- `RollbackInput`: deterministic packed buttons plus two signed axes;
- `IRollbackSimulation`: `save_state()`, `load_state(bytes)` and `step_frame(local, remote, emit_side_effects)`;
- `RollbackSession`: frame ownership, snapshots, predictions, late remote input repair and state hashes;
- `DeterministicRng`: platform-identical integer generator with explicit state access.

A normal frame is processed as follows:

1. Capture the simulation state at the start of the frame.
2. Select the exact remote input if already received; otherwise reuse the most recent remote input and mark the frame predicted.
3. Call `step_frame(local, remote, true)`.
4. Hash the resulting state and store the hash under the completed frame index.
5. Retain only the rollback window plus the current state.

When a late remote input differs from the prediction used for an already simulated frame, `RollbackSession` restores the snapshot from the start of that frame and replays through the latest completed frame. Every replayed call uses `emit_side_effects=false`; only normal forward simulation may emit audiovisual or other one-shot effects.

A late input older than the retained rollback window is rejected instead of mutating gameplay state with incomplete history.

## State hashing and desync

After every completed frame, M8 hashes the exact `save_state()` byte sequence using the existing SHA-256 implementation. The session exposes local hashes by frame and accepts remote hash reports. If both sides provide a hash for the same frame and the values differ, the session records the earliest detected desync frame.

Hashing is diagnostic and deterministic; it is not treated as a cryptographic trust or anti-cheat boundary.

## Deterministic RNG

`DeterministicRng` uses a fixed integer-only algorithm with exactly specified 64-bit wraparound behavior. The RNG state can be read and restored, so a game-state adapter can include it in snapshots. No wall clock, platform random source or floating-point operation participates in generation.

## Datagram protocol

`src/core/network_protocol.h` defines a versioned `NetworkPacket` and deterministic serializer/parser.

Packet kinds:

- `input`: frame-numbered rollback input; unreliable;
- `ping` / `pong`: timestamp telemetry; unreliable;
- `session_hello` / `session_accept`: reliable control;
- `disconnect`: reliable control.

Every packet carries protocol version, packet kind, sequence, acknowledgement, frame index and timestamp. Input packets additionally carry the packed rollback input; control packets may carry a bounded UTF-8/opaque payload.

The parser rejects bad magic/version/kind, truncated fields and oversized payloads. Serialization uses fixed little-endian integer fields and no compiler struct layout.

## Selective reliability

`ControlReliabilityQueue` accepts only packet kinds classified as reliable control. It assigns/retains sequences, removes acknowledged packets and returns due retransmissions after a configured interval. Input, ping and pong packets are explicitly rejected from the reliable queue, preventing head-of-line blocking from turning rollback inputs into a TCP-like stream.

Time used by reliability is caller-supplied monotonic network time in milliseconds and never simulation time.

## Telemetry and lifecycle

`NetworkTelemetry` tracks:

- latest RTT sample;
- jitter as the smoothed absolute delta between consecutive RTT samples;
- packets sent/received/lost and packet-loss percentage;
- predicted frame count;
- last and maximum rollback depth;
- `connected`, `reconnecting` or `disconnected` lifecycle state.

No API labels a connection "zero latency". RTT, jitter and loss are measurements of external network conditions.

## Error handling

Public operations use the repository `Result<T>` contract for invalid arguments, malformed datagrams, snapshots that cannot be restored and rollback requests outside the retained window. Invalid remote data must not partially mutate the simulation.

## Files

Create:

- `src/core/rollback.h`
- `src/core/rollback.cpp`
- `src/core/network_protocol.h`
- `src/core/network_protocol.cpp`
- `tests/test_rollback.cpp`
- `docs/superpowers/plans/2026-08-30-rollback-networking-core.md`
- `docs/superpowers/plans/2026-08-30-rollback-networking-core-verification.md`

Modify:

- `CMakeLists.txt`
- `docs/architecture/PRODUCTION-ROADMAP.md`

A temporary Linux-only RED workflow may be created solely to prove the permanent test fails before production implementation; it must be deleted before integration.

## Test contract

The permanent M8 test must prove:

1. deterministic RNG repeats for identical seed and restores exact sequence from saved state;
2. forward simulation predicts missing remote input and marks telemetry;
3. a corrected late input restores the correct snapshot and re-simulates to the same final state as an authoritative run;
4. replay calls suppress side effects;
5. rollback depth is bounded and too-old corrections are rejected without mutation;
6. state hashes repeat for identical states and mismatching remote hashes report desync;
7. packet serialization is deterministic and round-trips all packet kinds;
8. malformed/truncated/oversized packets are rejected;
9. reliable queue accepts only session/control packets, acknowledges them and retransmits only when due;
10. RTT, jitter, loss, prediction, rollback-depth and lifecycle telemetry behave deterministically;
11. Linux and Windows/MSVC build and CTest the same portable core.

## Readiness boundary

M8 completion means the reusable rollback/networking core contract is implemented and cross-platform verified. It does not mean public internet matchmaking is live, UDP sockets are already hosted by the commercial game process, NAT traversal exists, or a commercial revision's full gameplay state has been proven deterministic end to end.