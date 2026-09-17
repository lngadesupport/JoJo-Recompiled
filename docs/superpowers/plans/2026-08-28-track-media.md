# Track-aware Dreamcast Media Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Feed ISO9660 from cooked/raw Dreamcast media layouts through one bounded logical-sector interface.

**Architecture:** A new `LogicalSectorSource` owns physical-to-logical sector mapping. ISO9660 consumes that source; descriptor parsers only discover safe data tracks.

**Tech Stack:** C++20, CMake/CTest, synthetic ISO/raw-track fixtures.

**Spec:** `docs/superpowers/specs/2026-08-28-track-media-design.md`

## Global Constraints

- Read-only host-file access.
- No descriptor path may escape its own directory.
- Every physical offset/length is bounds checked.
- Tests generate all media bytes.

---

### Task 1: Logical sector source and raw BIN autodetection
- [x] Write failing cooked/raw source tests.
- [x] Implement bounded 2048/2352 sector mapping and PVD-based BIN detection.
- [x] Run tests.

### Task 2: GDI descriptor
- [x] Write failing GDI data-track and path-safety tests.
- [x] Implement GDI parser/track selection.
- [x] Run tests.

### Task 3: CUE descriptor
- [x] Write failing CUE MODE1/MODE2 tests.
- [x] Implement CUE parser/index mapping.
- [x] Run tests.

### Task 4: ISO9660 + conversion integration
- [x] Refactor ISO9660 extent reads through logical sectors.
- [x] Remove ISO-only conversion gate.
- [x] Verify ISO/BIN/CUE/GDI synthetic conversions.
- [x] Verify Linux and Windows CI.

## Completion audit

- Synthetic tests cover cooked ISO, raw 2352-byte BIN Mode 1 and Mode 2 Form 1, CUE index mapping, GDI data-track selection, descriptor path safety, and conversion through ISO/BIN/CUE/GDI.
- All media reads are read-only and bounded; descriptor paths cannot escape their own directory.
- CI #480 (`de88afc8e7cddda31e5da3ffb14bd7a8fda03106`) passed the complete test suite on Linux and Windows/MSVC, including the Windows Release executable artifact.
- No commercial disc image, extracted asset, or game byte is stored in the repository or test fixtures.
