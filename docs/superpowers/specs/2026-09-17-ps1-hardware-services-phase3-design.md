# PS1 Hardware Services — Phase 3 Design

**Status:** approved in-chat design, written spec pending final review  
**Date:** 2026-09-17  
**Base:** Phase 2 head `2a561a1ace7ddf7cdf95d0bc2c0c4ca5743da1a6`

## Purpose

Phase 3 moves JOJO Recompiled from a diagnostic commercial-boot frontier runner to a runtime with the minimum real PlayStation 1 hardware services needed to let the supported JoJo revision advance further.

The phase remains title-specific. It does not attempt to become a general PlayStation emulator. Unsupported behavior must remain explicit and must still produce a deterministic frontier instead of receiving a silent guessed value.

## Phase boundary

Phase 3 contains four ordered subprojects without creating new top-level phases:

1. **3A — IRQ and timers**
2. **3B — DMA core**
3. **3C — CD-ROM controller backed by the live `Ps1DiscSession`**
4. **3D — GPU command ingress (GP0/GP1 and DMA handoff)**

Phase 3 ends when these services are connected to the commercial evidence runtime, are covered by deterministic tests, and the full Linux/Windows regression gates are green.

Full GPU rendering, GTE geometry execution, SPU audio, gamepad-to-game integration, save/memory-card fidelity, full gameplay, and native x64 recompilation remain outside Phase 3.

## Architectural direction

Introduce a focused `Ps1HardwareServices` layer between CPU-visible MMIO and individual PS1 devices.

```text
Ps1DiscSession
      |
      v
Ps1HardwareServices
  |- IRQ / Timers
  |- DMA
  |- CD-ROM --------> read-only sectors from BIN/CUE/ISO
  `- GPU ingress
      ^
      | MMIO
Ps1MemoryBus
      ^
      |
R3000A / Ps1BootRuntime
```

`Ps1MemoryBus` remains responsible for guest-address translation, main RAM, scratchpad, and routing MMIO accesses. Device semantics move out of ad-hoc address checks and into hardware services.

`Ps1BootRuntime` remains the R3000A execution coordinator. It does not become the owner of CD-ROM, DMA, or GPU state.

`Ps1CommercialEvidenceRunner` owns the live `Ps1DiscSession` and connects it to the hardware services for the duration of a run. This preserves direct-disc semantics and prevents permanent extraction of commercial assets.

## Core interfaces

The exact internal shape may be refined during TDD, but the ownership contract is fixed:

```cpp
class Ps1HardwareServices {
public:
    Ps1HardwareServices();

    void attach_disc(const Ps1DiscSession* disc) noexcept;

    R3000aBusResult read8(std::uint32_t physical) noexcept;
    R3000aBusResult read16(std::uint32_t physical) noexcept;
    R3000aBusResult read32(std::uint32_t physical) noexcept;

    R3000aBusResult write8(std::uint32_t physical, std::uint8_t value) noexcept;
    R3000aBusResult write16(std::uint32_t physical, std::uint16_t value) noexcept;
    R3000aBusResult write32(std::uint32_t physical, std::uint32_t value) noexcept;

