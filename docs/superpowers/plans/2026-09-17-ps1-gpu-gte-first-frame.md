# PS1 GPU + GTE First Commercial Frame Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Advance the JoJo PS1 direct runtime from hardware ingress to a reproducible first commercial game frame by implementing the required COP2/GTE state and operations, real PS1 VRAM/GP0 drawing, and a D3D11 presentation bridge.

**Architecture:** Keep the R3000A reference executor as the semantic oracle and attach an explicit GTE state to `R3000aState`. Evolve `Ps1GpuIngress` into a deterministic packet/state/VRAM core shared by CPU GP0 writes and DMA. Expose a host-neutral display-frame snapshot and let the Win32/D3D11 layer present it. Implement only operations proven necessary by the JoJo commercial frontier, preserving explicit boundaries for unsupported behavior.

**Tech Stack:** C++20, CMake/CTest, Win32, D3D11/DXGI, GitHub Actions Linux + Windows x64.

**Spec:** Approved Phase 4 design from the project conversation; canonical prior state is `PROJECT-STATE.md` at commit `35ca296f49784edb4f33e9b8a9c798a856234f22`.

## Global Constraints

- Target is the PlayStation 1 version of JoJo only.
- Original `.bin/.cue/.iso` media remains read-only and user-supplied.
- No Sony BIOS distribution and no proprietary game asset extraction as shipping UX.
- One shipping Windows executable remains the product shape.
- TDD is mandatory: test RED before production GREEN for each behavior.
- Unsupported PS1 behavior remains an explicit diagnostic boundary; do not silently approximate unknown semantics.
- Do not claim Phase 4 complete until a reproducible non-empty commercial JoJo frame is produced from the legal source image.

---

### Task 1: 4A — COP2/GTE register foundation

**Files:**
- Modify: `src/core/r3000a_state.h`
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_r3000a_cop2.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `R3000aGte` with 32 data and 32 control registers, plus architectural transfer semantics for `MFC2`, `CFC2`, `MTC2`, and `CTC2`.
- Preserves: CU2-disabled instructions raise Coprocessor Unusable with CE=2.

- [ ] Step 1: Add focused tests proving CU2-disabled exception behavior still holds and CU2-enabled transfer instructions mutate/read the intended GTE register.
- [ ] Step 2: Run only the new COP2 target and verify RED because `R3000aState` has no GTE state and transfers still return `cop2_unimplemented`.
- [ ] Step 3: Add `R3000aGte` state and minimal transfer semantics. `MFC2/CFC2` use the existing delayed-load model; `MTC2/CTC2` write immediately.
- [ ] Step 4: Run the COP2 target and the existing R3000A exception/boundary targets; verify GREEN.
- [ ] Step 5: Commit `feat: add PS1 COP2 GTE register transfers`.

### Task 2: 4B — Frontier-driven GTE command execution

**Files:**
- Create: `src/core/ps1_gte.h`
- Create: `src/core/ps1_gte.cpp`
- Modify: `src/core/r3000a_reference_executor.cpp`
- Create: `tests/test_ps1_gte.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `R3000aGte` from Task 1.
- Produces: `execute_ps1_gte_command(R3000aGte&, uint32_t raw)` returning supported/unimplemented status and maintaining FLAG deterministically.

- [ ] Step 1: Add RED tests for the first GTE command encountered by the commercial frontier, including exact register/FLAG results from a small deterministic vector fixture.
- [ ] Step 2: Verify RED at the current commercial frontier.
- [ ] Step 3: Implement only that command and shared saturation/FLAG helpers needed by it.
- [ ] Step 4: Re-run frontier evidence; repeat RED→GREEN command-by-command until the boot advances to GPU drawing work or a different non-GTE boundary.
- [ ] Step 5: Commit each independently meaningful GTE command family; finish with full GTE + R3000A regression GREEN.

### Task 3: 4C — PS1 VRAM and GP0 packet renderer

**Files:**
- Modify: `src/core/ps1_gpu_ingress.h`
- Modify: `src/core/ps1_gpu_ingress.cpp`
- Create: `tests/test_ps1_gpu_vram.cpp`
- Extend: `tests/test_ps1_gpu_ingress.cpp`
- Extend: `tests/test_ps1_hardware_dma.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: 1024×512 16-bit PS1 VRAM ownership inside the GPU core, packet assembly across GP0 words, drawing environment state, CPU→VRAM transfer support, and frontier-driven primitive rasterization.
- Preserves: CPU GP0 and DMA channel 2 feed the same packet parser.

