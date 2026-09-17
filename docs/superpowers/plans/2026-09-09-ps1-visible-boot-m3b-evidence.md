# JOJO PS1 M3B Bounded Evidence Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the verified M3A checkpoint invokable from `JOJO-Recompiled.exe` and export a bounded derived diagnostic report that identifies the first real JoJo BIOS/MMIO/CPU boundary without exporting commercial bytes.

**Architecture:** Reuse the approved `Ps1BootReport` as the sole evidence source. Add a portable formatter/atomic writer, add an installation-backed `bootstrap_runtime_checkpoint_to_file` helper, then expose one explicit Win32 `EXECUTAR CHECKPOINT` button that writes `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/m3a-checkpoint.txt`. This plan does not implement any BIOS/HLE service; the actual M3B service is chosen only after a local JoJo report exists.

**Tech Stack:** C++20, CMake 3.20+, CTest, Win32, existing `jojo_core`, GitHub Actions Linux + Windows/MSVC 2022.

**Spec:** `docs/superpowers/specs/2026-09-09-ps1-visible-boot-m3-design.md`

## Global Constraints

- Product scope is **JoJo PS1 only**; do not add compatibility behavior for unrelated games.
- No proprietary PlayStation BIOS bytes, commercial JoJo bytes, sectors, or unrestricted guest-memory dumps may enter Git, CI, diagnostics, artifacts, or releases.
- Diagnostics may contain only derived bounded metadata already permitted by `Ps1BootReport`: addresses, register values, selectors, command IDs, counts, hashes and bounded summaries.
- The active installation manifest and installed game generation must not be mutated by a checkpoint run.
- The exported report path is outside the installation: `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/m3a-checkpoint.txt`.
- Default instruction budget remains exactly `10000` retired instructions.
- This plan must not claim BIOS/HLE progress, commercial boot, rendering, audio, input, gameplay or native x64 recompilation.
- Every code task follows TDD RED→GREEN and gets its own commit. Linux and Windows CI are final authorities.

---

### Task 1: Portable Bounded Boot-Report Formatter and Atomic Writer

**Files:**
- Create: `src/core/ps1_boot_report_io.h`
- Create: `src/core/ps1_boot_report_io.cpp`
- Create: `tests/test_ps1_boot_report_io.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
namespace jojo {
[[nodiscard]] std::string ps1_boot_stop_reason_name(Ps1BootStopReason reason) noexcept;
[[nodiscard]] std::string format_ps1_boot_report(const Ps1BootReport& report);
[[nodiscard]] Result<void> save_ps1_boot_report_atomic(
    const std::filesystem::path& path,
    const Ps1BootReport& report);
}
```

The text format is deterministic UTF-8 and begins with:

```text
format=jojo-m3a-checkpoint-v1
```

It must emit these scalar keys in this order:

```text
stop_reason
instructions_retired
last_pc
last_opcode
bios_call_count
interrupts_accepted
dma_transfer_count
gpu_gp0_command_count
gpu_gp1_command_count
vram_write_count
presented_frames
```

Then emit one bounded last-summary for BIOS, MMIO and CD-ROM. Missing optional values are the literal `none`. All 32-bit addresses/opcodes/values use lower-case fixed-width `0x1234abcd` formatting. Never serialize guest RAM or executable payload bytes.

- [ ] **Step 1: Write the failing formatter/writer contract**

Create `tests/test_ps1_boot_report_io.cpp`. Construct a `Ps1BootReport` with `bios_call_unimplemented`, `instructions_retired=2`, `last_pc=0x800000A0`, one BIOS summary `{0x800000A0,0x000000A0,0x3F}`, one MMIO summary `{0x80010100,0x1F801070,4,false,0}`, counters `interrupts_accepted=1`, `dma_transfer_count=2`, `gpu_gp0_command_count=3`, `gpu_gp1_command_count=4`, `vram_write_count=5`, `presented_frames=0`. Assert the formatted text contains exactly one `format=jojo-m3a-checkpoint-v1`, the exact stop reason name, `last_pc=0x800000a0`, `bios_last_selector=0x0000003f`, `mmio_last_address=0x1f801070`, and does not contain `PS-X EXE`.

