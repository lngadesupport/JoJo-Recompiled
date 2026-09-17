# JOJO Recompiled

JOJO Recompiled is an experimental native-Windows recreation/recompilation project for a **user-supplied, legally obtained PlayStation 1 copy** of *JoJo's Bizarre Adventure*.

The active guest platform is **Sony PlayStation 1 only**, and the scope is this JoJo title/revision family only. This is not intended to be a general PlayStation emulator.

This repository contains **no game image, PS-X EXE, PlayStation BIOS, artwork, music, ROM data, or extracted copyrighted game assets**. The project does not distribute the original game or a Sony BIOS.

## Product contract

The end-user application remains one Windows executable:

```text
JOJO-Recompiled.exe
```

The shipping flow now uses the user's original PS1 image directly. There is no user-facing "prepare game" conversion step and no extracted-game installation root.

Source resolution order is:

1. reopen the previously validated persistent source binding;
2. otherwise autodetect one logical PS1 source under `Data/ROM` beside the executable;
3. otherwise wait for the user to select or drag-and-drop a supported `.iso`, `.bin`, or `.cue` image.

A `.cue` plus its companion `.bin` track files is treated as one logical source. Unsupported Dreamcast `.gdi` files are not accepted by the PS1 source flow.

After validation, the application stores only source metadata needed to reopen the user's image — absolute path, format, size, fingerprint, and revision id. The original image remains the data source and is opened read-only.

## Current state — direct-source M4

The active PS1 path currently includes:

- PS1 ISO/BIN/CUE media access;
- observed USA whole-image fingerprint recognition;
- `SYSTEM.CNF` boot-path discovery;
- `PS-X EXE` parsing and validation;
- direct `Ps1DiscSession` access without creating an extracted installation;
- persistent game-source binding with change detection;
- startup priority of saved binding → `Data/ROM` → manual selection;
- Win32 `VALIDAR JOGO` flow replacing the old `PREPARAR JOGO` flow;
- direct R3000A checkpoint execution from the original disc image;
- synthetic regression fixtures for source validation, binding, startup selection, and Win32 UX contracts.

The repository still contains legacy installation/conversion implementation for compatibility with older tests and migration work, but the Windows M4 entry point no longer uses that path. Removing the remaining legacy subsystem is a later cleanup milestone.

The project already contains an R3000A reference execution core and HLE-oriented PS1 infrastructure. That does **not** mean the commercial game is fully playable yet. Full original-game execution, GPU rendering, SPU audio, controller integration with game logic, timing fidelity, and gameplay remain later milestones and require validation against the user's legal game image.

Synthetic fixtures prove technical contracts; they do not by themselves prove the commercial game is playable.

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

Historical design and plan files under `docs/superpowers/` remain in Git as project history. The active product direction is the PS1 direct-source runtime described above.

## Verification

Milestone changes are gated by CMake/CTest. M4 specifically has a Windows x64 contract test that launches the shipping entry point and verifies the direct-source UX, startup source priority, supported image selection, and removal of the legacy installation controls.