    void step(std::uint32_t cpu_cycles) noexcept;
};
```

The bus routes only recognized PS1 MMIO windows to `Ps1HardwareServices`. Unrecognized MMIO remains `unsupported`, preserving frontier behavior.

The disc attachment is non-owning. The owning lifetime remains in `Ps1CommercialEvidenceRunner`, which outlives the runtime and hardware services during a diagnostic run.

## 3A — IRQ and timers

### Goals

Move existing interrupt/timer state out of scattered `Ps1MemoryBus` special cases and provide deterministic register semantics needed by the supported game path.

### Required behavior

Support the existing verified interrupt registers:

- `I_STAT` at `0x1F801070`
- `I_MASK` at `0x1F801074`

Provide bounded root-counter state for the PS1 timer block at `0x1F801100..0x1F80112F`, beginning with the timer registers actually exercised by tests/frontiers.

Required timer state per channel:

- counter value
- mode
- target
- deterministic cycle accumulator

Timer progression is driven explicitly through `Ps1HardwareServices::step(cpu_cycles)`. No wall-clock time is used.

Interrupt assertion must update `I_STAT`; CPU interrupt acceptance remains the responsibility of the existing R3000A/COP0 path.

### Non-goals

Phase 3 does not promise cycle-perfect PS1 timer behavior. Unsupported timer mode combinations remain explicit until required by JoJo evidence.

## 3B — DMA core

### Goals

Replace probe-shadow behavior for DMA registers with deterministic real state and bounded transfers.

### Register model

Support seven DMA channels using the PS1 register layout beginning at `0x1F801080`:

- `MADR`
- `BCR`
- `CHCR`

Retain and formalize:

- `DPCR` at `0x1F8010F0`
- `DICR` at `0x1F8010F4`

### Transfer policy

A DMA transfer only executes when:

1. the corresponding channel is enabled in `DPCR`;
2. `CHCR` indicates a supported active transfer mode;
3. source/destination and word count are within bounded RAM/device limits;
4. the target device path is implemented.

Unsupported sync modes, directions, or devices stop at an explicit frontier rather than being treated as successful.

DMA accounting must increment `Ps1BootReport::dma_transfer_count` only for completed supported transfers.

DMA completion updates DICR flags and IRQ state deterministically.

### Initial supported device handoffs

Phase 3 requires enough routing for:

- CD-ROM -> RAM transfers required by direct-disc reads;
- RAM -> GPU GP0 transfers required by command ingress.

Other DMA channels may expose register state but must not fake successful transfers.

## 3C — CD-ROM controller

### Goals

Provide a minimal PS1 CD-ROM controller that reads directly from the already-open user image without extracting files or sectors to a permanent installation.

### Backing media

The controller consumes the live `Ps1DiscSession`. Sector payloads are fetched on demand through its existing logical-sector interface.

The controller must never:

- write to the source image;
- cache raw commercial sectors as permanent product assets;
- require a Sony BIOS;
- require an extracted `boot.psxexe`.

### Register surface

Implement the PS1 CD-ROM register window at `0x1F801800..0x1F801803` with indexed register semantics sufficient for the supported command subset.

Maintain bounded FIFOs for:

- parameter bytes;
- response bytes;
- data bytes;

Maintain deterministic controller state:

- current index/status;
- command state;
- interrupt enable/flag state;
- current logical sector/LBA;
- pending read state.

### Command subset

The first implementation targets the commands required for commercial boot/data access and synthetic validation, including the minimum useful path around:

- status/query behavior;
- set-location behavior;
- sector read initiation;
- pause/stop/init behavior when encountered by the supported path.

A command is added only with a failing test or observed JoJo frontier demonstrating need. Unknown commands return `device_command_unimplemented` evidence instead of a guessed success response.

### Data path

CD-ROM sector readiness feeds the CD DMA channel. DMA then copies the bounded sector payload into PS1 main RAM.

The controller owns no commercial data after the bounded transfer buffer is consumed.

## 3D — GPU command ingress

### Goals

Make GP0/GP1 command ports real runtime endpoints so the commercial path can advance beyond immediate GPU-port MMIO frontiers.

### Register surface

Implement:

- GP0 at `0x1F801810`
- GP1 at `0x1F801814`

Maintain bounded GPU ingress state:

- GP0 command word count;
- GP1 command word count;
- GPU status register state;
- reset/display control state required by encountered commands;
- bounded command packet assembly;
- VRAM write accounting metadata.

### Command policy

Phase 3 accepts and classifies command packets; it implements only command semantics needed to maintain coherent control/status and feed later rendering work.

Commands requiring real rasterization or GTE output are preserved as explicit GPU command frontiers for Phase 4 unless their control-side semantics are necessary to keep boot progressing.

RAM -> GPU DMA delivers words to the same GP0 ingress path used by CPU writes.

### Phase boundary

`presented_frames` does not need to become non-zero in Phase 3. First real rendered frame remains a Phase 4 objective.

## Evidence and frontier integration

The Phase 2 frontier taxonomy remains authoritative.

When an MMIO access reaches a recognized Phase 3 device:

- supported operations execute and the runtime continues;
- unsupported CD commands produce `cdrom_command`;
- unsupported DMA operations produce `dma_operation`;
- unsupported GP0/GP1 commands produce the corresponding GPU frontier;
- unknown MMIO remains `mmio_access`.

Normal shipping diagnostics never enable speculative MMIO shadow behavior to bypass a real hardware dependency.

Diagnostic-only probing may remain available to engineering tests, but its events must remain marked speculative and must not affect normal-mode results.

## Reporting

`Ps1BootReport` remains bounded. Phase 3 extends reporting only when necessary to distinguish real progress:

- DMA transfer count reflects completed supported transfers;
- recent CD-ROM command summaries are bounded;
- GP0/GP1 counters reflect accepted command words/packets according to the final TDD contract;
- interrupt count remains tied to actual CPU-accepted interrupts;
- unsupported device behavior remains represented by a stable stop reason and frontier class.

No raw RAM dump, VRAM dump, disc-sector dump, or executable payload is added to the compact commercial evidence report.

## Error handling

Device code must not throw through the CPU execution loop.

Recoverable unsupported behavior returns an explicit unsupported/device status that the runtime maps to a frontier.

Invalid or out-of-range DMA requests fail deterministically and must not perform partial unbounded memory copies.

CD-ROM reads outside the logical-sector source fail without mutating RAM.

If the live disc session is absent, CD data commands stop with media/device evidence instead of returning fabricated data.

## Testing strategy

All implementation follows RED -> GREEN -> refactor.

### 3A tests

Prove:

- I_STAT/I_MASK semantics survive extraction from `Ps1MemoryBus`;
- timer counter/mode/target register behavior is deterministic;
- explicit stepping advances supported timer modes;
- timer IRQ assertion updates I_STAT;
- unsupported timer modes remain visible rather than guessed.

### 3B tests

Prove:

- all seven DMA channel register banks have stable reset/read/write behavior;
- DPCR/DICR behavior remains compatible with existing tests;
- disabled channels do not transfer;
- bounded supported transfers update RAM/device state and count once;
- invalid or unsupported modes stop explicitly;
- completion flags/IRQ behavior are deterministic.

### 3C tests

Use synthetic PS1 ISO/BIN/CUE fixtures to prove:

- CD register/index semantics;
- command/response FIFO bounds;
- location + read path fetches the expected logical sector;
- CD DMA moves only the requested bounded data into RAM;
- source bytes are unchanged before/after;
- unknown command creates a CD-ROM frontier.

### 3D tests

Prove:

- GP0/GP1 MMIO no longer falls through to generic unsupported access for implemented control commands;
- reset/status commands update deterministic GPU state;
- unsupported render commands remain explicit GPU frontiers;
- GPU DMA and CPU GP0 writes share one ingress path;
- counters remain bounded and deterministic.

### Integration tests

A synthetic direct-disc program must be able to exercise an ordered path across Phase 3 services without diagnostic MMIO probing.

`Ps1CommercialEvidenceRunner` must retain source integrity and produce a frontier beyond each newly implemented synthetic dependency.

## CI and completion gate

Phase 3 is complete only when the final published implementation head passes:

### Linux

```bash
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j2
ctest --test-dir build-linux --output-on-failure
```

### Windows x64

```powershell
cmake -S . -B build-win -A x64
cmake --build build-win --config Release
ctest --test-dir build-win -C Release --output-on-failure
```

The Windows shipping executable must also build with the same hardware-services path used by the commercial evidence runner.

## Success criteria

Phase 3 is complete when all of the following are true:

1. Phase 2 remains green and its commercial evidence API is preserved.
2. `Ps1MemoryBus` no longer owns ad-hoc primary semantics for IRQ/timer/DMA/GPU/CD device behavior targeted by Phase 3.
3. Real deterministic state exists for IRQ/timers and DMA registers targeted by the phase.
4. A live direct-disc CD-ROM data path can feed RAM through supported DMA without permanent extraction.
5. GP0/GP1 command ingress accepts the supported control/data path and integrates with GPU DMA.
6. Unsupported behavior remains an explicit stable frontier.
7. No speculative MMIO bypass is silently enabled in normal mode.
8. Full Linux and Windows x64 build/CTest gates pass on the final branch head.

## Explicitly deferred to Phase 4+

- full GPU rasterization;
- GTE instruction implementation needed for scene geometry;
- first commercial rendered frame;
- SPU/audio fidelity;
- controller integration with original game logic;
- memory-card/save fidelity beyond any prerequisite metadata;
- complete gameplay validation;
- x64 native recompiler/backend promotion.