Also save twice to a temporary path with different `instructions_retired` values and assert the second file completely replaces the first.

Register:

```cmake
add_jojo_test(jojo_ps1_boot_report_io_tests tests/test_ps1_boot_report_io.cpp)
```

- [ ] **Step 2: Run RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target jojo_ps1_boot_report_io_tests --config Release
```

Expected: build fails because `core/ps1_boot_report_io.h` does not exist.

- [ ] **Step 3: Implement formatter and atomic replacement**

Use `std::ostringstream`, `std::hex`, `std::setw(8)` and `std::setfill('0')` for 32-bit values. `ps1_boot_stop_reason_name` maps every current `Ps1BootStopReason` enumerator to its exact snake-case enum meaning.

`save_ps1_boot_report_atomic` creates the parent directory, writes `<target>.tmp`, flushes/closes, and atomically replaces the target. On Windows use `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`; on non-Windows use `std::filesystem::rename`. Any failure returns `ErrorCode::io_error` and removes the temporary file when possible.

Add `src/core/ps1_boot_report_io.cpp` to `jojo_core`.

- [ ] **Step 4: Run GREEN**

```bash
cmake --build build --target jojo_ps1_boot_report_io_tests --config Release
ctest --test-dir build -C Release --output-on-failure -R jojo_ps1_boot_report_io_tests
```

Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/ps1_boot_report_io.h src/core/ps1_boot_report_io.cpp tests/test_ps1_boot_report_io.cpp
git commit -m "feat: add bounded JoJo M3A checkpoint report export"
```

---

### Task 2: Installation-Backed Checkpoint-to-File API

**Files:**
- Modify: `src/core/runtime.h`
- Modify: `src/core/runtime.cpp`
- Modify: `tests/test_ps1_runtime_installation.cpp`

**Produces:**

```cpp
[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_checkpoint_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path,
    const Ps1BootOptions& options = {});
```

- [ ] **Step 1: Write failing integration test**

Extend `tests/test_ps1_runtime_installation.cpp` with a synthetic converted installation. Call `bootstrap_runtime_checkpoint_to_file(install, report_path, Ps1BootOptions{4u})`. Assert success, `instructions_retired==4`, report file exists, text contains `format=jojo-m3a-checkpoint-v1` and `instructions_retired=4`, text does not contain `PS-X EXE`, the active manifest text is byte-for-byte unchanged, and no file is written inside the active generation except pre-existing conversion content.

- [ ] **Step 2: Run RED**

```bash
cmake --build build --target jojo_ps1_runtime_installation_tests --config Release
```

Expected: build fails because `bootstrap_runtime_checkpoint_to_file` is absent.

- [ ] **Step 3: Implement by composition only**

Implement the new function as:

```cpp
auto report = bootstrap_runtime_checkpoint(install_root, options);
if (!report) return report;
auto saved = save_ps1_boot_report_atomic(report_path, report.value);
if (!saved) return Result<Ps1BootReport>::failure(saved.error, saved.detail);
return report;
```

Include `core/ps1_boot_report_io.h`. Remove the private duplicate stop-reason string mapper from `runtime.cpp` and use public `ps1_boot_stop_reason_name` in `bootstrap_runtime()`.

- [ ] **Step 4: Run GREEN**

