# R2.5 Online Session Controller — Design

Date: 2026-09-04
Status: proposed for implementation after user review
Scope: R2.5 user-facing direct-session product layer

## 1. Purpose

Add a UI-independent product controller above the existing `DirectUdpSession` transport so a future in-game Online screen can host or join a direct IPv4 session, expose stable connection/telemetry state, and exchange gameplay packets without knowing transport internals.

This package does not implement an Online screen. It creates the product-facing model that such a screen can consume later.

## 2. Context

R2.5 already provides real nonblocking IPv4 UDP transport, host/client handshake, peer pinning, heartbeat/liveness, reconnect, disconnect propagation and RTT/jitter/loss telemetry. Those capabilities currently live directly in `DirectUdpSession` and have no product-level session controller.

The current Win32 application UI is the first-run preparation flow, while `SettingsMenuSession` is limited to graphics/audio/controls. Online lifecycle therefore must not be attached to either of those surfaces.

## 3. Goals

The package must provide:

- one controller that owns one direct Online session at a time;
- explicit host and join entry points;
- dotted-decimal IPv4 plus port parsing suitable for a future direct-connect form;
- stable product states independent of transport enum details;
- local/remote endpoint exposure;
- role exposure (`host` or `client`);
- RTT, jitter, packet loss and packet counters through a view snapshot;
- deterministic caller-supplied time for connect, poll, reconnect and disconnect behavior;
- gameplay send/delivery only while product state is connected;
- validation retries that do not poison a healthy lifecycle state;
- reset/retry after terminal or operationally faulted sessions;
- clear errors suitable for future UI presentation.

## 4. Non-goals

This package must not claim or implement:

- matchmaking;
- casual/ranked backend services;
- public rooms or room discovery;
- invitations;
- accounts, profiles or history;
- NAT traversal or relay;
- DNS/hostname resolution;
- encryption/authentication;
- spectator mode;
- replays;
- game-specific commercial online integration;
- complete M9 UI.

The package also must not modify the first-run Win32 preparation screen or turn Online into a settings page.

## 5. Architecture

Create:

- `src/core/online_session.h`
- `src/core/online_session.cpp`
- `tests/test_online_session.cpp`

`OnlineSessionController` owns an optional `DirectUdpSession`. It translates product operations into transport operations and translates `DirectSessionState`/`NetworkTelemetry` into a stable `OnlineSessionViewState`.

The controller must not reimplement handshake, reliability, liveness, reconnect, spoof protection or telemetry math. Those remain owned by `DirectUdpSession`.

### 5.1 Product state

Define a product-facing state enum:

- `inactive` — no bound session;
- `waiting_for_peer` — host is bound and waiting for the first valid client hello;
- `connecting` — client handshake is in progress;
- `connected` — gameplay send/delivery is allowed;
- `reconnecting` — pinned-peer recovery is in progress; gameplay is suppressed;
- `disconnected` — session ended by peer, local disconnect or reconnect timeout;
- `faulted` — a transport/runtime operation failed and the current session must be reset before a new start.

Host-side `DirectSessionState::idle` maps to `waiting_for_peer`. Client-side `idle` is not exposed after a successful `join`; the controller calls `connect` as part of the join operation.

### 5.2 View snapshot

Expose an immutable snapshot structure containing at least:

- product state;
- optional role;
- optional local endpoint;
- optional remote endpoint;
- RTT in milliseconds;
- jitter in milliseconds;
- packet loss percentage;
- packets sent;
- packets received;
- packets lost;
- `can_send_gameplay`;
- last error code;
- last error detail.

`can_send_gameplay` is true only for `connected`.

The view snapshot is derived from controller/transport state. A UI must not infer state transitions itself.

## 6. Endpoint input contract

Provide a pure parser for direct IPv4 endpoint input. The accepted canonical user form is:

`A.B.C.D:PORT`

Rules:

- exactly four decimal IPv4 octets;
- every octet is 0–255;
- port is decimal 1–65535;
- no whitespace inside the address;
- no hostnames;
- no IPv6 in this package;
- malformed or out-of-range input returns `ErrorCode::invalid_argument` with a stable human-readable detail;
- canonical formatting emits dotted-decimal IPv4 plus decimal port.

This parser is platform-independent and does not perform network I/O.

## 7. Controller API behavior

The exact C++ spelling may be refined during implementation, but the behavior contract is fixed.

### 7.1 Host

`host(local_endpoint, timing)`:

- is valid only from `inactive`;
- binds `DirectUdpSession` as host;
- accepts port 0 for an ephemeral local port;
- on success enters `waiting_for_peer`;
- exposes the actual bound local endpoint returned by the transport;
- on bind failure enters `faulted` and records the error.

### 7.2 Join

`join(local_endpoint, remote_endpoint, timing, now_ms)`:

- is valid only from `inactive`;
- rejects remote port 0 before binding;
- binds as client and immediately starts `connect`;
- on success enters `connecting`;
- on bind/connect failure enters `faulted` and records the error;
- the remote endpoint becomes visible as soon as the connect attempt is accepted by the transport.

The future UI may call the endpoint parser first and then pass the parsed endpoint to `join`.