- [ ] Step 1: RED test for CPU→VRAM image transfer writing exact 16-bit pixels.
- [ ] Step 2: Implement packet assembly and transfer mode; verify GREEN.
- [ ] Step 3: RED test for fill rectangle / first primitive required by JoJo and implement exact clipping/mask behavior needed by the frontier.
- [ ] Step 4: Iterate primitive/texture commands strictly from commercial evidence until drawing commands no longer stop the frontier.
- [ ] Step 5: Run GPU ingress, GPU VRAM, DMA and hardware integration suites; commit `feat: add PS1 VRAM packet renderer` plus incremental primitive commits as appropriate.

### Task 4: 4D — Host-neutral display frame and D3D11 bridge

**Files:**
- Modify: `src/core/ps1_gpu_ingress.h`
- Modify: `src/core/ps1_gpu_ingress.cpp`
- Modify: `src/core/presentation.h`
- Modify: `src/app_win32/presentation_host.h`
- Modify: `src/app_win32/presentation_host.cpp`
- Modify: `src/app_win32/main.cpp`
- Create: `tests/test_ps1_gpu_display.cpp`
- Extend: `tests/test_win32_presentation.cpp`

**Interfaces:**
- Produces: a deterministic host-neutral frame snapshot derived from GP1 display start/range/mode plus VRAM; Win32 presentation uploads/presents that frame with D3D11.

- [ ] Step 1: RED test for extracting a known display rectangle from synthetic VRAM into host RGBA pixels.
- [ ] Step 2: Implement host-neutral frame extraction and verify GREEN on Linux.
- [ ] Step 3: RED Windows presentation test for D3D11 texture/upload plan integration, without requiring a visible desktop in CI.
- [ ] Step 4: Implement the D3D11 upload/present path while keeping aspect/window policy outside PS1 GPU semantics.
- [ ] Step 5: Run Linux core tests and Windows x64 presentation/full build; commit `feat: present PS1 VRAM through D3D11`.

### Task 5: 4E — First commercial JoJo frame gate

**Files:**
- Modify as evidence requires: `src/core/ps1_gte.*`, `src/core/ps1_gpu_ingress.*`, `src/core/ps1_hardware_services.*`, `src/core/ps1_cdrom.*`, `src/core/ps1_boot_runtime.*`
- Extend: commercial evidence/frontier tests and report structures.
- Create: `.github/workflows/phase4-first-frame-gate.yml`
- Modify: `README.md`, `PROJECT-STATE.md`

**Interfaces:**
- Produces: evidence identifying a frame boundary, non-empty frame hash, dimensions, instruction count/frontier state, and reproducibility metadata without embedding proprietary image content.

- [ ] Step 1: Add a failing commercial-evidence assertion that requires a produced non-empty display frame rather than a synthetic GPU test.
- [ ] Step 2: Run the legal JoJo commercial frontier and record the next concrete missing behavior.
- [ ] Step 3: Resolve missing behaviors one-by-one with separate RED→GREEN tests; never bypass a frontier with hardcoded game-specific output.
- [ ] Step 4: When a real frame is produced, store only derived evidence such as dimensions/hash/counters, not frame pixels from copyrighted content.
- [ ] Step 5: Add a read-only Phase 4 CI gate: architecture guard, Release build, Phase 4 focused tests, full CTest on Linux + Windows x64. Update project state only after both pass.

## Self-review

- Spec coverage: COP2/GTE, VRAM/GP0, presentation, commercial first-frame evidence and cross-platform gates are all mapped to tasks.
- Placeholder scan: no TBD/TODO implementation gaps are used as plan steps.
- Type consistency: Task 2 consumes the `R3000aGte` state created in Task 1; Tasks 3–4 share `Ps1GpuIngress`; Task 5 validates their integrated runtime behavior.
