# JOJO Recompiled — Project State

## Canonical line

- Repository: `lngadesupport/JoJo-Recompiled`
- Active development branch: `feature/ps1-hardware-services-phase3`
- Guest platform: **Sony PlayStation 1**
- Product scope: **JoJo PS1 only**
- Shipping policy: one `JOJO-Recompiled.exe`
- User data source: user-supplied legal `.iso`, `.bin`, or `.cue`, opened read-only
- No Sony BIOS, disc image, extracted PS-X EXE, artwork, music, or other commercial game assets are distributed.

## Phase status

### Phase 1 — Direct-source foundation: complete

The shipping path reads the original PS1 image directly. Persistent source binding and `Data/ROM` discovery are active. The legacy extracted installation/conversion path is retired from the shipping `jojo_core` graph and default CTest graph.

### Phase 2 — Commercial boot frontier/evidence: complete

`Ps1CommercialEvidenceRunner` owns the direct disc session and boot runtime during diagnostic execution. It classifies deterministic BIOS/MMIO/DMA/CD-ROM/GPU/CPU frontiers, uses bounded evidence buffers, and writes compact derived diagnostics without commercial payload dumps. Normal mode does not silently enable speculative MMIO behavior.

Phase 2 completion head: `2a561a1ace7ddf7cdf95d0bc2c0c4ca5743da1a6`.

### Phase 3 — PS1 hardware services: complete

A focused `Ps1HardwareServices` layer now handles title-scoped device semantics behind `Ps1MemoryBus`.

Implemented and covered by synthetic contracts:

- `I_STAT` / `I_MASK` state and IRQ routing;
- three deterministic root counters with explicit cycle stepping and supported target/reset/IRQ behavior;
- seven DMA channel register banks (`MADR`, `BCR`, `CHCR`) plus DPCR/DICR;
- bounded DMA channel 3 CD-ROM → RAM transfer;
- bounded DMA channel 2 RAM → GPU GP0 transfer;
- direct-disc CD-ROM controller using the live `Ps1DiscSession` as its read-only media backend;
- bounded indexed CD parameter/response/data FIFO behavior and sector reads;
- GP0/GP1 command ingress with deterministic control/status state;
- explicit unsupported behavior for unimplemented device modes/commands instead of fabricated success;
- integrated synthetic path: timer IRQ → CD sector read → DMA3 to RAM → DMA2 to GP0;
- source-image integrity check proving the fixture image remains unchanged.

Phase 3 final-gate code head before documentation closeout: `1fa3f00cc2464d3ea14907b470b8fce9bd9f0e0d`.
GitHub Actions final gate: `35199471906`.
Both jobs passed:

- Linux Release: architecture guard, full build, Phase 3 integration contract, complete CTest graph;
- Windows x64 Release: full build, Phase 3 integration contract, complete CTest graph.

## Current truth boundary

The following remain outside the completed Phase 3 contract:

- full GPU rasterization / VRAM presentation;
- GTE execution required for the commercial rendering path;
- first real commercial frame;
- SPU/audio fidelity;
- original-game controller integration;
- memory-card/save fidelity;
- full commercial menus/fights/gameplay validation;
- production MIPS CFG/IR execution and Windows x64 native code generation;
- final optimization/release packaging.

Synthetic tests prove only the behavior they exercise. Phase 3 does not by itself prove that the user's commercial JoJo image reaches gameplay.

## Roadmap

1. **Phase 1 — Direct-source foundation** — complete
2. **Phase 2 — Commercial boot frontier/evidence** — complete
3. **Phase 3 — PS1 hardware services** — complete
4. **Phase 4 — GPU/GTE + first rendered frame** — next
5. **Phase 5 — SPU/audio**
6. **Phase 6 — controls/timing/saves**
7. **Phase 7 — complete gameplay validation**
8. **Phase 8 — native x64 optimization and Windows release**

## Next priority

Phase 4 must build on the Phase 3 GP0/GP1 ingress rather than introducing a parallel graphics path. The next architecture should preserve the current direct-disc/hardware-services ownership, add bounded VRAM/GPU command execution and the JoJo-required GTE subset, and make the first real commercial frame an explicit completion gate.

Unsupported graphics/GTE behavior must remain observable rather than receiving guessed semantics. The reference R3000A runtime remains the semantic oracle until later native-backend promotion.
