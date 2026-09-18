# JOJO Recompiled — Production Architecture

## Product contract

The end user launches one Windows x64 application: `JOJO-Recompiled.exe`.

The active guest platform is **Sony PlayStation 1** and the product scope is the supported JoJo PS1 revision family only. This is not a general-purpose PlayStation emulator.

The user supplies a legally obtained `.iso`, `.bin`, or `.cue`. The source remains read-only and authoritative. Runtime settings, diagnostics and raw PS1 Memory Cards live in application-owned writable user directories.

The repository, CI, artifacts and releases contain no commercial game image, extracted PS-X EXE, proprietary PlayStation BIOS, copyrighted artwork, music, stages, or unrestricted guest-memory dumps.

## Current program state

The project has moved beyond the original M1 direct-source foundation.

### Phase 1 — direct-source foundation

Complete. The shipping runtime resolves the source directly through a persistent binding, `Data/ROM` discovery or file selection. `SYSTEM.CNF`, PS-X EXE metadata and runtime sectors are consumed without a prepared extracted installation.

### Phase 2 — deterministic commercial frontier

Complete. The direct-disc commercial runner owns the media session and reports explicit BIOS/MMIO/CD-ROM/DMA/GPU/CPU frontiers with bounded diagnostic evidence.

### Phase 3 — hardware-services foundation

Complete. Interrupts, root counters, DMA, title-scoped CD-ROM, GPU ingress and direct-media ownership are integrated behind the PS1 bus.

### Phase 4 — GPU/GTE/rendering foundation

Implementation exists and is synthetic-test covered: GTE/COP2, GP0/GP1 state, VRAM transfers, rasterization, texture sampling, display extraction and D3D11 presentation.

The acceptance boundary remains strict: a current non-empty commercial frame must be observed locally before rendering is considered commercially verified.

### Phase 5 — SPU/audio

Implementation and Linux/Windows CI are complete for the current synthetic contracts: SPU register/RAM model, DMA4, ADPCM, pitch, loop/ENDX, ADSR, 44.1 kHz stereo mixing and Windows XAudio2 output.

Commercial audio evidence is still required.

### Phase 6 — controls/timing/saves

The continuous Windows runtime now integrates:

- SIO0 controller and Memory Card transport;
- two digital controller ports;
- keyboard/XInput/HID → JoJo PS1 button bridge;
- raw 128 KiB PS1 `.mcr` persistence;
- VBlank IRQ0 and SIO IRQ7 behavior;
- PAL/NTSC/interlace-aware timing driven by GP1 display mode;
- bounded frame execution slices for host responsiveness;
- periodic Memory Card flushes;
- continuous frame/audio/input servicing.

This phase is closed only after its latest Linux and Windows final gates are green.

## Phase 7 — current commercial gameplay validation

Phase 7 is the next evidence-driven phase.

The workflow is:

1. launch the supported user-supplied image on the latest green runtime;
2. capture the first current production frontier;
3. implement the exact missing behavior with synthetic regression coverage;
4. repeat through visible title/menu progression;
5. verify controller response, audio and Memory Card behavior in the commercial game;
6. reach and complete a representative fight;
7. preserve diagnostics for every unsupported BIOS/MMIO/CD-ROM/GPU/CPU dependency.

Broad speculative hardware emulation is not the strategy. Real title evidence controls scope.

## Phase 8 — native x64 optimization and release

The reference R3000A executor remains the correctness oracle until the gameplay path is stable.

The later native-backend program will:

- discover title code regions and control flow;
- lift supported R3000A semantics into explicit IR;
- preserve branch/load-delay and exception behavior;
- lower validated IR to Windows x64;
- bind derived caches to the exact supported revision/executable identity;
- cross-check native execution against the reference runtime;
- add final performance, packaging and release gates.

`native-codegen-ready` is not equivalent to playable or production-ready.

## Commercial evidence policy

Synthetic fixtures prove only the contracts they exercise.

Do not label the game `bootable`, `rendering`, `audio-working`, `playable`, save-compatible, or release-ready based only on unit/CI coverage. Those claims require current evidence from the supported user-supplied commercial image.

Diagnostic fallbacks must stay explicit and may not silently become production semantics.

## Retained host infrastructure

Presentation/settings/input models, mod runtime, training tools, rollback/networking utilities, ISO/media infrastructure, revision fingerprints, Windows UI plumbing, hashing and CI remain available as host-side infrastructure. They become part of original-game behavior only when explicitly wired into and validated through the PS1 runtime.

Historical Dreamcast/SH-4 plans and superseded installation-generation documents remain project history only.

## Production completion program (R2)

The machine-readable truth vocabulary remains in [`PRODUCTION-READINESS.tsv`](PRODUCTION-READINESS.tsv):

- R2.1 — repository truth/release gates;
- R2.2 — commercial revision enablement;
- R2.3 — game-specific execution/device integration;
- R2.4 — real gameplay integration;
- R2.5 — online product modes/integration;
- R2.6 — production validation/release.

A workstream is not complete because code exists. `blocked-external-evidence` is not equivalent to verified.

## Architectural rules

- Commercial source media is read-only.
- No proprietary BIOS is distributed or required by design.
- No commercial payload bytes are committed to Git/CI/releases.
- Unsupported PS1 behavior produces explicit diagnostics.
- Game requirements drive hardware/HLE scope; generic emulator completeness is a non-goal.
- Simulation state is not owned by rendering or networking.
- User saves/configuration remain separate from the commercial source image.
- Derived caches must be reproducible and disposable.
- The distributable application artifact remains one `JOJO-Recompiled.exe`.
