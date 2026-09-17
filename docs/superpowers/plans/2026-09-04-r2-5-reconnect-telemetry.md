# R2.5 Direct Session Reconnect & Telemetry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic liveness, bounded reconnect, and telemetry behavior to the existing direct UDP session without expanding R2.5 into matchmaking or production-service claims.

**Architecture:** Extend `DirectUdpSession`'s existing caller-driven state machine. Keep the peer endpoint pinned, use existing ping/pong plus `session_hello`/`session_accept`, gate gameplay delivery on `connected`, and transition to terminal `disconnected` after a bounded reconnect deadline.

**Tech Stack:** C++20, UDP sockets, existing `NetworkPacket` protocol, `ControlReliabilityQueue`, CMake/CTest, GitHub Actions Linux/Windows.

**Spec:** `docs/superpowers/specs/2026-09-04-r2-5-reconnect-telemetry-design.md`

## Global Constraints

- Keep `main` unchanged without explicit user authorization.
- Keep one shipping artifact: `JOJO-Recompiled.exe`.
- R2.5 remains `implemented-unverified`; CI is not commercial-game evidence.
- Do not add matchmaking, accounts, NAT traversal, relay, ranked services, encryption, or game-specific online claims.
- Preserve peer pinning and data-only gameplay packet delivery.

---

### Task 1: RED reconnect/liveness contract

**Files:**
- Modify: `tests/test_network_transport.cpp`

**Interfaces:**
- Consumes: existing `DirectUdpSession::bind`, `connect`, `poll`, `send`, `state`, `remote_endpoint`, `telemetry`.
- Produces: executable behavioral expectations for timing configuration, heartbeat, silence, reconnect, spoof resistance, and reconnect deadline.

- [ ] **Step 1: Add failing tests** for invalid timing configuration, heartbeat/pong telemetry, silence transition to `reconnecting`, gameplay suppression during reconnect, same-peer recovery, spoof traffic not refreshing liveness, and reconnect deadline to `disconnected`.
- [ ] **Step 2: Run the standalone contract in CI** and verify RED failures are specifically due to missing timing/reconnect behavior.
- [ ] **Step 3: Commit RED evidence** without production changes.

### Task 2: GREEN liveness state machine

**Files:**
- Modify: `src/core/network_transport.h`

**Interfaces:**
- Consumes: `NetworkPacketKind::{ping,pong,session_hello,session_accept,disconnect}`, `ControlReliabilityQueue`, `NetworkTelemetry`.
- Produces: extended `DirectUdpSession::bind` timing configuration and deterministic `connected -> reconnecting -> connected|disconnected` behavior.

- [ ] **Step 1: Validate timing configuration** in `bind` and store heartbeat/liveness/reconnect intervals.
- [ ] **Step 2: Track liveness clocks** (`last_peer_receive_ms`, heartbeat send time, reconnect start time) using caller-supplied `now_ms` only.
- [ ] **Step 3: Emit heartbeat ping** while connected at the configured interval.
- [ ] **Step 4: Enter reconnecting on liveness timeout**, retain the pinned peer, suppress gameplay delivery, and update telemetry state/loss.
- [ ] **Step 5: Reuse the existing hello/accept handshake** to restore the pinned direct session; reject takeover by any other endpoint.
- [ ] **Step 6: Enforce reconnect deadline** and transition terminally to `disconnected`.
- [ ] **Step 7: Run standalone Linux/Windows contract** and verify GREEN.

### Task 3: Regression and checkpoint evidence

**Files:**
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`

**Interfaces:**
- Consumes: final CI run ID and commit SHA.
- Produces: truthful package checkpoint documentation without promoting R2.5 beyond `implemented-unverified`.

- [ ] **Step 1: Run full Linux/Windows workflow** with readiness gate, repository CTests, standalone R2.5 contract, and Windows artifact upload.
- [ ] **Step 2: Confirm both jobs are `success`** and the single Windows artifact exists.
- [ ] **Step 3: Record the reconnect/telemetry scope and evidence run** in project state/readiness docs.
- [ ] **Step 4: Re-run the workflow after documentation update** if readiness checks require exact evidence consistency.
