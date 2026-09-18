# JOJO Recompiled — Project State

## Canonical line

- Repository: `lngadesupport/JoJo-Recompiled`
- Active development branch: `feature/ps1-controls-timing-saves-phase6`
- Guest platform: **Sony PlayStation 1**
- Product scope: **JoJo PS1 only**
- Shipping policy: one `JOJO-Recompiled.exe`
- User data source: user-supplied legal `.iso`, `.bin`, or `.cue`, opened read-only
- Writable user data: settings, diagnostics, and raw PS1 Memory Cards under application-owned user directories
- No Sony BIOS, disc image, extracted PS-X EXE, artwork, music, or other commercial game assets are distributed.

## Phase status

### Phase 1 — Direct-source foundation: complete

The shipping path reads the original PS1 image directly. Persistent source binding and `Data/ROM` discovery are active. The legacy extracted installation/conversion path is retired from the shipping runtime path.

### Phase 2 — Commercial boot frontier/evidence: complete

`Ps1CommercialEvidenceRunner` retains the live read-only disc session and classifies bounded BIOS/MMIO/DMA/CD-ROM/GPU/CPU frontiers. Diagnostic fallbacks remain explicit and separate from production behavior.

### Phase 3 — PS1 hardware services: complete

The title-scoped hardware layer covers the required interrupt controller foundation, root counters, DMA register banks, CD-ROM streaming path, GPU ingress, direct-disc ownership, and explicit unsupported behavior.

Phase 3 final gate: GitHub Actions run `35199471906` — Linux and Windows x64 passed.

### Phase 4 — GPU/GTE + rendering foundation: implemented; commercial frame gate pending

Implemented and synthetic-test covered:

- JoJo-required GTE/COP2 execution foundation;
- GPU GP0/GP1 ingress and GPUSTAT state;
- VRAM transfers, fill/copy operations, rectangles and polygon rasterization used by the current title path;
- texture page/CLUT/window state and 4/8/15bpp sampling;
- display-frame extraction and D3D11 presentation;
- explicit GP0/GP1 unsupported-command evidence;
- dynamic PAL/NTSC/interlace display-mode state.

Phase 4 code and CI progressed through `261969ae05361c1244f7116f01d5490efc47879c`.
The remaining Phase 4 acceptance gate is a fresh non-empty commercial frame from the user's supported legal image. Synthetic rendering is not a substitute for that evidence.

### Phase 5 — SPU/audio: implementation and CI complete

Implemented and covered by Linux/Windows gates:

- 24 SPU voices and 512 KiB Sound RAM;
- SPU register surface, Key ON/OFF and DMA4;
- PS1 SPU-ADPCM decoding;
- pitch-driven 44.1 kHz stereo mixing;
- loop/ENDX handling and ADSR progression;
- host-neutral PCM extraction;
- Windows XAudio2 output.

Phase 5 completion code head: `353fc5b744316b40c05a9dbb57ce5e7e4007ac7d`.
Phase 5 Final Gate: GitHub Actions run `35308419254` — Linux and Windows x64 passed.

Commercial audio still requires a fresh real-image validation before it is described as proven in gameplay.

### Phase 6 — controls/timing/saves: implemented; hardening and commercial validation active

Implemented on the active branch:

- SIO0/JOY register routing and IRQ7 behavior;
- two digital PS1 controller ports;
- keyboard/XInput/HID host input bridged to JoJo's PS1 digital-pad layout;
- 128 KiB raw PS1 Memory Cards with read/write/ID SIO protocol;
- atomic `.mcr` persistence and periodic autosave;
- continuous Win32 runtime instead of a one-shot checkpoint;
- VBlank IRQ0 delivery;
- PAL/NTSC and interlace-aware frame pacing derived from GPU display mode;
- bounded per-frame execution slices for UI responsiveness;
- corrected wide SIO0 RX FIFO access semantics.

The first complete continuous-runtime Phase 6 baseline passed Linux and Windows x64 in GitHub Actions run `35310276546`.
Later commits harden timing, GPUSTAT and SIO semantics and must remain green before Phase 6 is closed.

## Current truth boundary

Synthetic and CI tests now cover substantially more than the old Phase 3 boundary, but they do **not** prove commercial gameplay.

Still requiring current commercial evidence:

- first real non-empty frame on the current runtime;
- title/menu progression on the current runtime;
- real controller response in the commercial game;
- real SPU audio during title/gameplay;
- Memory Card behavior exercised by the commercial game;
- a complete match/fight path;
- any additional BIOS/CD-ROM/GPU/CPU frontier revealed by that run.

Also not production-complete:

- MIPS CFG/IR production execution;
- Windows x64 native lowering/cache promotion;
- final performance optimization, packaging and release validation.

The most recent saved commercial checkpoint available to development predates the current GPU/SPU/SIO/runtime architecture, so it must not be treated as proof of present-day playability.

## Roadmap

1. **Phase 1 — Direct-source foundation** — complete
2. **Phase 2 — Commercial boot frontier/evidence** — complete
3. **Phase 3 — PS1 hardware services** — complete
4. **Phase 4 — GPU/GTE + rendered frame** — implementation complete; fresh commercial frame evidence pending
5. **Phase 5 — SPU/audio** — implementation and CI complete; commercial validation pending
6. **Phase 6 — controls/timing/saves** — implementation complete; final hardening/CI and commercial validation pending
7. **Phase 7 — complete gameplay validation** — next evidence-driven phase
8. **Phase 8 — native x64 optimization and Windows release** — pending

## Next priority

Finish the current Phase 6 hardening gates, then branch Phase 7 from the latest green head.

Phase 7 is frontier-driven:

1. run the supported user-supplied JoJo image on the current runtime;
2. capture the first current production frontier;
3. implement only the observed missing behavior;
4. repeat through title/menu, controller/audio/save validation and a complete fight;
5. keep every unknown BIOS/MMIO/CD-ROM/GPU/CPU behavior explicit rather than fabricating success.

The R3000A reference executor remains the semantic oracle until the later native x64 backend is proven equivalent.
