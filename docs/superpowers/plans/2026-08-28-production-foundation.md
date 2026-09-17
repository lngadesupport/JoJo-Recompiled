# Production Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Status:** **Complete (100%)** — audited against the production roadmap on 2026-08-29.

**Goal:** Ship a single Windows executable foundation with real conversion progress, modern first-run flow, expanded graphics settings model, and CI.

**Architecture:** Portable conversion/settings logic lives in `jojo_core`; the Windows application owns only native UI, filesystem-location discovery and device presentation. Conversion reports stage events through a callback so the UI never fakes progress and future recompiler phases can slot into the same pipeline.

**Tech Stack:** C++20, CMake 3.20+, Win32/COM, MSVC v143, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-28-production-foundation-design.md`

## Global Constraints

- End-user release exposes one `JOJO-Recompiled.exe`.
- No game images/assets in source control or release artifacts.
- First-run UI contains conversion only; graphics/controls belong in-game.
- Conversion progress is actual event-driven progress.
- MSAA persists Off/2x/4x/8x.
- Display mode persists Windowed/Fullscreen/Borderless.
- Portable core tests must pass on Linux and Windows.

## Completion evidence

- `tests/test_main.cpp` covers MSAA 8x, display mode/UI-scale persistence, real monotonic conversion progress, source-independent installation, explicit unknown-revision rejection, and ISO/BIN/CUE/GDI conversion fixtures.
- `src/app_win32/main.cpp` implements the themed first-run-only preparation UI, worker-thread conversion, `%LOCALAPPDATA%\\JOJO Recompiled\\game`, persistent `game/logs/conversion.log`, and explicit backend-pending messaging.
- `CMakeLists.txt` defines one non-test Windows shipping target with output name `JOJO-Recompiled.exe`.
- `.github/workflows/build.yml` runs Linux and Windows/MSVC build/tests and uploads only `build/Release/JOJO-Recompiled.exe` as the application artifact.
- Main CI run #472 completed successfully on both Linux and Windows/MSVC before this checklist was closed.

---

### Task 1: Expand settings model

**Files:** `src/core/settings.h`, `src/core/settings.cpp`, `tests/test_main.cpp`

**Produces:** `Msaa::x8`, `DisplayMode`, `UiScale`, `HudSafeArea` and round-trip persistence.

- [x] Add failing tests for MSAA 8x, display mode and UI-scale round trip.
- [x] Run tests and verify failure.
- [x] Implement enums, validation and serialization/parsing.
- [x] Run the full test suite.

### Task 2: Add conversion progress contract

**Files:** `src/core/conversion.h`, `src/core/conversion.cpp`, `tests/test_main.cpp`

**Produces:** `ConversionStage`, `ConversionProgress`, `ConversionProgressCallback`, progress-aware `convert_image`.

- [x] Add a failing test that captures progress and checks monotonic 0..100 stage events.
- [x] Verify the test fails because the callback API does not exist.
- [x] Implement progress emission around existing real conversion work.
- [x] Verify existing source-independent installation behavior remains green.

### Task 3: Single-shipping-executable build

**Files:** `CMakeLists.txt`, `src/app_win32/main.cpp`, platform input files.

**Produces:** Windows target with output name `JOJO-Recompiled.exe`; converter/runtime CLIs removed from shipping targets.

- [x] Add build-structure regression checks in the portable tests/documentation script.
- [x] Move/replace the old launcher UI with the first-run application shell.
- [x] Ensure only one non-test Windows executable is defined.
- [x] Build the portable core/tests locally and in CI.

### Task 4: Windows first-run progress UI

**Files:** `src/app_win32/main.cpp`

**Produces:** themed first-run screen, source picker, worker-thread conversion, progress bar and log display.

- [x] Implement custom drawing and Unicode text.
- [x] Run conversion on a worker thread and post progress to the main window.
- [x] Persist log entries under the game data directory.
- [x] Surface backend-pending state accurately after conversion.

### Task 5: CI

**Files:** `.github/workflows/build.yml`, `README.md`, `docs/BUILD-WINDOWS.md`

**Produces:** Windows and Linux build/test jobs.

- [x] Add Windows Release configure/build/CTest job.
- [x] Add Linux portable-core configure/build/CTest job.
- [x] Update build docs for the single executable.
- [x] Push feature branch and verify GitHub Actions results before merge.