### 7.3 Poll

`poll(now_ms)`:

- delegates to `DirectUdpSession::poll`;
- returns gameplay/data packets delivered by the transport;
- refreshes the product view after every poll, including state and telemetry;
- maps transport `connected`, `reconnecting` and terminal `disconnected` states into the product state;
- preserves `waiting_for_peer` for a host whose transport remains idle;
- preserves `connecting` for a client whose initial handshake is still pending;
- on transport error enters `faulted`, records the error, and returns the failure.

The controller does not synthesize gameplay packets and does not own rollback state.

### 7.4 Send gameplay

`send(packet, now_ms)`:

- is accepted only while product state is `connected`;
- delegates packet-type validation and transport send to `DirectUdpSession`;
- is rejected during inactive/waiting/connecting/reconnecting/disconnected/faulted states;
- on transport I/O failure enters `faulted` and records the error;
- caller misuse such as sending while not connected returns `invalid_argument` without changing a healthy non-faulted lifecycle state.

### 7.5 Disconnect

`disconnect(now_ms)`:

- is accepted only while connected;
- delegates the disconnect control packet to `DirectUdpSession`;
- on success enters `disconnected`;
- invalid lifecycle use returns `invalid_argument` without converting the controller to `faulted`;
- transport I/O failure enters `faulted`.

### 7.6 Reset

`reset()`:

- is valid from every product state;
- destroys the owned `DirectUdpSession` when one exists;
- clears endpoints, role, telemetry snapshot and last error;
- returns the controller to `inactive`;
- allows a new host or join operation;
- may be used as a local cancel for `waiting_for_peer`, `connecting` or `reconnecting`.

Reset is local only; it must not pretend to notify a peer. A connected user must call `disconnect` first when graceful peer notification is desired.

## 8. Error model

Errors are divided into two categories.

### 8.1 User/lifecycle validation errors

Examples:

- malformed direct endpoint text;
- join target port 0;
- host/join while another session is active;
- send while not connected;
- disconnect while not connected.

These return a failure but do not turn an otherwise healthy controller into `faulted`. If the controller is still `inactive`, corrected input may be retried directly without calling `reset()`.

### 8.2 Operational errors

Examples:

- UDP bind failure;
- connect handshake send failure;
- polling socket error;
- gameplay send I/O error;
- disconnect send I/O error.

These enter `faulted`, preserve the error for the view snapshot, and require `reset()` before starting another session.

A remote disconnect or reconnect timeout is a normal terminal lifecycle result and maps to `disconnected`, not `faulted`.

## 9. Time and determinism

The controller must not read wall-clock time. All time-dependent methods accept `now_ms` from the caller and forward it to the transport.

Tests can therefore advance direct sessions deterministically while still using real loopback UDP sockets.

## 10. Testing

Add `tests/test_online_session.cpp` and register it with CTest.

Required coverage:

1. endpoint parser accepts canonical IPv4/port and rejects malformed/out-of-range forms;
2. host binds on loopback with ephemeral port and exposes `waiting_for_peer` plus actual local port;
3. client joins a host and both controllers reach `connected` through caller-driven polling;
4. view snapshots expose role, local/remote endpoints and gameplay gating correctly;
5. gameplay packet roundtrip succeeds only while connected;
6. liveness loss maps both transport/product state correctly into reconnect behavior;
7. successful pinned-peer recovery returns to connected;
8. reconnect timeout becomes normal `disconnected`;
9. explicit disconnect propagates to the peer and both sides become disconnected after polling;
10. send/disconnect misuse returns validation errors without incorrectly faulting the controller;
11. operational failures that can be deterministically induced enter `faulted` and preserve the error;
12. `reset()` clears terminal/faulted state, cancels non-terminal attempts locally, and permits a fresh session;
13. packets from an unpinned third endpoint do not change the product peer or liveness-derived state.

The existing `test_network_transport.cpp` remains the transport contract; controller tests must not duplicate low-level protocol assertions unnecessarily.

## 11. Build and CI

- Add `online_session.cpp` to the existing core target.
- Add `test_online_session.cpp` as a normal CTest target on Linux and Windows.
- Keep the existing standalone R2.5 transport contract unchanged.
- Run the production-readiness gate unchanged.
- At package checkpoint, run full Linux/Windows build, CTest, R2.5 transport contract and Windows single-executable artifact upload.

Passing generic tests does not promote R2.5 to `verified` and does not prove commercial-game online compatibility.

## 12. Repository/product constraints

- Work on `feature/r2-5-online-controller` until the package is reviewed and GREEN.
- Do not change production claims beyond evidence actually obtained.
- Keep R2.5 `implemented-unverified` unless independent production criteria are later satisfied.
- Keep one shipping executable: `JOJO-Recompiled.exe`.
- Preserve R2.2/R2.4 external-evidence blockers.
- Mods remain deferred until base-game production completion.

## 13. Completion criteria

This package is complete when:

- the endpoint parser and controller API are implemented;
- all required controller tests pass on Linux and Windows;
- existing transport tests remain green;
- readiness gate remains green;
- Windows artifact generation remains green;
- project state records the new controller evidence without claiming complete M9 or commercial online support.
