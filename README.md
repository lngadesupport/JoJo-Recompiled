# JOJO Recompiled

JOJO Recompiled is an experimental native-Windows recreation/recompilation project for a **user-supplied, legally obtained PlayStation 1 copy** of *JoJo's Bizarre Adventure*.

The active guest platform is **Sony PlayStation 1 only**, and the scope is this JoJo title/revision family only. This is not intended to be a general PlayStation emulator.

This repository contains **no game image, PS-X EXE, PlayStation BIOS, artwork, music, ROM data, or extracted copyrighted game assets**. The project does not distribute the original game or a Sony BIOS.

## Product contract

The end-user application remains one Windows executable:

```text
JOJO-Recompiled.exe
```

The shipping flow uses the user's original PS1 image directly. There is no user-facing "prepare game" conversion step and no extracted-game installation root.

Source resolution order is:

1. reopen the previously validated persistent source binding;
2. otherwise autodetect one logical PS1 source under `Data/ROM` beside the executable;
3. otherwise wait for the user to select or drag-and-drop a supported `.iso`, `.bin`, or `.cue` image.

A `.cue` plus its companion `.bin` track files is treated as one logical source. Unsupported Dreamcast `.gdi` files are not accepted by the PS1 source flow.

After validation, the application stores only source metadata needed to reopen the user's image — absolute path, format, size, fingerprint, and revision id. The original image remains the authoritative data source and is opened read-only.

## Current state — commercial frontier M7 / Phase 2

The completed direct-source foundation includes:

- PS1 ISO/BIN/CUE media access;
- observed USA whole-image fingerprint recognition;
- `SYSTEM.CNF` boot-path discovery;
- `PS-X EXE` parsing and validation;
- direct `Ps1DiscSession` access without creating an extracted installation;
- persistent game-source binding with change detection;
- startup priority of saved binding → `Data/ROM` → manual selection;
- Win32 `VALIDAR JOGO` flow replacing the old `PREPARAR JOGO` flow;
- direct R3000A execution from the original disc image;
- bounded logical-sector streaming from ISO and raw BIN/CUE media;
- removal of the legacy conversion/installation sources from the shipping `jojo_core` target;
- removal of legacy installation/conversion tests from the default CTest graph;
- removal of `install_root` from active application settings;
- an architecture gate that prevents the shipping entry point/runtime from regaining dependencies on `convert_image`, `active_install.ini`, `boot.psxexe`, `generations/`, or the old installation API.

Phase 2 adds a deterministic commercial-boot evidence path:

- stable frontier classes for BIOS, generic MMIO, DMA, GPU GP0/GP1, CD-ROM, CPU boundary, stalls, execution budget, frame presentation, and fatal runtime errors;
- `Ps1CommercialEvidenceRunner`, which owns both the direct disc session and R3000A boot runtime so media remains available throughout the run;
- normal mode that **stops at the first unsupported behavior instead of guessing**;
- optional diagnostic BIOS fallbacks that are explicit, bounded, and recorded in the report;
- bounded BIOS/MMIO/CD-ROM/trace evidence buffers;
- compact atomic report serialization that contains engineering metadata and summaries but no RAM/VRAM dump, sector dump, PS-X EXE payload, or copied commercial asset;
- Win32 shipping diagnostics redirected from the old direct checkpoint helper to the commercial evidence runner;
- `%LOCALAPPDATA%\JOJO Recompiled\diagnostics\commercial-frontier.txt` as the current user-facing frontier report.

Historical conversion/installation source files may remain in the repository as development history, but they are no longer part of the shipping runtime graph or default test graph.

The project already contains an R3000A reference execution core and HLE-oriented PS1 infrastructure. That does **not** mean the commercial game is fully playable yet. Full original-game execution, GPU/GTE behavior, SPU audio, CD-ROM controller semantics, controller integration with game logic, timing fidelity, and gameplay remain later milestones and require validation against the user's legal game image.

Synthetic fixtures prove technical contracts and CI portability; they do **not** by themselves prove that the commercial JoJo image boots to gameplay. The next commercial implementation frontier must be selected from evidence produced by a user-supplied legal JoJo image, not from speculative emulation work.

## User data

Per-user configuration and diagnostics are stored under:

```text
%LOCALAPPDATA%\JOJO Recompiled\
```

The persistent source binding is metadata only. No original game assets are copied into the repository or release package.

## Retained host-side infrastructure

Console-neutral components retained from earlier work include presentation/settings/input models, mods, training tools, rollback/networking utilities, revision/fingerprint infrastructure, ISO9660/media handling, Windows application plumbing, and diagnostics. Their existence does not imply that every subsystem is already connected to the original PS1 game logic.

## Build on Windows

See [`docs/BUILD-WINDOWS.md`](docs/BUILD-WINDOWS.md).

## Architecture / roadmap

Historical design and plan files under `docs/superpowers/` remain in Git as project history. The active product direction is the PS1 direct-source runtime and commercial-frontier workflow described above.

## Verification

Milestone changes are gated by CMake/CTest. The direct-source foundation includes sector-streaming and active-architecture contracts. Phase 2 adds dedicated commercial-frontier, commercial-evidence, compact-report, shipping-integration, and Windows x64 gates. A synthetic green suite establishes deterministic contracts and portability; commercial-game progress still requires frontier evidence from the user's own legal image.
