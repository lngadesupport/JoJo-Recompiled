# M7 Training Laboratory Design

## Scope

M7 provides a portable deterministic training runtime. It records frame-indexed gameplay observations, derives startup/active/recovery information, preserves stun/cancel/combo diagnostics, exposes logical collision geometry and input history, controls pause/frame-step, and stores deterministic training-state snapshots. It contains no proprietary offsets or game data.

## Timeline

`TrainingFrameSample` stores a strictly increasing frame index, `FramePhase` (`neutral`, `startup`, `active`, `recovery`), hitstop/hitstun/blockstun counters, attacker recovery, defender stun, cancel-window state, combo count, cumulative damage, scaling percentage, the resolved M5 two-player input state, and zero or more logical `CollisionBox` values.

`TrainingTimeline` is bounded. Duplicate or older frame indexes are rejected; exceeding capacity evicts the oldest sample deterministically. Collision boxes have non-negative dimensions, a kind (`attack`, `vulnerable`, `push`) and a non-empty semantic label.

## Derived views

`FrameMeterSegment` groups contiguous startup/active/recovery samples and always contains frame range, text label and icon token, so a future visual meter never relies on color alone. `frame_advantage()` is `defender_stun_frames - attacker_recovery_frames`.

Input history reuses M5 `ResolvedPlayerInput`. Entries are emitted in `all_game_actions()` order for both players and therefore remain stable across hosts.

## Playback control

`TrainingPlaybackControl` is either running or paused. Running grants every simulation frame. Paused grants no frame unless `request_frame_step()` adds one bounded step permit; `consume_frame_permission()` consumes exactly one permit. Rendering is not authoritative and may continue while simulation is paused.

## Save/load state

`TrainingStateSnapshot` contains frame index, opaque deterministic simulation bytes and SHA-256 integrity over a canonical little-endian frame index followed by the bytes. Ten numbered slots (`0..9`) are available. Saving replaces a slot; loading validates the digest and rejects invalid/corrupted data.

The snapshot payload is intentionally opaque so later commercial-game integration can supply complete runtime state without changing M7.

## Determinism and errors

No operation depends on wall-clock time. Ordering is explicit, all validation errors use `jojo::Result<T>`, and identical inputs must derive identical results on Linux and Windows.

## Files

- `src/core/training.h`
- `src/core/training.cpp`
- `tests/test_training.cpp`
- `CMakeLists.txt`

## Completion gates

M7 is complete only after the permanent tests are observed RED before production code; Linux and Windows/MSVC are green; the roadmap records the verified contract and readiness boundary; the PR is merged at the validated head SHA; and post-merge `main` CI is green with the Windows executable artifact. M7 completion does not claim that real commercial-game state is already wired into the training adapter.