# JOJO Recompiled — Evidence-Based Production Completion Design

## Goal

Finish JOJO Recompiled as a real end-user product without converting architectural contracts, mocks, fallbacks, synthetic fixtures, or unverified assumptions into claims of commercial-game functionality.

## Truth policy

A capability may be called complete only when its exact completion criterion has reproducible evidence. Status vocabulary is limited to:

- `not-started`: no implementation evidence.
- `implemented-unverified`: implementation exists but the required validation has not been observed.
- `verified`: the required automated or end-to-end evidence exists and passes.
- `blocked-external-evidence`: implementation cannot be honestly verified without legally supplied commercial media, external infrastructure, credentials, hardware, or another explicit external prerequisite.

Percentages are not inferred from code volume. A production milestone is 100% only when every mandatory criterion is `verified`; `blocked-external-evidence` is never counted as verified.

## Baseline

The current authoritative architecture roadmap records M1 through M8 as complete within their scoped reusable contracts. Those milestone labels do not prove that the commercial game boots or that the complete product is playable. M9 remains open. The README also states that the game-specific native recompiler is not finished and that converted output remains pending rather than pretending to be playable.

The production-completion program therefore starts from the current `main` after M8 and treats all commercial-game integration claims as unverified until demonstrated.

## Workstreams

### R2.1 — Repository truth and release gates

Make roadmap, README, next-milestone documentation, conversion status, CI, and release criteria agree. Add a machine-checkable production-readiness contract so stale documentation cannot silently claim completion. Existing M1–M8 contract tests remain valuable but are not substitutes for end-to-end evidence.

### R2.2 — Commercial revision enablement

The disc/revision pipeline must identify at least one explicitly supported commercial revision using fingerprints independently derived from a legally supplied user image. Unknown media must remain rejected. No copyrighted bytes or extracted assets may be committed.

If no legal image is available to the development environment, this workstream must stop at `blocked-external-evidence`; fingerprints must not be guessed.

### R2.3 — Game-specific execution and device integration

Close the gap between reusable backend architecture and real game execution: SH-4 coverage required by the supported revision, Dreamcast device behavior needed by the title, PVR2 scene submission/presentation, AICA audio, Maple controller input, timing/interrupt behavior, and save/configuration integration.

Fallback execution is permitted during development only when observable and tested. Release readiness requires that the supported game's exercised paths satisfy the agreed performance/correctness gates rather than merely possessing generic interfaces.

### R2.4 — Real gameplay integration

Prove the supported revision progresses from converted installation through native startup into actual interactive gameplay. Renderer output, audio, input, settings, training adapters, mod overlays, state snapshots, and deterministic frame stepping must be wired to real game state rather than synthetic fixtures.

### R2.5 — Online product modes (M9)

Build the user-facing Online menu and production session layer around the existing rollback core: direct rooms/invites, casual matchmaking, ranked/competitive policy, custom rules/mod policy, profile/history/replays, connection-quality UI, reconnect/leave semantics, socket transport, and any required relay/NAT traversal or service boundary.

Account/service infrastructure must be explicitly scoped before implementation; no mock service may be labeled production matchmaking.

### R2.6 — Production validation and release

A release candidate is eligible for `verified` only when all mandatory gates pass on the shipping Windows build:

1. Clean Windows x64 build produces the single shipping `JOJO-Recompiled.exe`.
2. Automated Linux/Windows suites pass for applicable portable/platform contracts.
3. A legally supplied supported image is accepted and an unknown/unsupported image is rejected safely.
4. First-run conversion reaches a truthful native-ready state only after all required conversion stages succeed.
5. A subsequent launch reaches real game code without requiring developer tools.
6. At least one reproducible path reaches rendered, audible, controllable real gameplay.
7. Save/configuration persistence survives restart.
8. Supported graphics/aspect/input settings are exercised in the real runtime.
9. Training state capture/restore and frame diagnostics operate on real gameplay state.
10. Mod policy/identity is enforced on real session/game content where applicable.
11. Two real peers can complete the required rollback/direct-online scenario with telemetry and deterministic-state checks; matchmaking modes require their actual production service rather than a stub.
12. Crash/failure paths leave actionable logs and never falsely mark conversion or runtime readiness.
13. Release documentation describes only observed capabilities and known limitations.

## Evidence model

Every completion claim must link to one or more of:

- a committed automated test plus passing CI run for the exact commit under evaluation;
- a deterministic fixture that proves a generic contract, labeled as such;
- a legally supplied-media end-to-end run with no copyrighted data committed;
- a platform/hardware integration run when CI cannot reproduce the behavior;
- a production-service integration run for online service claims.

A passing generic unit test cannot establish commercial-game compatibility. A build artifact cannot establish playability. A synthetic network peer cannot establish production matchmaking.

## Architecture constraints

Existing roadmap rules remain mandatory: simulation is independent of render cadence; rendering does not own authoritative gameplay state; networking supplies frame-numbered inputs/session commands rather than mutating gameplay directly; mods use versioned public interfaces; UI uses logical coordinates/anchors/safe areas; conversion progress comes from real stages; shipping distribution remains one executable; copyrighted game data never enters the repository.

## Execution strategy

This program is too broad for one monolithic implementation plan. Execute it as ordered, independently reviewable subprojects: R2.1 truth/release gates first; R2.2 revision enablement when legal evidence is available; R2.3 execution/device integration; R2.4 gameplay integration; R2.5 M9; R2.6 final validation. Each subproject gets its own TDD implementation plan and verification evidence before the next completion claim.

## Definition of done

JOJO Recompiled is globally `100% verified` only when every mandatory production gate above is verified against the shipping code and any required real external systems/media. Until then, documentation must state the precise verified scope and blockers rather than a global completion percentage.