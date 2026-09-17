# Disc Filesystem and Revision Identification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse ISO9660 safely and identify game revisions through declarative file signatures.

**Architecture:** A portable read-only `Iso9660Image` module owns filesystem parsing while revision matching consumes that interface. No game-specific offsets or bytes enter source control.

**Tech Stack:** C++20, CMake/CTest, dependency-free synthetic ISO fixtures.

**Spec:** `docs/superpowers/specs/2026-08-28-disc-filesystem-design.md`

## Global Constraints

- No copyrighted game bytes in tests/repository.
- Every disc offset and length is bounds checked.
- Caller paths cannot escape the virtual disc root.
- Unknown revisions fail explicitly.

---

### Task 1: ISO9660 mount and root listing
- [x] Write synthetic ISO fixture and failing mount/list tests.
- [x] Verify compile/test failure because ISO API does not exist.
- [x] Implement PVD/root-record validation and directory listing.
- [x] Run full tests.

### Task 2: Nested lookup and bounded reads
- [x] Add failing nested/case-insensitive file-read tests.
- [x] Implement path normalization/traversal and bounded extent reads.
- [x] Add malformed/out-of-bounds regression tests.
- [x] Run full tests.

### Task 3: Revision matcher
- [x] Add failing synthetic profile match/unknown tests.
- [x] Implement signature hashing and profile evaluation.
- [x] Run full tests and clean build.

### Task 4: Integrate conversion discovery stage
- [x] Add progress stages for filesystem discovery and revision identification.
- [x] Keep unsupported real revisions explicit until verified signatures exist.
- [x] Verify Linux and Windows CI.

## Completion audit

- The matcher requires multiple declarative file signatures for a revision profile and does not guess game offsets.
- Unknown media remains `unknown_revision`; when a profile is rejected, diagnostics report the profile and first mismatching/missing disc path without exposing expected hashes or game bytes.
- TDD RED: `da08416559b7af245427ab1c6894258c3e080eaa`; CI #479 built successfully and failed only `jojo_iso_tests` on the new diagnostic assertions (38/39 tests passed).
- GREEN: `de88afc8e7cddda31e5da3ffb14bd7a8fda03106`; CI #480 passed the full suite on Linux and Windows/MSVC, including Windows Release artifact generation.
- Commercial revision profiles remain intentionally unregistered until their signatures can be independently verified from legally supplied media. This preserves explicit failure instead of inventing support.