```bash
cmake --build build --target jojo_ps1_runtime_installation_tests --config Release
ctest --test-dir build -C Release --output-on-failure -R "jojo_ps1_runtime_installation_tests|jojo_ps1_boot_report_io_tests|jojo_ps1_boot_runtime_tests"
```

Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/runtime.h src/core/runtime.cpp tests/test_ps1_runtime_installation.cpp
git commit -m "feat: export installation-backed JoJo checkpoint evidence"
```

---

### Task 3: Win32 Explicit Checkpoint Button

**Files:**
- Modify: `src/app_win32/main.cpp`
- Modify: `tests/test_win32_image_selection.cpp`

**UI contract:**
- Add control ID `1006`.
- Button text: `EXECUTAR CHECKPOINT`.
- Position: `(300, 690)`, size `300x50`.
- It is visible but disabled when the selected install is absent/invalid/legacy.
- It is enabled only when `refresh_install()` has validated a PS1 M1/M3A-compatible installation and conversion is not running.
- Clicking it runs a 10,000-instruction checkpoint and writes `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/m3a-checkpoint.txt`.

- [ ] **Step 1: Write Windows RED**

In `tests/test_win32_image_selection.cpp`, add `constexpr int ID_RUN_CHECKPOINT = 1006;`, locate it with `GetDlgItem`, assert the control exists, is visible, lies inside the client area, has text exactly `EXECUTAR CHECKPOINT`, and is disabled for the initial absent installation.

- [ ] **Step 2: Observe RED on Windows CI**

Push only the test change. Require the Windows job to fail because control `1006` is missing. Linux may remain green because the Win32 test target is not built there.

- [ ] **Step 3: Implement the button and action**

Add global `checkpoint_btn`, create the button with the exact ID/text/bounds, and add `run_checkpoint()`:

```cpp
void run_checkpoint() {
    if (!converted || running) return;
    jojo::Ps1BootOptions options{};
    options.instruction_budget = 10000u;
    const auto report_path = app_root() / L"diagnostics" / L"m3a-checkpoint.txt";
    auto result = jojo::bootstrap_runtime_checkpoint_to_file(game_dir, report_path, options);
    if (!result) {
        status = L"Checkpoint M3A falhou: " + wide(result.detail);
        add_log(L"Falha ao gerar diagnóstico derivado do checkpoint.");
    } else {
        status = L"Checkpoint M3A concluído. Relatório: " + report_path.wstring();
        add_log(L"Parada: " + wide(jojo::ps1_boot_stop_reason_name(result.value.stop_reason)));
    }
    InvalidateRect(win, nullptr, FALSE);
}
```

Include `core/ps1_boot_report_io.h`. Handle `ID_RUN_CHECKPOINT` in `WM_COMMAND`. `set_enabled(false)` disables it during conversion. At the end of `refresh_install()` call `EnableWindow(checkpoint_btn, converted && !running);`.

- [ ] **Step 4: Run GREEN authority**

Require a workflow run on the implementation commit with both `Portable core / Linux` and `Windows x64 / MSVC 2022` completed `success`.

- [ ] **Step 5: Commit**

```bash
git add src/app_win32/main.cpp tests/test_win32_image_selection.cpp
git commit -m "feat: expose JoJo M3A checkpoint diagnostics on Windows"
```

---

### Task 4: Evidence-Capture State and Final Verification

**Files:**
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`

- [ ] **Step 1: Record exact code-head CI**

Use the exact Task 3 implementation commit run. Both Linux and Windows must be `success`. Record its decimal workflow run ID in the docs.

- [ ] **Step 2: Update truth without promoting M3B**

State that the single Windows executable can now run the bounded M3A checkpoint against a validated local JoJo installation and export only derived evidence to `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/m3a-checkpoint.txt`.

State explicitly that no commercial JoJo checkpoint evidence has yet been supplied by CI and no BIOS/HLE function has been implemented by this plan.

Keep `docs/architecture/PRODUCTION-READINESS.tsv` unchanged: R2.3 remains `implemented-unverified` with immutable M3A evidence `github-actions:run-34334677057`, and R2.4 remains `not-started`.

- [ ] **Step 3: Commit docs**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md
git commit -m "docs: expose local JoJo M3B evidence capture"
```

- [ ] **Step 4: Verify final docs head independently**

Require a fresh GitHub Actions run whose `head_sha` is exactly the docs commit and both Linux/Windows jobs conclude `success`.

- [ ] **Step 5: Completion boundary**

The maximum allowed claim is:

```text
M3B evidence-capture surface ready for a local JoJo boundary report; no BIOS/HLE service is implemented yet.
```

The next implementation plan must be written only after the local report identifies the actual JoJo boundary selector/address.
