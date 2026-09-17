# M7 Training Laboratory Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the portable deterministic training-laboratory contract for frame data, readable frame meter, diagnostics, pause/frame-step and save/load state.

**Architecture:** Keep all training behavior in `jojo_core`. Consume M5 resolved inputs, store logical gameplay observations supplied by a future game adapter, and treat snapshot bytes as opaque deterministic state.

**Tech Stack:** C++20, CMake/CTest, existing `jojo::Result<T>`, M5 input types and portable SHA-256.

**Spec:** `docs/superpowers/specs/2026-08-30-training-laboratory-design.md`

## Global Constraints

- No wall-clock dependency in training state.
- No proprietary game offsets, assets or fingerprints.
- Meter semantics must include labels/icons and never rely on color alone.
- Snapshot integrity uses existing SHA-256.
- New behavior follows RED -> GREEN and passes Linux + Windows/MSVC CI.

---

### Task 1: Permanent M7 RED contract

**Files:**
- Create: `tests/test_training.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Requires `FramePhase`, `CollisionBoxKind`, `CollisionBox`, `TrainingFrameSample`, `TrainingTimeline`, `FrameMeterSegment`, `TrainingPlaybackControl`, `TrainingStateSnapshot`, `TrainingStateSlots`.

- [ ] **Step 1: Write the failing test**

Test bounded monotonic timeline behavior; attack-phase grouping with non-empty label/icon; positive/negative frame advantage; preservation of hitstop/hitstun/blockstun/cancel/combo/damage/scaling; logical collision geometry validation; stable two-player input history; pause/resume and exact single-frame permissions; deterministic snapshot digest; ten save slots; corrupted snapshot rejection.

- [ ] **Step 2: Wire CTest**

Add `jojo_training_tests` linked to `jojo_core`.

- [ ] **Step 3: Verify RED in CI**

Push only test/CMake changes. Expected Linux and Windows compile failure because `core/training.h` and the required M7 API do not exist.

- [ ] **Step 4: Commit**

Commit message: `test: require deterministic M7 training laboratory`.

---

### Task 2: Training timeline and derived diagnostics GREEN

**Files:**
- Create: `src/core/training.h`
- Create: `src/core/training.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces deterministic timeline, meter, collision validation, input history and `frame_advantage`.

- [ ] **Step 1: Implement validated data types**

Use fixed-width counters where useful, preserve `ResolvedInputFrame` from M5, validate capacity and collision dimensions/labels before accepting samples.

- [ ] **Step 2: Implement bounded timeline**

Reject non-monotonic frame indexes, append valid samples, and evict oldest entries only when capacity is exceeded.

- [ ] **Step 3: Implement derived views**

Group contiguous startup/active/recovery phases into meter segments with mandatory text label/icon. Derive player input history in stable action order and frame advantage as defender stun minus attacker recovery.

- [ ] **Step 4: Run CI**

Expected: timeline/diagnostic tests pass; playback/snapshot assertions remain failing until Task 3 if split by test sections, or keep implementation minimal enough to satisfy only these assertions.

- [ ] **Step 5: Commit**

Commit message: `feat: add deterministic training timeline diagnostics`.

---

### Task 3: Pause/frame-step and state slots GREEN

**Files:**
- Modify: `src/core/training.h`
- Modify: `src/core/training.cpp`
- Test: `tests/test_training.cpp`

**Interfaces:**
- Produces `TrainingPlaybackControl`, `make_training_snapshot`, `TrainingStateSlots`.

- [ ] **Step 1: Implement playback gate**

Running mode allows every frame. Paused mode allows only queued frame-step permits; cap queued permits at 8. Resume clears queued permits.

- [ ] **Step 2: Implement canonical snapshot hashing**

Hash little-endian 64-bit frame index followed by opaque state bytes with `Sha256Hasher`; persist the hex digest inside the snapshot.

- [ ] **Step 3: Implement ten state slots**

Slots `0..9` support replace-on-save, explicit empty/invalid-slot errors, and integrity validation before load returns a snapshot.

- [ ] **Step 4: Verify GREEN**

Require the entire `jojo_training_tests` plus all existing CTest targets green on Linux and Windows/MSVC.

- [ ] **Step 5: Commit**

Commit message: `feat: complete training playback and state snapshots`.

---

### Task 4: Close M7 and integrate

**Files:**
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`
- Create: `docs/superpowers/plans/2026-08-30-training-laboratory-verification.md`

- [ ] **Step 1: Record RED/GREEN evidence**

Include exact commit SHAs, workflow run IDs and final Windows artifact information.

- [ ] **Step 2: Mark M7 complete in roadmap**

Check every training-lab requirement and state the readiness boundary: portable training contract complete, commercial game-state adapter not yet proven.

- [ ] **Step 3: Final branch CI**

Require Linux and Windows/MSVC build/CTest plus Windows executable artifact upload.

- [ ] **Step 4: PR and protected merge**

Open PR, verify final head, merge with `expected_head_sha`, then require post-merge `main` CI green before declaring M7 closed.
