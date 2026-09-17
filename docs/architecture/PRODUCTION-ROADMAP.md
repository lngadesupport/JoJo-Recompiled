# JOJO Recompiled — Production Architecture

## Product contract

The end user receives and launches one Windows x64 application: `JOJO-Recompiled.exe`.

The active guest platform is **Sony PlayStation 1**. The project targets **this JoJo title/revision family only**; it is not a general PlayStation emulator.

The user supplies their own legally obtained PS1 image. Active source formats are `.iso`, `.bin`, and `.cue`. The source is read-only. The user chooses the installation root; `%LOCALAPPDATA%\JOJO Recompiled\game` is only a proposed default.

The repository, CI, artifacts, and releases contain no commercial game image, extracted PS-X EXE, proprietary PlayStation BIOS, or copyrighted game assets.

## Current milestone — PS1 M1 foundation

Implemented and repository-test verified:

- PS1-only media acceptance and `.gdi` rejection;
- raw MODE1/2352 and MODE2/2352 logical-sector handling;
- ISO9660 filesystem access;
- strict `SYSTEM.CNF` boot-path resolution;
- `PS-X EXE` header validation, metadata extraction, and full-file hashing;
- observed USA whole-image fingerprint recognition;
- user-selectable install root;
- manifest v2 plus transactional generation staging/activation;
- legacy incompatible-installation classification;
- Linux and Windows CI with an explicit PS1 active-architecture gate;
- removal of Dreamcast/SH-4 guest/backend source, tests, targets, and workflow contracts from the active project.

M1 deliberately stops before CPU execution. The following are not M1 capabilities:

- R3000A/MIPS execution;
- MIPS CFG/IR execution;
- native x64 code generation for R3000A;
- commercial BIOS/HLE coverage;
- commercial boot;
- GPU rendering;
- SPU audio;
- original-game input integration;
- gameplay.

The known commercial whole-image fingerprint has been observed, but the corrected PS1 pipeline still requires a fresh local run to observe `SYSTEM.CNF` and validate the commercial `PS-X EXE` on the user's real image.

## M2 — R3000A reference execution

Start only after M1 commercial-image metadata has been observed locally. Build an R3000A-compatible MIPS I reference executor with synthetic TDD and explicit diagnostics.

Required baseline semantics include 32 GPRs with `$zero` invariant, HI/LO, PC, little-endian memory behavior, branch delay slots, load delay semantics, and alignment/exception behavior actually required by the game. COP0 and GTE/COP2 are added according to observed JoJo requirements rather than by implementing a generic PS1 compatibility matrix.

No M2 completion claim is valid without Linux and Windows tests for the exact semantics introduced.

## M3 — MIPS CFG/IR and native x64 backend

After reference semantics are stable:

- discover executable code regions and control flow;
- lift supported R3000A instructions into explicit IR;
- preserve delay-slot/load-delay behavior in CFG/IR boundaries;
- add host x64 lowering with an architecture/versioned cache;
- bind cache identity to the exact revision and commercial executable hash;
- reject incompatible legacy caches.

`native-codegen-ready` is not equivalent to bootable or playable.

## M4 — JoJo PS1 runtime services

Add only the services the game demonstrably uses:

- PS1 main memory and bus/MMIO diagnostics;
- BIOS HLE without a proprietary BIOS dependency;
- interrupts and timers;
- DMA;
- GTE/COP2;
- GPU;
- CD-ROM/runtime disc access;
- controllers;
- SPU/audio.

Unknown MMIO, HLE, or coprocessor operations must fail with diagnostics including PC/address/operation context. Silent zero-filled fake success is not an acceptable compatibility strategy.

## M5 — Observable commercial checkpoints

Promote evidence in small checkpoints: executable entry reached, first expected service boundary, first GPU command stream, first visible frame/logo/menu, controller response, audio, and finally gameplay. Each checkpoint must be backed by a local legally supplied run and reproducible diagnostics where feasible.

Do not label the game `bootable`, `rendering`, `audio-working`, `playable`, or similar until the corresponding evidence exists.

## Retained console-neutral host infrastructure

Presentation/settings/input models, mod runtime, training tools, rollback/networking utilities, ISO/media infrastructure, revision fingerprints, Windows UI plumbing, hashing, and CI remain available as host-side infrastructure. They are not automatically integrated into the original PS1 game merely because their standalone tests pass.

Historical Dreamcast/SH-4 plans and specs under `docs/superpowers/` remain project history only.

### Production completion program (R2)

The machine-checkable truth vocabulary remains in [`PRODUCTION-READINESS.tsv`](PRODUCTION-READINESS.tsv). Its workstreams are interpreted against the active PS1 architecture:

- R2.1 — repository truth/release gates;
- R2.2 — commercial revision enablement;
- R2.3 — game-specific execution/device integration;
- R2.4 — real gameplay integration;
- R2.5 — online product modes/integration;
- R2.6 — production validation/release.

A workstream is not complete because code exists. Synthetic fixtures prove only their explicit contracts, and `blocked-external-evidence` never counts as verified.

## Architectural rules

- Commercial source media is read-only.
- No proprietary BIOS is distributed or required by design.
- No commercial bytes are committed to Git/CI/releases.
- Unsupported PS1 behavior produces explicit diagnostics.
- Game requirements drive hardware/HLE scope; generic emulator completeness is a non-goal.
- Simulation state is not owned by rendering or networking.
- Conversion progress reflects real stages, not timers.
- The distributable application artifact remains one `JOJO-Recompiled.exe`.
