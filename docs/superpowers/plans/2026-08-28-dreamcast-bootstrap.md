# Dreamcast Bootstrap Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse Dreamcast IP metadata and discover/read the boot executable selected by the disc.

**Architecture:** A portable bootstrap module reads the system-area sectors from `LogicalSectorSource`, validates fixed-width IP metadata, then resolves the boot filename through `Iso9660Image`.

**Tech Stack:** C++20, CMake/CTest, generated synthetic media fixtures.

**Spec:** `docs/superpowers/specs/2026-08-28-dreamcast-bootstrap-design.md`

## Global Constraints

- Never hardcode a retail executable path when the bootstrap names it.
- Never execute or persist user game bytes in source control.
- Reject unsafe boot filenames and oversized programs.
- Keep scrambling/SH-4 decoding out of this milestone.

---

### Task 1: IP metadata parser
- [x] Write failing valid/invalid IP metadata tests.
- [x] Implement fixed-field parsing and Dreamcast signature validation.
- [x] Run tests.

### Task 2: Boot executable discovery
- [x] Write failing custom boot-filename and unsafe-name tests.
- [x] Resolve and read the executable through ISO9660 with a size cap.
- [x] Hash the discovered bytes for diagnostics/revision support.
- [x] Run full tests.

### Task 3: CI gate
- [x] Publish PR.
- [ ] Verify Linux and Windows/MSVC 2022.
- [ ] Merge only after both jobs pass.
