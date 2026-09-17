# JOJO Recompiled — R2.5 Direct Session Reconnect & Telemetry Design

## Goal

Extend the existing direct UDP session increment with deterministic peer liveness, bounded reconnect behavior, and connection telemetry without expanding scope into matchmaking, accounts, relay/NAT traversal, ranked services, encryption, or production online claims.

## Status policy

This increment remains `implemented-unverified`. Synthetic loopback tests and CI may verify the generic transport contract, but they do not establish commercial-game online compatibility or complete M9.

## Scope

### Liveness

`DirectUdpSession` owns a caller-driven heartbeat clock. While connected, the session periodically sends a `ping` control packet to the already pinned peer. Any valid packet received from that peer refreshes the last-receive timestamp. Traffic from an unpinned endpoint is ignored and does not refresh liveness.

### Reconnect transition

If no valid peer traffic is observed for the configured liveness timeout, the session transitions from `connected` to `reconnecting` and `NetworkTelemetry::state` follows that transition. Gameplay packets must not be delivered while reconnecting.

The session must retain the previously pinned peer endpoint. Reconnect attempts are limited to that endpoint; a third party cannot replace or redirect the peer during reconnect.

### Reconnect handshake

The client reuses the existing `session_hello` / `session_accept` control protocol against the pinned host endpoint. The host accepts a reconnect hello only from its already pinned client endpoint. A successful handshake restores `connected` state and refreshes liveness timestamps.

Control retransmission continues through `ControlReliabilityQueue`. Reconnect logic must not mutate authoritative gameplay state.

### Reconnect timeout

If reconnect does not complete within the configured reconnect deadline, the session transitions to `disconnected`. After this terminal transition, incoming datagrams cannot revive the session automatically.

### Telemetry

Existing RTT/jitter calculation remains driven by ping/pong samples. Liveness-triggered reconnect attempts increment loss telemetry only for heartbeat probes that age past the liveness boundary; ordinary ignored/spoofed traffic is not counted as peer traffic.

The session exposes no new global service state. All behavior remains local to the direct-session state machine.

## Configuration

`DirectUdpSession::bind` gains explicit caller-configurable timing values with conservative defaults:

- control retry interval: 100 ms (existing default);
- heartbeat interval: 500 ms;
- liveness timeout: 2000 ms;
- reconnect timeout: 5000 ms.

Configuration must reject zero heartbeat/liveness/reconnect intervals and reject a liveness timeout shorter than the heartbeat interval.

## State machine

### Client

`idle -> connecting -> connected -> reconnecting -> connected | disconnected`

Explicit local disconnect from `connected` remains terminal `disconnected`.

### Host

`idle -> connected -> reconnecting -> connected | disconnected`

The host never selects a different peer after the first accepted hello unless a new session object is created.

## Packet handling rules

- `input` and other gameplay/data packets are delivered only in `connected`.
- `ping`, `pong`, `session_hello`, `session_accept`, and `disconnect` remain session-owned control packets.
- Valid control packets from the pinned peer may advance reconnect state.
- Datagrams from other endpoints are ignored before telemetry/liveness accounting.
- Explicit `disconnect` from the pinned peer is terminal and bypasses reconnect.

## Error handling

Socket I/O errors continue to surface as `Result` failures; they do not silently rewrite state. Invalid timing configuration returns `ErrorCode::invalid_argument`. A failed reconnect send leaves the session in `reconnecting` so caller-driven polling can retry until the deadline.

## Tests

The transport contract must add deterministic loopback coverage for:

1. heartbeat emission and pong RTT update;
2. silence causing `connected -> reconnecting`;
3. gameplay suppression while reconnecting;
4. reconnect with the same pinned peer returning both sides to `connected`;
5. spoofed peer traffic not refreshing liveness or taking over the endpoint;
6. reconnect deadline causing terminal `disconnected`;
7. invalid timing configuration rejection;
8. existing handshake, gameplay round-trip, spoofing, and explicit disconnect behavior continuing to pass.

Linux and Windows CI must run the standalone R2.5 contract plus the repository CTest suite and readiness gate.

## Non-goals

- public rooms or invitations;
- matchmaking, ranked, profile/history/replay services;
- NAT traversal or relay infrastructure;
- encryption/account identity;
- game-specific online integration;
- changes to rollback simulation ownership;
- promotion of R2.5 to `verified`.
