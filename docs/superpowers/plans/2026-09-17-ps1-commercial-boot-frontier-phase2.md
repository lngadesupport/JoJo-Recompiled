# PS1 Commercial Boot Frontier Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic direct-disc commercial evidence runner that classifies the first unsupported JoJo PS1 runtime dependency, records bounded engineering evidence, isolates diagnostic fallbacks, and writes a compact report without extracting commercial assets.

**Architecture:** Add a focused `Ps1CommercialEvidenceRunner` that owns both `Ps1DiscSession` and `Ps1BootRuntime`, keeping the source media alive for the entire diagnostic run. A separate frontier classifier maps existing `Ps1BootReport` evidence into stable high-level classes, while a dedicated formatter persists only metadata, bounded traces/events, and explicit diagnostic decisions. The shipping Windows checkpoint action is redirected to this runner; normal mode never guesses, while diagnostic BIOS fallbacks are opt-in and recorded.

**Tech Stack:** C++20, CMake/CTest, Win32, GitHub Actions, existing PS1 R3000A/HLE/media infrastructure.

**Spec:** `docs/superpowers/specs/2026-09-17-ps1-commercial-boot-frontier-m7-design.md`

## Global Constraints

- The original user-supplied PS1 image remains read-only and authoritative.
- No extracted installation, `boot.psxexe`, `generations/`, proprietary BIOS, ROM acquisition, or copied commercial assets may be introduced.
- Normal mode stops at unsupported behavior; speculative diagnostic fallbacks are opt-in and always recorded.
- Evidence buffers are bounded; reports contain metadata and engineering summaries, not raw commercial payloads or large memory dumps.
- The implementation remains JoJo-specific and must not expand into a general PlayStation emulator.
- Full CMake/CTest regression must pass on Linux and Windows x64 before Phase 2 is considered complete.

---

### Task 1: Stable commercial frontier taxonomy

**Files:**
- Create: `src/core/ps1_commercial_frontier.h`
- Create: `src/core/ps1_commercial_frontier.cpp`
- Create: `tests/test_ps1_commercial_frontier.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Ps1BootReport`, `Ps1BootStopReason`, `Ps1UnsupportedAccess`, `Ps1MemoryBus::guest_to_physical()`.
- Produces:
  - `enum class Ps1CommercialFrontierClass : std::uint8_t`
  - `Ps1CommercialFrontierClass classify_ps1_commercial_frontier(const Ps1BootReport&) noexcept`
  - `std::string_view ps1_commercial_frontier_class_name(Ps1CommercialFrontierClass) noexcept`

- [ ] **Step 1: Write the failing classifier test**

Create table-driven cases proving these mappings:

```cpp
CHECK(classify(report_with(Ps1BootStopReason::bios_call_unimplemented)) ==
      Ps1CommercialFrontierClass::bios_call);
CHECK(classify(mmio_at(0x1F801080u)) == Ps1CommercialFrontierClass::dma_operation);
CHECK(classify(mmio_at(0x1F801810u)) == Ps1CommercialFrontierClass::gpu_gp0_command);
CHECK(classify(mmio_at(0x1F801814u)) == Ps1CommercialFrontierClass::gpu_gp1_command);
CHECK(classify(mmio_at(0x1F801070u)) == Ps1CommercialFrontierClass::mmio_access);
CHECK(classify(report_with(Ps1BootStopReason::diagnostic_stall)) ==
      Ps1CommercialFrontierClass::diagnostic_stall);
CHECK(classify(report_with(Ps1BootStopReason::commercial_frame_presented)) ==
      Ps1CommercialFrontierClass::commercial_frame_presented);
```

Also prove `execution_budget_exhausted`, `cpu_boundary`, and `fatal_runtime_error` remain distinct stable classes rather than being mislabeled as hardware frontiers.

