# PS1 Hardware Services — Phase 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the minimum deterministic PlayStation 1 IRQ/timer, DMA, CD-ROM, and GPU-ingress services required for JoJo's commercial boot path while preserving explicit frontiers for unsupported behavior.

**Architecture:** Introduce `Ps1HardwareServices` as the CPU-visible device/MMIO layer. `Ps1MemoryBus` keeps address translation/RAM/scratchpad and delegates recognized device windows to hardware services. `Ps1CommercialEvidenceRunner` retains ownership of the live `Ps1DiscSession` and attaches it to hardware services so CD reads remain direct and read-only.

**Tech Stack:** C++20, CMake/CTest, existing R3000A reference runtime, PS1 direct-disc media stack, GitHub Actions, Windows x64 + Linux.

**Spec:** `docs/superpowers/specs/2026-09-17-ps1-hardware-services-phase3-design.md`

## Global Constraints

- Supported source media remain `.iso`, `.bin`, and `.cue`; the original user image is read-only and authoritative.
- No extracted game installation, Sony BIOS, permanent raw-sector cache, or copied commercial asset may be introduced.
- Normal mode never enables speculative MMIO shadowing to bypass a hardware dependency.
- Unsupported hardware behavior remains an explicit stable frontier.
- Device execution is deterministic and driven by guest CPU cycles, never wall-clock time.
- DMA and FIFO operations are bounded before mutation.
- Phase 3 does not implement full GPU rasterization, GTE, SPU, controller-to-game integration, gameplay validation, or x64 recompilation.
- Phase 3 is complete only after full Linux and Windows x64 CMake/CTest gates pass on the final published head.

---

### Task 1: Extract IRQ/timer semantics into `Ps1HardwareServices` (3A)

**Files:**
- Create: `src/core/ps1_hardware_services.h`
- Create: `src/core/ps1_hardware_services.cpp`
- Create: `tests/test_ps1_hardware_irq_timers.cpp`
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `class Ps1HardwareServices` with `read8/16/32`, `write8/16/32`, `step(uint32_t)`, `interrupt_status()`, `interrupt_mask()`, and timer inspection helpers.
- `Ps1MemoryBus` owns one `Ps1HardwareServices` and delegates recognized MMIO to it.
- `Ps1BootRuntime` advances hardware by one deterministic CPU cycle per retired reference instruction during Phase 3.

- [ ] **Step 1: Write RED IRQ/timer tests**

Require reset state and register behavior:

```cpp
jojo::Ps1HardwareServices hw;
CHECK(hw.read16(0x1F801074u).value == 0u);
CHECK(hw.write16(0x1F801074u, 0xFFFFu).status == jojo::R3000aBusStatus::ok);
CHECK(hw.read16(0x1F801074u).value == 0x07FFu);
CHECK(hw.write16(0x1F801070u, 0u).status == jojo::R3000aBusStatus::ok);

CHECK(hw.write16(0x1F801108u, 3u).status == jojo::R3000aBusStatus::ok);
CHECK(hw.write16(0x1F801104u, 0x0058u).status == jojo::R3000aBusStatus::ok);
hw.step(3u);
CHECK(hw.timer_counter(0u) == 3u);
```

Also require timer target/reset/IRQ behavior for one supported deterministic mode and `unsupported` for an unsupported mode combination.

