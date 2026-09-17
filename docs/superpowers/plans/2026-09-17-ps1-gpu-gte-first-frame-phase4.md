# PS1 GPU + GTE First-Frame Phase 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Advance the direct PS1 runtime from Phase 3 hardware services to a deterministic commercial JoJo frame by implementing COP2/GTE state and the GPU/VRAM path actually exercised by the game.

**Architecture:** Keep `R3000aState` as the semantic CPU oracle and add an explicit PS1 GTE state/command surface behind COP2 instructions. Evolve `Ps1GpuIngress` into a packetized GPU core with owned 1024x512 16-bit VRAM, keeping CPU MMIO and DMA on the same GP0/GP1 ingress. Presentation remains a host concern: extract the PS1 display region from VRAM and hand an immutable host frame to the existing Windows/D3D11 presentation layer.

**Tech Stack:** C++20, CMake/CTest, PS1 R3000A reference executor, direct `Ps1DiscSession`, D3D11/DXGI on Windows, GitHub Actions Linux + Windows x64.

**Spec:** Approved Phase 4 design in the 2026-09-17 project conversation: 4A GTE/COP2 foundation, 4B JoJo-used GTE commands, 4C GPU+VRAM, 4D presentation bridge, 4E commercial first-frame gate.

## Global Constraints

- PS1 target only; no Dreamcast path.
- The user supplies the legal `.bin/.cue/.iso`; source media remains read-only.
- No Sony BIOS distribution and no permanent extraction of proprietary game assets.
- CPU MMIO GP0 and DMA channel 2 must converge on the same GPU ingress.
- Implement GTE/GPU behavior frontier-first from observed JoJo needs; do not build unused PS1 functionality speculatively.
- Preserve deterministic reference execution and explicit unsupported boundaries.
- Every production behavior change follows RED -> GREEN -> REFACTOR.
- Phase completion requires Linux Release and Windows x64 Release full CTest green on the exact final head.
- Phase 4 is complete only after a reproducible non-empty commercial JoJo frame is produced through the direct-disc runtime.

---

### Task 1: 4A COP2/GTE register foundation

**Files:**
- Modify: `src/core/r3000a_state.h`
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_cop2.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `R3000aGte { data[32], control[32], last_command, flag }` embedded as `R3000aState::gte`.
- COP2 enabled when COP0 Status.CU2 is set.
- `MTC2/CTC2` write GTE registers immediately; `MFC2/CFC2` use the ordinary one-instruction GPR load delay.
- `cop2_command` remains an explicit `cop2_unimplemented` boundary until Task 2.

- [ ] **Step 1: Write the failing test**

Create `tests/test_r3000a_cop2.cpp` covering CU2-unusable exception, data/control register writes, delayed reads, and command boundary.

```cpp
// Core contract example
s.cop0.status = 1u << 30u;
s.gpr[8] = 0x12345678u;
bus.store32(0x1000u, cop2(0x04u, 8u, 3u)); // MTC2 r8 -> D3
CHECK(step_r3000a(s, bus).status == R3000aStepStatus::retired);
CHECK(s.gte.data[3] == 0x12345678u);
```

- [ ] **Step 2: Run test to verify RED**

Run CI target `jojo_r3000a_cop2_tests` on Linux. Expected: compile failure because `R3000aState::gte` does not exist.

- [ ] **Step 3: Implement minimal COP2 register semantics**

Add the GTE register state and replace the current all-COP2 boundary block with transfer semantics while retaining the existing CU2 exception and command boundary.

- [ ] **Step 4: Verify GREEN**

Run `jojo_r3000a_cop2_tests`, then full CTest on Linux and Windows x64.

- [ ] **Step 5: Commit**

`feat: add PS1 COP2 GTE register foundation`

---

### Task 2: 4B Frontier-driven GTE command core

**Files:**
- Create: `src/core/ps1_gte.h`
- Create: `src/core/ps1_gte.cpp`
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_ps1_gte.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `Ps1GteCommandResult execute_ps1_gte_command(R3000aGte&, uint32_t raw_command) noexcept`.
- Return distinguishes `retired` from `unsupported_command` and preserves exact command bits for diagnostics.
- First implemented command set is chosen from the next commercial frontier; initial synthetic gate requires RTPS and a minimal arithmetic dependency chain only when evidence demands it.

- [ ] **Step 1: Write a failing unit test for the first observed command** with fixed register inputs and exact register/FLAG outputs.
- [ ] **Step 2: Verify RED** as an unsupported-command boundary.
- [ ] **Step 3: Implement only that command with explicit fixed-point/clamp helpers.**
- [ ] **Step 4: Re-run the commercial frontier; add one RED/GREEN cycle per newly observed GTE command.**
- [ ] **Step 5: Stop expanding once execution leaves GTE and reaches the next non-GTE frontier; commit.**