- [ ] **Step 2: Run the target and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_ps1_commercial_frontier_tests -j2
```

Expected: build fails because `ps1_commercial_frontier.h` / classifier symbols do not exist.

- [ ] **Step 3: Implement the minimal classifier**

Define the enum with:

```cpp
enum class Ps1CommercialFrontierClass : std::uint8_t {
    none,
    execution_budget,
    bios_call,
    mmio_access,
    cdrom_command,
    dma_operation,
    gpu_gp0_command,
    gpu_gp1_command,
    cpu_boundary,
    diagnostic_stall,
    commercial_frame_presented,
    fatal_runtime_error,
};
```

Classification rules:

```text
bios_call_unimplemented / bios_call_unknown -> bios_call
mmio_unimplemented + physical 0x1F801080..0x1F8010FF -> dma_operation
mmio_unimplemented + physical 0x1F801810 -> gpu_gp0_command
mmio_unimplemented + physical 0x1F801814 -> gpu_gp1_command
device_command_unimplemented with recent CD-ROM evidence -> cdrom_command
other mmio_unimplemented -> mmio_access
cpu_boundary -> cpu_boundary
diagnostic_stall -> diagnostic_stall
commercial_frame_presented -> commercial_frame_presented
fatal_runtime_error -> fatal_runtime_error
execution_budget_exhausted -> execution_budget
none -> none
```

Do not infer GP0/GP1 from counters when the unsupported address does not identify the port.

- [ ] **Step 4: Run classifier tests and verify GREEN**

Run the same target and:

```bash
ctest --test-dir build -R jojo_ps1_commercial_frontier_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_commercial_frontier.* tests/test_ps1_commercial_frontier.cpp CMakeLists.txt
git commit -m "feat: classify PS1 commercial boot frontiers"
```

---

### Task 2: Direct-disc commercial evidence runner

**Files:**
- Create: `src/core/ps1_commercial_evidence.h`
- Create: `src/core/ps1_commercial_evidence.cpp`
- Create: `tests/test_ps1_commercial_evidence.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Ps1DiscSession::open()`, `Ps1BootRuntime::create()`, Task 1 classifier.
- Produces:

```cpp
struct Ps1CommercialDiagnosticDecision {
    std::uint32_t bios_table{};
    std::uint32_t bios_selector{};
    Ps1BiosFallback fallback{Ps1BiosFallback::return_zero};
};

struct Ps1CommercialEvidenceOptions {
    Ps1BootOptions boot{};
    std::vector<Ps1BiosFallback> diagnostic_bios_fallbacks;
};

struct Ps1CommercialEvidenceReport {
    GameSourceBinding source{};
    Ps1CommercialFrontierClass frontier{Ps1CommercialFrontierClass::none};
    Ps1BootReport boot{};
    std::uint64_t total_instructions_retired{};
    std::vector<Ps1CommercialDiagnosticDecision> diagnostic_decisions;
};

class Ps1CommercialEvidenceRunner {
public:
    static Result<Ps1CommercialEvidenceRunner> open(
        const std::filesystem::path& source,
        const Ps1DiscOpenOptions& open_options = {});
    Ps1CommercialEvidenceReport run(const Ps1CommercialEvidenceOptions& options) noexcept;
    const Ps1DiscSession& disc_session() const noexcept;
private:
    Ps1DiscSession disc_{};
    Ps1BootRuntime runtime_{};
};
```

- [ ] **Step 1: Write RED tests**

Use `tests/ps1_fixture.h` to prove:

1. `open()` validates the fixture through `Ps1DiscSession` and exposes the same binding.
2. `disc_session().read_sectors(...)` still works after runtime creation, proving media lifetime is retained.
3. Normal mode returns the first real `Ps1BootReport` stop and never applies a fallback.
4. Source bytes/hash are identical before and after the run.
5. Event/trace capacities remain bounded by the existing `Ps1BootOptions` capacities.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_ps1_commercial_evidence_tests -j2
```

Expected: compile/link failure because the runner does not exist.

- [ ] **Step 3: Implement normal-mode runner**

`open()` must:

```cpp
auto disc = Ps1DiscSession::open(source, open_options);
auto runtime = Ps1BootRuntime::create(disc.value.boot_executable());
```

Move both into the runner. `run()` with an empty fallback vector executes exactly one `runtime_.run(options.boot)` segment, copies `disc_.binding()` into the report, classifies the frontier, and returns without mutating the disc.

- [ ] **Step 4: Run evidence tests GREEN**

```bash
ctest --test-dir build -R 'jojo_ps1_(commercial_evidence|commercial_frontier)_tests' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_commercial_evidence.* tests/test_ps1_commercial_evidence.cpp CMakeLists.txt
git commit -m "feat: add direct-disc commercial evidence runner"
```

---

### Task 3: Explicit diagnostic BIOS fallback recording

**Files:**
- Modify: `src/core/ps1_commercial_evidence.cpp`
- Modify: `tests/test_ps1_commercial_evidence.cpp`

**Interfaces:**
- Consumes: `Ps1BootRuntime::apply_diagnostic_bios_fallback()` and `Ps1BootReport::recent_bios_calls`.
- Produces: ordered `Ps1CommercialDiagnosticDecision` entries and accumulated `total_instructions_retired`.

- [ ] **Step 1: Add a failing synthetic BIOS-frontier test**

Construct a fixture whose program reaches an unimplemented A0/B0/C0 selector. Verify:

```cpp
options.diagnostic_bios_fallbacks = {
    Ps1BiosFallback::return_zero,
    Ps1BiosFallback::return_one,
};
const auto report = runner.run(options);
CHECK(!report.diagnostic_decisions.empty());
CHECK(report.diagnostic_decisions.front().bios_selector == expected_selector);
CHECK(report.diagnostic_decisions.front().fallback == Ps1BiosFallback::return_zero);
CHECK(report.total_instructions_retired >= report.boot.instructions_retired);
```

A control run with an empty fallback vector must still stop at `bios_call` with zero decisions.

- [ ] **Step 2: Verify RED**

Run only `jojo_ps1_commercial_evidence_tests`; expected failure is missing diagnostic decision behavior.

- [ ] **Step 3: Implement bounded fallback loop**

For each supplied fallback:

1. call `runtime_.run(options.boot)`;
2. add segment retired count to `total_instructions_retired`;
3. if frontier is not BIOS, return immediately;
4. capture the most recent BIOS table/selector;
5. call `apply_diagnostic_bios_fallback(fallback)`;
6. if it returns false, stop with the BIOS frontier rather than guessing;
7. append the exact decision and continue.

After the provided fallback sequence is exhausted, run no additional speculative behavior: return the current BIOS frontier. Never enable MMIO probing implicitly.

- [ ] **Step 4: Verify GREEN and bounded behavior**

Run:

```bash
ctest --test-dir build -R jojo_ps1_commercial_evidence_tests --output-on-failure
```

Expected: PASS with normal-mode and diagnostic-mode contracts both proven.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_commercial_evidence.cpp tests/test_ps1_commercial_evidence.cpp
git commit -m "feat: record explicit PS1 diagnostic fallback decisions"
```

---

### Task 4: Compact frontier report serialization

**Files:**
- Create: `src/core/ps1_commercial_evidence_io.h`
- Create: `src/core/ps1_commercial_evidence_io.cpp`
- Create: `tests/test_ps1_commercial_evidence_io.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Ps1CommercialEvidenceReport`, existing boot/fallback name helpers.
- Produces:

```cpp
std::string format_ps1_commercial_evidence_report(
    const Ps1CommercialEvidenceReport& report);
Result<void> save_ps1_commercial_evidence_report_atomic(
    const std::filesystem::path& path,
    const Ps1CommercialEvidenceReport& report);
