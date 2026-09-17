# PS1 MAX³ Multi-Cycle Explorer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic snapshot-based PS1 diagnostic explorer that can branch through multiple unknown BIOS calls in one user checkpoint and consolidate the resulting dependencies into one report.

**Architecture:** Keep `Ps1BootRuntime::run()` strict, add diagnostic-only frontier-resume and stagnation primitives, and implement a separate DFS explorer that clones stopped runtime snapshots and tries four BIOS return policies. Only the best node retains a heavyweight boot report; all other nodes become compact summaries.

**Tech Stack:** C++20, CMake 3.20+, synthetic PS-X EXE fixtures, GitHub Actions Linux + Windows Server 2022 / MSVC.

**Spec:** `docs/superpowers/specs/2026-09-10-ps1-max3-multicycle-design.md`

## Global Constraints

- Production `bootstrap_runtime()` remains strict and must never use speculative BIOS fallbacks.
- MAX³ local evidence uses per-segment `UINT64_MAX`, 2,000,000-instruction stagnation limit, 5,461-node cap, depth 6, and 1,000,000,000 total actually-retired instructions.
- Diagnostic branching policies are exactly: return 0, return 1, return -1, preserve `$v0`.
- Speculative behavior must be labeled diagnostic and must not become production BIOS/device semantics.
- No proprietary game data, BIOS data, RAM dump, or commercial payload is committed or serialized.
- Windows x64 Release CI is the authority for the delivered executable.

---

### Task 1: DICR real register

**Files:**
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `tests/test_ps1_memory_bus.cpp`

**Interfaces:**
- Produces: `std::uint32_t Ps1MemoryBus::dma_interrupt() const noexcept`
- Produces real `read32/write32` handling for physical `0x1F8010F4`.

- [ ] **Step 1: Write the failing DICR test**

Add assertions that reset DICR is zero, `write32(0x1F8010F4, 0x00FF807F)` succeeds and reads back writable control bits, and a diagnostic-probe-enabled write to DICR does not populate `last_diagnostic_mmio_probe()`.

- [ ] **Step 2: Run CI RED**

Expected: `jojo_ps1_memory_bus_tests` fails because DICR still falls through to diagnostic shadow and no `dma_interrupt()` accessor exists.

- [ ] **Step 3: Implement minimal DICR semantics**

Use masks:

```cpp
constexpr std::uint32_t kDmaInterruptAddress = 0x1F8010F4u;
constexpr std::uint32_t kDmaInterruptControlMask = 0x00FF807Fu;
constexpr std::uint32_t kDmaInterruptFlagMask = 0x7F000000u;
```

On write, replace control bits and clear existing 24-30 flags where the written value has 1s. Compute bit 31 on read from bit 15 or master-enable/masked channel flags. Do not create DMA flags.

- [ ] **Step 4: Run GREEN and commit**

Expected: memory-bus tests pass and existing I_STAT/I_MASK/DPCR/Timer1 tests remain green.

---

### Task 2: A0/56 + A0/72 `_96_remove()` HLE

**Files:**
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Create: `tests/test_ps1_96_remove.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `bool Ps1BootRuntime::bios_iso9660_removed() const noexcept`

- [ ] **Step 1: Write RED fixture**

Synthetic code sets `$v0=0x1234`, invokes A0/0x72 through `jalr`, then executes one instruction after return. Assert runtime continues, ISO-9660 removed state is true, and `$v0` remains `0x1234`. Repeat for alias selector 0x56.

- [ ] **Step 2: Run CI RED**

Expected: new test fails because A0/0x72 and A0/0x56 still stop as unimplemented.

- [ ] **Step 3: Implement HLE**

Add selectors `0x56` and `0x72` to `handle_bios_call()`, set the logical removal flag, call existing `return_from_bios_call()`, and never assign GPR2.

- [ ] **Step 4: Run GREEN and commit**

Expected: both aliases return via RA and preserve `$v0`.

---

### Task 3: Diagnostic BIOS frontier resume and stagnation watchdog

**Files:**
- Modify: `src/core/ps1_boot_report.h`
- Modify: `src/core/ps1_boot_runtime.h`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Create: `tests/test_ps1_diagnostic_frontier.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class Ps1BiosFallback : std::uint8_t {
    return_zero,
    return_one,
    return_minus_one,
    preserve_v0,
};