- [ ] **Step 2: Verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_ps1_hardware_irq_timer_tests -j2
```

Expected: compile failure because `ps1_hardware_services.h` does not exist.

- [ ] **Step 3: Implement minimal 3A hardware services**

Use three root-counter structs:

```cpp
struct Ps1RootCounterState {
    std::uint16_t counter{};
    std::uint16_t mode{};
    std::uint16_t target{};
    std::uint32_t cycle_accumulator{};
};
```

Implement `I_STAT`, `I_MASK`, and timer blocks at `0x1F801100 + channel * 0x10`. Only mode combinations covered by RED tests are treated as supported. `step()` updates counters and raises timer IRQ bits deterministically.

- [ ] **Step 4: Route bus MMIO and preserve legacy contracts**

Move existing I_STAT/I_MASK/timer special cases out of `Ps1MemoryBus` into `Ps1HardwareServices`. Keep `Ps1MemoryBus::interrupt_mask()`, `timer1_counter()`, and `timer1_mode()` as compatibility delegators while existing tests migrate.

- [ ] **Step 5: Verify GREEN**

Run hardware, memory-bus, boot-runtime and diagnostic-frontier tests.

- [ ] **Step 6: Commit**

Commit message: `feat: add PS1 IRQ and timer hardware services`.

---

### Task 2: Add deterministic seven-channel DMA core (3B)

**Files:**
- Modify: `src/core/ps1_hardware_services.h`
- Modify: `src/core/ps1_hardware_services.cpp`
- Create: `tests/test_ps1_hardware_dma.cpp`
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Add seven `Ps1DmaChannelState { madr, bcr, chcr }` entries.
- Add DPCR/DICR state and deterministic completion/IRQ helpers.
- Hardware services expose a bounded RAM transfer callback/interface supplied by the bus rather than owning RAM.

- [ ] **Step 1: Write RED DMA-register tests**

Prove all seven banks at `0x1F801080 + channel * 0x10` read/write `MADR/BCR/CHCR`, DPCR reset is `0x07654321`, and DICR acknowledge/master-flag semantics remain compatible with current tests.

- [ ] **Step 2: Verify RED**

Build `jojo_ps1_hardware_dma_tests`; expected missing DMA behavior.

- [ ] **Step 3: Implement register model and bounded transfer request**

Represent a transfer as:

```cpp
struct Ps1DmaTransferRequest {
    std::uint8_t channel{};
    std::uint32_t madr{};
    std::uint32_t words{};
    bool from_ram{};
};
```

Only emit a request when DPCR enables the channel and CHCR matches a supported mode. Unsupported modes return `unsupported` without clearing start bits or mutating RAM/device state.

- [ ] **Step 4: Implement completion**

On successful bounded device handoff, increment DMA completion accounting, clear active state, update DICR channel flag, and assert the DMA interrupt bit in I_STAT when enabled.

- [ ] **Step 5: Verify GREEN**

Run new DMA tests plus memory-bus, boot-runtime, frontier and Phase 2 evidence tests.

- [ ] **Step 6: Commit**

Commit message: `feat: add bounded PS1 DMA core`.

---

### Task 3: Add direct-disc CD-ROM controller and CD DMA (3C)

**Files:**
- Create: `src/core/ps1_cdrom.h`
- Create: `src/core/ps1_cdrom.cpp`
- Create: `tests/test_ps1_cdrom.cpp`
- Modify: `src/core/ps1_hardware_services.h`
- Modify: `src/core/ps1_hardware_services.cpp`
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `src/core/ps1_commercial_evidence.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `Ps1CdromController::attach_disc(const Ps1DiscSession*) noexcept` is non-owning.
- Implement indexed MMIO at `0x1F801800..0x1F801803` with bounded parameter/response/data FIFOs.
- Supported command path begins with Getstat (`0x01`), Setloc (`0x02`), ReadN (`0x06`), Pause (`0x09`), Stop (`0x08`) and Init (`0x0A`) only when covered by tests/frontiers.
- Expose bounded `read_data_words(...)` to DMA channel 3.

- [ ] **Step 1: Write RED register/FIFO tests**

Prove index selection, parameter bounds, Getstat response, Setloc state, unknown-command frontier, and no media mutation.

- [ ] **Step 2: Write RED direct-sector test**

Using `tests/ps1_fixture.h`, Setloc + ReadN must make exactly one expected logical sector available from the live session without writing a permanent file.

- [ ] **Step 3: Verify RED**

Build/run `jojo_ps1_cdrom_tests`; expected missing controller symbols.

- [ ] **Step 4: Implement minimal controller**

Translate Setloc BCD MSF to logical LBA with PS1 lead-in handling. Keep FIFO capacities fixed and reject overflow deterministically. Unknown commands set device evidence rather than returning success.

- [ ] **Step 5: Wire CD DMA**

Channel 3 device-to-RAM transfers consume only the requested bounded words from the CD data FIFO. Validate RAM range before the first write; source image bytes must remain unchanged.

- [ ] **Step 6: Attach live session from commercial runner**

After `Ps1CommercialEvidenceRunner::open()` creates disc+runtime, attach `&disc_` to runtime hardware services before execution.

- [ ] **Step 7: Verify GREEN**

Run CD-ROM, DMA, direct-disc, commercial-evidence and streaming tests.

- [ ] **Step 8: Commit**

Commit message: `feat: add direct-disc PS1 CD-ROM services`.

---

### Task 4: Add GP0/GP1 command ingress and GPU DMA (3D)