```

- [ ] **Step 1: Write RED serialization tests**

Require output to contain source format, source size, FNV hash, revision id, frontier class, stop reason, total retired count, PC/opcode, bounded BIOS/MMIO/CD-ROM/GPU/DMA counters, trace samples, and every diagnostic decision.

Also require the text **not** to contain the synthetic PS-X EXE payload marker or a hex/raw dump of the full executable buffer.

- [ ] **Step 2: Verify RED**

Build/run `jojo_ps1_commercial_evidence_io_tests`; expected missing symbols.

- [ ] **Step 3: Implement formatter and atomic writer**

Follow the existing `save_ps1_boot_report_atomic()` temporary-file + replace pattern. Serialize summaries line-by-line; do not serialize RAM, VRAM, sectors, executable payloads, or arbitrary file contents.

- [ ] **Step 4: Verify GREEN**

Run the IO target plus existing boot report IO tests.

- [ ] **Step 5: Commit**

```bash
git add src/core/ps1_commercial_evidence_io.* tests/test_ps1_commercial_evidence_io.cpp CMakeLists.txt
git commit -m "feat: persist compact commercial frontier evidence"
```

---

### Task 5: Redirect the Windows diagnostic action to Phase 2 evidence

**Files:**
- Modify: `src/app_win32/main.cpp`
- Modify: `tests/test_win32_image_selection.cpp`
- Modify: `README.md`

**Interfaces:**
- Consumes: `Ps1CommercialEvidenceRunner`, `save_ps1_commercial_evidence_report_atomic()`.
- Produces: `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/commercial-frontier.txt` from the already validated direct source.

- [ ] **Step 1: Extend the Win32 contract test RED**

Require the shipping entry point to reference `Ps1CommercialEvidenceRunner` / commercial evidence report output and to no longer call `bootstrap_runtime_checkpoint_from_disc_to_file()` for the user-facing diagnostic button.

- [ ] **Step 2: Verify RED on Windows x64**

Configure MSVC x64 and build/run `jojo_win32_image_selection_tests`; expected contract failure against the old checkpoint implementation.

- [ ] **Step 3: Implement the UI integration**

Keep the button label user-friendly, but have `run_checkpoint()`:

1. `Ps1CommercialEvidenceRunner::open(fs::path(source), open_options)`;
2. run with normal (non-speculative) evidence options;
3. save `commercial-frontier.txt`;
4. display the stable frontier class and report path in the status/log area.

Do not enable diagnostic fallbacks in shipping UI.

- [ ] **Step 4: Update README Phase status**

Document Phase 1 as complete and Phase 2 evidence runner as implemented, while explicitly stating that a synthetic green suite does not prove the commercial game is playable and that local user-supplied JoJo evidence is still required to identify the first real frontier.

- [ ] **Step 5: Verify Win32 GREEN and commit**

Run the Win32 contract test, then commit:

```bash
git add src/app_win32/main.cpp tests/test_win32_image_selection.cpp README.md
git commit -m "feat: expose commercial boot frontier diagnostics on Windows"
```

---

### Task 6: Full regression and Phase 2 completion gate

**Files:**
- Modify only if regression exposes a real defect in Phase 2 code.

**Interfaces:**
- Consumes all Phase 2 targets.
- Produces a verified branch suitable as the base for Phase 3.

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

- [ ] **Step 3: Verify architectural invariants**

Search shipping sources/CMake and require absence of `convert_image`, `active_install.ini`, `boot.psxexe`, `generations/`, and automatic diagnostic fallback enabling.

- [ ] **Step 4: Record Phase 2 status**

Phase 2 technical infrastructure is complete only when both platform gates are green. The commercial-evidence criterion remains explicitly dependent on running the supported user-supplied JoJo image locally; if no legal image is available to CI/session tooling, report that as the only remaining evidence input rather than fabricating a frontier.

- [ ] **Step 5: Use the first real frontier to scope Phase 3**

If commercial evidence is available, name the next Phase 3 work from the observed class/address/selector/command. If not, Phase 3 implementation may proceed only on hardware semantics already demonstrated by synthetic/repository evidence, never by guessing what the commercial game will request.