bool Ps1BootRuntime::apply_diagnostic_bios_fallback(Ps1BiosFallback) noexcept;
std::uint64_t Ps1BootRuntime::diagnostic_state_hash() const noexcept;
```

`Ps1BootOptions` gains `std::uint64_t stagnation_instruction_limit{}`. `Ps1BootStopReason` gains `diagnostic_stall`.

- [ ] **Step 1: Write RED fallback tests**

Create a fixture that stops at an unknown A0 selector. Clone the stopped runtime four times, apply each fallback, resume each clone, and assert GPR2 is 0, 1, 0xFFFFFFFF, or the original sentinel respectively. Assert fallback returns false when CPU is not at A0/B0/C0.

- [ ] **Step 2: Write RED stagnation test**

Run a self-loop with `instruction_budget=UINT64_MAX` and `stagnation_instruction_limit=64`; expect `diagnostic_stall` after 64 retired instructions.

- [ ] **Step 3: Run CI RED**

Expected: compilation/test failure for missing enum/API/stop reason.

- [ ] **Step 4: Implement fallback and watchdog**

Fallback only changes `$v0` according to the enum and uses existing BIOS return logic. The watchdog resets only for newly seen BIOS table/selector or speculative MMIO tuple in the segment; otherwise it stops at the configured limit.

- [ ] **Step 5: Run GREEN and commit**

Expected: strict default behavior remains unchanged because the new watchdog defaults to zero and fallback is explicit.

---

### Task 4: Full-state diagnostic fingerprint

**Files:**
- Modify: `src/core/ps1_memory_bus.h`
- Modify: `src/core/ps1_memory_bus.cpp`
- Modify: `src/core/ps1_boot_runtime.cpp`
- Modify: `tests/test_ps1_diagnostic_frontier.cpp`

**Interfaces:**
- Produces: `std::uint64_t Ps1MemoryBus::diagnostic_state_hash() const noexcept`
- Completes: `std::uint64_t Ps1BootRuntime::diagnostic_state_hash() const noexcept`

- [ ] **Step 1: Extend RED tests**

Two identical runtimes must hash identically. Mutating a GPR, RAM word, DICR, or diagnostic MMIO shadow must change the hash.

- [ ] **Step 2: Run RED**

Expected: missing hash API or unchanged hash after state mutation.

- [ ] **Step 3: Implement FNV-1a 64-bit hash**

Hash all state listed in the spec in deterministic byte/order form. Do not hash transient `last_unsupported_` or `last_diagnostic_mmio_probe_` metadata because those are reporting artifacts rather than guest state.

- [ ] **Step 4: Run GREEN and commit**

Expected: deterministic state equality/difference assertions pass on Linux and Windows.

---

### Task 5: Snapshot DFS MAX³ explorer

**Files:**
- Create: `src/core/ps1_max3_explorer.h`
- Create: `src/core/ps1_max3_explorer.cpp`
- Create: `tests/test_ps1_max3_explorer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct Ps1Max3Options {
    std::size_t max_nodes{5461u};
    std::size_t max_branch_depth{6u};
    std::uint64_t max_total_retired{1000000000ull};
    Ps1BootOptions segment_options{};
};

struct Ps1Max3Decision {
    std::uint32_t table{};
    std::uint32_t selector{};
    Ps1BiosFallback fallback{};
};

struct Ps1Max3NodeSummary { /* compact fields from spec */ };
struct Ps1Max3Dependency { /* BIOS or MMIO identity */ };
struct Ps1Max3Report {
    Ps1Max3Options options;
    std::uint64_t total_retired{};
    std::vector<Ps1Max3NodeSummary> nodes;
    std::vector<Ps1Max3Dependency> dependencies;
    std::size_t best_node{};
    std::vector<Ps1Max3Decision> best_path;
    Ps1BootReport best_report;
};