---

### Task 3: 4C GPU packet assembly and PS1 VRAM

**Files:**
- Create: `src/core/ps1_gpu.h`
- Create: `src/core/ps1_gpu.cpp`
- Modify: `src/core/ps1_gpu_ingress.h`
- Modify: `src/core/ps1_gpu_ingress.cpp`
- Modify: `src/core/ps1_hardware_services.*` only if ownership wiring is required
- Create: `tests/test_ps1_gpu_vram.cpp`
- Extend: `tests/test_ps1_gpu_ingress.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces 1024x512 `uint16_t` VRAM owned by the GPU core.
- `write_gp0(uint32_t)` feeds one packet assembler used identically by CPU MMIO and DMA channel 2.
- GP0 environment commands update draw state.
- VRAM fill/copy/upload and only the primitive packet types observed by JoJo are implemented.
- Unsupported packet opcodes remain explicit diagnostics and must not mutate VRAM.

- [ ] **Step 1: RED for VRAM fill and packet word assembly.**
- [ ] **Step 2: GREEN minimal VRAM core.**
- [ ] **Step 3: RED/GREEN CPU-to-VRAM transfer.**
- [ ] **Step 4: RED/GREEN the first JoJo primitive packet, then iterate frontier-first.**
- [ ] **Step 5: Prove DMA and MMIO produce identical VRAM state for the same GP0 words; commit.**

---

### Task 4: 4D Display-frame extraction and Windows presentation bridge

**Files:**
- Create: `src/core/ps1_video_frame.h`
- Create: `src/core/ps1_video_frame.cpp`
- Modify: `src/core/ps1_gpu.*`
- Modify: `src/app_win32/presentation_host.h`
- Modify: `src/app_win32/presentation_host.cpp`
- Create/extend: `tests/test_ps1_video_frame.cpp`, `tests/test_win32_presentation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces immutable `Ps1VideoFrame { width, height, rgba8 }` extracted from GP1 display start/range/mode plus VRAM.
- Core conversion is platform-independent and deterministic.
- Windows host uploads that frame to D3D11; PS1 emulation code never includes D3D headers.

- [ ] **Step 1: RED deterministic 15-bit VRAM -> RGBA8 conversion and display crop.**
- [ ] **Step 2: GREEN platform-independent frame extraction.**
- [ ] **Step 3: RED Windows host contract for texture upload/presentation plumbing.**
- [ ] **Step 4: GREEN D3D11 bridge without changing PS1 semantics.**
- [ ] **Step 5: Full CTest and commit.**

---

### Task 5: 4E Commercial JoJo first-frame gate

**Files:**
- Modify: `src/core/ps1_direct_runtime.*` and/or `ps1_boot_runtime.*` for frame-stop evidence only as needed
- Extend: `src/core/ps1_commercial_evidence.*`
- Create: `tests/test_ps1_first_frame_gate.cpp`
- Create: `.github/workflows/phase4-final-gate.yml`
- Update: `PROJECT-STATE.md`, `README.md`

**Interfaces:**
- Produces bounded evidence containing instruction count, last PC/opcode, GTE command counts, GP0/GP1 counts, VRAM mutation count, frame dimensions, and deterministic frame hash.
- Synthetic test proves the gate can distinguish empty/unmodified VRAM from a valid non-empty frame.
- Commercial run uses the original user-provided disc path directly and never writes game assets.

- [ ] **Step 1: RED synthetic first-frame gate.**
- [ ] **Step 2: GREEN evidence/frame hashing.**
- [ ] **Step 3: Run commercial JoJo frontier and iterate Tasks 2/3 only on concrete missing operations until a frame is produced.**
- [ ] **Step 4: Add read-only Phase 4 final workflow: architecture guard + Release build + first-frame integration + full CTest on Linux and Windows x64.**
- [ ] **Step 5: Update canonical project state only after exact-head green verification and commit.**

## Self-Review

- Spec coverage: 4A, 4B, 4C, 4D, 4E each map to one independently testable task.
- No proprietary media/assets are added to the repository.
- COP2/GTE and GPU remain deterministic reference components suitable as semantic oracles for the later x64 backend.
- CPU GP0 and DMA GP0 share one ingress path.
- No claim of Phase 4 completion is permitted without a reproducible commercial frame and exact-head Linux/Windows green gates.
