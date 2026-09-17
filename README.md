# JOJO Recompiled

JOJO Recompiled is an experimental native-Windows recreation/recompilation project for a **user-supplied, legally obtained PlayStation 1 copy** of *JoJo's Bizarre Adventure*.

The active guest platform is **Sony PlayStation 1 only**, and the scope is this JoJo title/revision family only. This is not intended to be a general PlayStation emulator.

This repository contains **no game image, PS-X EXE, PlayStation BIOS, artwork, music, ROM data, or extracted copyrighted game assets**. The project does not distribute the original game or a Sony BIOS.

## Product contract

The end-user application remains one Windows executable:

```text
JOJO-Recompiled.exe
```

The shipping flow uses the user's original PS1 image directly. There is no user-facing prepare/convert step and no extracted-game installation root.

Source resolution order is:

1. reopen the previously validated persistent source binding;
2. otherwise autodetect one logical PS1 source under `Data/ROM` beside the executable;
3. otherwise wait for the user to select or drag-and-drop a supported `.iso`, `.bin`, or `.cue` image.

A `.cue` plus its companion `.bin` track files is one logical source. `.gdi` is rejected. The original image remains authoritative and read-only.

## Phase status

### Phase 1 — Direct-source foundation ✅

Completed capabilities include ISO/BIN/CUE access, ISO9660, `SYSTEM.CNF`, PS-X EXE parsing, source binding, `Data/ROM` discovery, direct R3000A startup, sector streaming, Windows direct-source UX, and removal of the legacy conversion/installation runtime from the shipping graph.

### Phase 2 — Commercial boot frontier/evidence ✅

Phase 2 added:

- stable commercial frontier classification;
- `Ps1CommercialEvidenceRunner` retaining a live direct-disc session;
- normal mode that stops at unsupported behavior instead of guessing;
- explicit and recorded diagnostic-only BIOS fallbacks;
- bounded BIOS/MMIO/CD-ROM/trace evidence;
- compact `commercial-frontier.txt` diagnostics without commercial payload dumps;
- Windows shipping diagnostics using the same direct-disc runtime.

### Phase 3 — PS1 hardware services ✅

Phase 3 adds a focused `Ps1HardwareServices` layer behind CPU-visible MMIO:

- deterministic `I_STAT` / `I_MASK` routing;
- three bounded root counters with explicit cycle stepping and target IRQ behavior;
- seven DMA register banks plus DPCR/DICR state;
- bounded supported DMA for CD-ROM → RAM and RAM → GPU;
- a direct-disc CD-ROM controller backed by the live `Ps1DiscSession`;
- indexed CD register/FIFO behavior and bounded sector reads from the user's image;
- GP0/GP1 GPU command ingress with deterministic control/status state;
- CPU GP0 writes and DMA2 words sharing the same GPU ingress path;
- explicit unsupported frontiers for device behavior not yet implemented;
- an end-to-end synthetic contract covering timer IRQ → CD-ROM read → DMA3 to RAM → DMA2 to GP0 while proving the source image is unchanged.

The final Phase 3 gate performs full Release builds and the complete CTest graph on Linux and Windows x64.

## What Phase 3 does not claim

Synthetic fixtures prove deterministic hardware contracts and portability. They do **not** prove that the commercial JoJo image is fully playable.

Still deferred:

- full GPU rasterization and VRAM presentation;
- GTE geometry execution required by the commercial rendering path;
- first real rendered commercial frame;
- SPU/audio fidelity;
- original-game controller/save integration;
- complete gameplay validation;
- native x64 recompiler/backend promotion and release optimization.

These are addressed by later phases, beginning with **Phase 4 — GPU/GTE + first rendered frame**.

## User data

Per-user configuration and diagnostics are stored under:

```text
%LOCALAPPDATA%\JOJO Recompiled\
```

The persistent source binding is metadata only. No original game assets are copied into the repository or release package.

## Build on Windows

See [`docs/BUILD-WINDOWS.md`](docs/BUILD-WINDOWS.md).

## Architecture / roadmap

Historical design and plan files under `docs/superpowers/` remain in Git as project history. The active product direction is the PS1 direct-source runtime, deterministic commercial evidence workflow, and title-scoped PS1 hardware services described above.

## Verification

All phase changes are gated by CMake/CTest. Phase 3 adds a read-only final gate that verifies the active PS1 architecture, the integrated hardware-services contract, a complete Release build, and the full test graph on both Linux and Windows x64.