Ps1Max3Options ps1_max3_local_evidence_options() noexcept;
Result<Ps1Max3Report> explore_ps1_max3(const Ps1Executable&, const Ps1Max3Options&);
```

- [ ] **Step 1: RED one-frontier branching**

Use a fixture with one unknown BIOS frontier and a post-return branch on `$v0`. Assert explorer produces four child outcomes from one stopped snapshot and records all four fallback choices.

- [ ] **Step 2: RED converged-state dedup**

Use a fixture where all four return values are overwritten immediately before the next unknown BIOS call. Assert only one equivalent frontier state is expanded further.

- [ ] **Step 3: RED bounds and ranking**

Set tiny node/depth/total budgets and assert they are obeyed exactly. Create two synthetic terminal paths and assert higher progress rank wins deterministically.

- [ ] **Step 4: Implement DFS**

Use recursive/explicit-stack depth-first traversal; clone only the stopped parent runtime for each child. Maintain a visited set keyed by frontier table/selector plus runtime state hash. Merge unique speculative MMIO and unknown BIOS frontiers into a deterministic first-seen dependency vector.

- [ ] **Step 5: Run GREEN and commit**

Expected: explorer tests pass without any commercial files.

---

### Task 6: Consolidated MAX³ report and local-evidence integration

**Files:**
- Modify: `src/core/ps1_boot_report_io.h`
- Modify: `src/core/ps1_boot_report_io.cpp`
- Modify: `src/core/runtime.h`
- Modify: `src/core/runtime.cpp`
- Modify: `tests/test_ps1_boot_report_io.cpp`
- Modify: `tests/test_ps1_local_evidence.cpp`

**Interfaces:**

```cpp
std::string format_ps1_max3_report(const Ps1Max3Report&);
Result<void> save_ps1_max3_report_atomic(const std::filesystem::path&, const Ps1Max3Report&);
Result<Ps1Max3Report> bootstrap_runtime_max3_local_evidence_to_file(...);
```

`bootstrap_runtime_local_evidence_to_file(...)` remains source-compatible and returns `max3.best_report` after saving the MAX³ report.

- [ ] **Step 1: RED report tests**

Assert first line `format=jojo-max3-checkpoint-v1`, explorer limits, node summaries, dependency identities, best path decisions, and begin/end markers around the best ordinary report.

- [ ] **Step 2: RED integration test**

Synthetic installed fixture with an unknown BIOS call must create a MAX³ file, explore at least root + four child nodes, and leave manifest/generation files byte-for-byte unchanged.

- [ ] **Step 3: Implement formatter/save/integration**

Reuse `format_ps1_boot_report()` inside delimited `best_report_begin=1` / `best_report_end=1` section. Do not dump RAM or PS-X EXE bytes.

- [ ] **Step 4: Run GREEN and commit**

Expected: local evidence now produces MAX³ while production bootstrap remains strict.

---

### Task 7: Final regression, review, and Windows artifact

**Files:**
- Review all files changed since `e5d0dbd517d7cc8642ad0169d25ed65b6e1ebc25`.

- [ ] **Step 1: Run full CI on exact final HEAD**

Required jobs: Portable core / Linux and Windows x64 / MSVC 2022. All build, readiness, PS1 architecture, test, observed revision, and transport gates must succeed.

- [ ] **Step 2: Verify test count and MAX³ tests**

Confirm new DICR, `_96_remove`, diagnostic frontier/fingerprint, explorer, report, and local-evidence tests are present in the successful run.

- [ ] **Step 3: Review diff**

Confirm changes are limited to PS1 diagnostics/HLE/MMIO, tests, CMake, and design/plan docs. No proprietary data or unrelated subsystem implementation.

- [ ] **Step 4: Download Windows artifact**

Download `JOJO-Recompiled-Windows-x64`, compute local SHA-256, list ZIP contents, and verify it matches the GitHub Actions digest and contains the single expected `JOJO-Recompiled.exe`.

- [ ] **Step 5: Deliver artifact**

Provide the exact branch, final SHA, run ID, digest, and executable ZIP. Instruct the user to click `EXECUTAR CHECKPOINT` once and upload the resulting `m3a-checkpoint.txt` whose first line must be `format=jojo-max3-checkpoint-v1`.