**Files:**
- Create: `src/core/ps1_gpu_ingress.h`
- Create: `src/core/ps1_gpu_ingress.cpp`
- Create: `tests/test_ps1_gpu_ingress.cpp`
- Modify: `src/core/ps1_hardware_services.h`
- Modify: `src/core/ps1_hardware_services.cpp`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `src/core/ps1_boot_report.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- GP0 physical port: `0x1F801810`; GP1 physical port: `0x1F801814`.
- `Ps1GpuIngress::write_gp0(uint32_t)` and `write_gp1(uint32_t)` share the same packet/control path used by DMA.
- GPU DMA channel 2 RAM->GPU feeds `write_gp0()`.
- No rasterizer is introduced.

- [ ] **Step 1: Write RED GPU-ingress tests**

Require GP1 reset/status control, bounded counters, CPU GP0 writes, and DMA GP0 writes to use identical ingress state.

- [ ] **Step 2: Verify RED**

Build `jojo_ps1_gpu_ingress_tests`; expected missing ingress symbols.

- [ ] **Step 3: Implement control-side semantics**

Implement reset/control/status commands needed by tests. Parse enough GP0 packet headers to determine bounded expected length. Commands requiring unsupported rendering return explicit GPU command status after preserving packet metadata.

- [ ] **Step 4: Wire GPU DMA**

Channel 2 RAM->GPU reads a validated bounded RAM range and sends every word through `write_gp0()`; count one DMA transfer only after complete success.

- [ ] **Step 5: Update boot report accounting**

Populate `gpu_gp0_command_count`, `gpu_gp1_command_count`, and supported VRAM-write metadata only from real ingress events.

- [ ] **Step 6: Verify GREEN**

Run GPU, DMA, boot-runtime, commercial-frontier and commercial-evidence tests.

- [ ] **Step 7: Commit**

Commit message: `feat: add PS1 GPU command ingress`.

---

### Task 5: End-to-end Phase 3 synthetic runtime path

**Files:**
- Create: `tests/test_ps1_hardware_integration.cpp`
- Modify: `src/core/ps1_boot_runtime.cpp` only if integration exposes a Phase 3 defect.
- Modify: `CMakeLists.txt`

**Interfaces:**
- Uses direct-disc synthetic fixture and real `Ps1CommercialEvidenceRunner`.
- Exercises implemented MMIO without `diagnostic_mmio_probe`.

- [ ] **Step 1: Write integration RED**

Construct a synthetic PS-X EXE that configures an implemented timer/IRQ register, programs a supported DMA/CD or GPU path, then advances beyond the old MMIO frontier. Require `diagnostic_probe_mode == false` and zero speculative MMIO count.

- [ ] **Step 2: Verify RED**

Run only `jojo_ps1_hardware_integration_tests`; any generic MMIO frontier before the expected endpoint is a failure.

- [ ] **Step 3: Fix only real Phase 3 integration defects**

Do not add speculative fallback behavior. Each fix must correspond to the failing synthetic dependency.

- [ ] **Step 4: Verify GREEN**

Run all `jojo_ps1_hardware_*`, CD-ROM, GPU, commercial-evidence and direct-disc tests.

- [ ] **Step 5: Commit**

Commit message: `test: prove Phase 3 direct-disc hardware path`.

---

### Task 6: Full regression and Phase 3 completion gate

**Files:**
- Modify only if a regression reveals a real Phase 3 defect.
- Update: `README.md` and `PROJECT-STATE.md` after all technical gates are green.

- [ ] **Step 1: Linux full gate**

```bash
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j2
ctest --test-dir build-linux --output-on-failure
```

Expected: all pass.

- [ ] **Step 2: Windows x64 full gate**

```powershell
cmake -S . -B build-win -A x64
cmake --build build-win --config Release
ctest --test-dir build-win -C Release --output-on-failure
```

Expected: all pass.

- [ ] **Step 3: Verify shipping architecture guards**

Require no `convert_image`, extracted `boot.psxexe`, installation runtime, or implicit diagnostic-MMIO bypass in shipping entry/runtime paths.

- [ ] **Step 4: Update status docs**

Mark Phase 3 complete only if Steps 1-3 are fresh green evidence. State explicitly that first real rendered commercial frame remains Phase 4.

- [ ] **Step 5: Final commit**

Commit message: `docs: close PS1 hardware services Phase 3`.

## Self-review

- Spec coverage: 3A IRQ/timers, 3B DMA, 3C direct-disc CD-ROM, 3D GPU ingress, frontier preservation, direct-disc ownership, reporting, bounded operations, integration and dual-platform completion gates all map to explicit tasks.
- Placeholder scan: no TODO/TBD/"implement later" requirements remain.
- Type consistency: `Ps1HardwareServices`, non-owning disc attachment, DMA request model, CD DMA channel 3 and GPU DMA channel 2 are consistently referenced across tasks.
- Scope: rasterization/GTE/SPU/controller/gameplay/native x64 remain explicitly deferred and are not completion criteria for Phase 3.
