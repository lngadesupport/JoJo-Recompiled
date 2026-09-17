# JOJO Recompiled

JOJO Recompiled is an experimental native-Windows recompilation project for a **user-supplied, legally obtained PlayStation 1 copy** of *JoJo's Bizarre Adventure: Heritage for the Future*.

The active guest platform is **Sony PlayStation 1 only**, and the scope is **this JoJo title/revision family only**. This is not a general PlayStation emulator.

This repository contains **no game image, PS-X EXE, PlayStation BIOS, artwork, music, ROM data, or extracted copyrighted game assets**. The final runtime is designed not to require a proprietary external BIOS.

## Product contract

The end-user application remains one executable:

```text
JOJO-Recompiled.exe
```

On first launch it asks for the user's own supported PS1 image (`.iso`, `.bin`, or `.cue`) and lets the user choose the installation root. `%LOCALAPPDATA%\JOJO Recompiled\game` is only the proposed default, not a fixed destination. The source image is opened read-only.

## Current state — PS1 M1 foundation

The current active implementation is the PlayStation 1 M1 foundation:

- observed USA whole-image fingerprint: recognized;
- PS1 ISO/BIN/CUE media path: implemented;
- `SYSTEM.CNF` boot-path discovery: implemented and synthetic-test verified;
- `PS-X EXE` parsing/validation and hashing: implemented and synthetic-test verified;
- user-selectable install root: implemented;
- transactional generation installation and active-generation pointer: implemented;
- incompatible legacy Dreamcast/SH-4 guest/backend architecture: removed from the active build, tests, and CI contracts;
- R3000A/MIPS execution: **not implemented in M1**;
- native x64 code generation for R3000A: **not implemented in M1**;
- commercial PS-X EXE discovery on the user's real image: **awaiting a new local run**;
- boot, rendering, audio, game-input integration, and gameplay: **not verified**.

A synthetic fixture proves parser/conversion contracts only. It does not prove the commercial game boots or is playable.

The repository still uses the **R2 — Production completion** readiness vocabulary and machine-checkable status file at [`docs/architecture/PRODUCTION-READINESS.tsv`](docs/architecture/PRODUCTION-READINESS.tsv), but old Dreamcast/SH-4 implementation claims are not active product claims.

Commercial-game integration is not yet verified. The next evidence boundary is a local run against the user's same legally obtained PS1 image to confirm `SYSTEM.CNF` resolution and commercial `PS-X EXE` validation without storing commercial bytes in Git or CI.

## Retained host-side infrastructure

Console-neutral components retained from earlier work include presentation/settings/input models, mods, training tools, rollback/networking utilities, revision/fingerprint infrastructure, ISO9660/media handling, Windows application plumbing, and CI. Their existence does **not** mean they are already connected to the original PS1 game code.

## Build on Windows

See [`docs/BUILD-WINDOWS.md`](docs/BUILD-WINDOWS.md).

## Architecture / roadmap

See [`docs/architecture/PRODUCTION-ROADMAP.md`](docs/architecture/PRODUCTION-ROADMAP.md) for the current PS1 roadmap. Historical design/plan files under `docs/superpowers/` remain in Git as project history and are not the active guest architecture.

## CI

GitHub Actions builds/tests the portable core on Linux and the x64 application on `windows-2022`. Both jobs run the production-readiness gate and the PS1 active-architecture gate. The Windows job uploads only `JOJO-Recompiled.exe` as the application artifact.
