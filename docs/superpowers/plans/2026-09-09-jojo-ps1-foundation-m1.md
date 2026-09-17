# JoJo PS1 M0+M1 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the incorrect Dreamcast/SH-4 commercial path with a truthful PlayStation 1 foundation that recognizes the supported JoJo image, resolves `SYSTEM.CNF`, validates the PS-X EXE, installs required user-local data into a user-selected root, and records only verified M1 evidence.

**Architecture:** Preserve console-independent media/ISO9660/revision infrastructure, add focused PS1 `SYSTEM.CNF`, PS-X EXE, and generation-installation components, then rewrite conversion/runtime around manifest v2. Remove Dreamcast/SH-4/native-backend source and tests only after the replacement M1 path is green. This plan intentionally stops before R3000A execution: a valid M1 install is inspectable and source-independent, but not bootable.

**Tech Stack:** C++20, CMake 3.20+, CTest, Windows x64 / Visual Studio 2022, Win32/COM shell dialogs, portable Linux CI for core tests.

**Spec:** `docs/superpowers/specs/2026-09-09-jojo-ps1-architecture-migration-design.md`

## Global Constraints

- Active commercial platform is Sony PlayStation 1 only.
- The product is exclusive to this JoJo title/revision family, not a generic PS1 emulator.
- No proprietary PlayStation BIOS is required or distributed.
- Source BIN/CUE is opened read-only and is never modified.
- Git repository, tests, CI, artifacts, and releases contain no commercial game bytes or extracted proprietary assets.
- Synthetic fixtures are the only game-like bytes committed to the repository.
- User-local installed data derived from the user's own image may live only under the user-selected install root.
- `%LOCALAPPDATA%\JOJO Recompiled\game` is a suggested default, not a fixed destination.
- Dreamcast/SH-4 is absent from the final active build, test suite, workflow contracts, and current product documentation for this milestone.
- A synthetic test can prove parser/conversion contracts but cannot prove commercial boot/render/audio/input/gameplay.
- `JOJO-Recompiled.exe` remains the single downloadable application artifact.
- M1 must not claim `mips-analysis-ready`, `reference-execution-ready`, `native-codegen-ready`, `boot-reached`, `rendering-verified`, `audio-verified`, `input-verified`, or `gameplay-verified`.

---

## File Structure

### Create

- `src/core/ps1_system_cnf.h` — strict `SYSTEM.CNF` parsing and ISO9660 boot discovery.
- `src/core/ps1_system_cnf.cpp` — parser/path normalization.
- `src/core/ps1_exe.h` — PS-X EXE metadata/parser interfaces.
- `src/core/ps1_exe.cpp` — PS-X EXE validation and full-file hash.
- `src/core/ps1_installation.h` — install destination, staging, generation activation, active resolution.
- `src/core/ps1_installation.cpp` — filesystem implementation.
- `tests/ps1_fixture.h` — synthetic PS1 ISO and PS-X EXE builders.
- `tests/test_ps1_system_cnf.cpp`
- `tests/test_ps1_exe.cpp`
- `tests/test_ps1_manifest.cpp`
- `tests/test_ps1_installation.cpp`
- `tests/test_ps1_conversion.cpp`
- `tests/test_ps1_runtime_installation.cpp`
- `cmake/CheckPs1ActiveArchitecture.cmake`

### Modify

- `src/core/disc_image.cpp`
- `src/core/disc_media.cpp`
- `src/core/conversion.h`
- `src/core/conversion.cpp`
- `src/core/runtime.h`
- `src/core/runtime.cpp`
- `src/core/settings.h`
- `src/core/settings.cpp`
- `src/app_win32/main.cpp`
- `tests/iso_fixture.h`
- `tests/test_disc_media.cpp`
- `tests/test_main.cpp`
- `tests/test_observed_disc_revision.cpp`
- `tests/test_win32_image_selection.cpp`
- `CMakeLists.txt`
- `.github/workflows/build.yml`
- `README.md`
- `PROJECT-STATE.md`
- `docs/NEXT-MILESTONES.md`
- `docs/architecture/PRODUCTION-ROADMAP.md`
- `docs/architecture/PRODUCTION-READINESS.tsv`

### Delete at Task 9

Production guest/backend files:

```text
src/core/dreamcast_analysis.cpp
src/core/dreamcast_analysis.h
src/core/dreamcast_boot.cpp
src/core/dreamcast_boot.h
src/core/dreamcast_boot_runner.cpp
src/core/dreamcast_boot_runner.h
src/core/dreamcast_bus.cpp
src/core/dreamcast_bus.h
src/core/dreamcast_interrupts.cpp
src/core/dreamcast_interrupts.h
src/core/dreamcast_maple.h
src/core/dreamcast_memory.cpp
src/core/dreamcast_memory.h
src/core/dreamcast_pvr2.cpp
src/core/dreamcast_pvr2.h
src/core/dreamcast_system_asic.cpp
src/core/dreamcast_system_asic.h
src/core/sh4_cfg.cpp
src/core/sh4_cfg.h
src/core/sh4_decoder.cpp
src/core/sh4_decoder.h
src/core/sh4_interrupt_entry.cpp
src/core/sh4_ir.cpp
src/core/sh4_ir.h
src/core/sh4_opcode_census.cpp
src/core/sh4_opcode_census.h
src/core/sh4_reference_boundary.cpp
src/core/sh4_reference_bus.cpp
src/core/sh4_reference_executor.cpp
src/core/sh4_reference_executor.h
src/core/game_backend.cpp
src/core/game_backend.h
src/core/native_backend.cpp
src/core/native_backend.h
src/core/native_x64.cpp
src/core/native_x64.h
```

`native_mod_loader.*` stays; it is host-side mod loading, not the SH-4 guest backend.

---

### Task 1: Make Media Acceptance PS1-Only

**Files:**
- Modify: `src/core/disc_image.cpp`
- Modify: `src/core/disc_media.cpp`
- Modify: `tests/test_main.cpp`
- Modify: `tests/test_disc_media.cpp`

**Interfaces:**
- Consumes: `supported_disc_extension`, `open_logical_sector_source`.
- Produces: `.iso`, `.bin`, `.cue` accepted; `.gdi` rejected; raw MODE1/2352 offset 16 and MODE2/2352 offset 24 preserved.

- [ ] **Step 1: Write RED media assertions**

Replace the extension test in `tests/test_main.cpp` with:

```cpp
static void test_disc_extension_detection() {
    CHECK(jojo::supported_disc_extension("game.ISO"));
    CHECK(jojo::supported_disc_extension("game.bin"));
    CHECK(jojo::supported_disc_extension("game.cue"));
    CHECK(!jojo::supported_disc_extension("game.gdi"));
    CHECK(!jojo::supported_disc_extension("game.zip"));
}
```

Add to `tests/test_disc_media.cpp`:

```cpp
static void test_gdi_is_not_an_active_ps1_format() {
    const auto path = temp_path("rejected.gdi");
    { std::ofstream out(path); out << "1\n"; }
    const auto opened = jojo::open_logical_sector_source(path);
    CHECK(!opened);
    CHECK(opened.error == jojo::ErrorCode::unsupported_format);
    CHECK(opened.detail.find(".gdi") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
```

Use the file's existing temporary-path helper name if it differs; do not add a second helper with the same responsibility.

- [ ] **Step 2: Run RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_tests jojo_media_tests --parallel 2
ctest --test-dir build -R "jojo_tests|jojo_media_tests" --output-on-failure
```

Expected: FAIL because GDI is still accepted.

- [ ] **Step 3: Implement PS1-only acceptance**

In `src/core/disc_image.cpp`:

```cpp
bool supported_disc_extension(std::string_view filename) {
    const auto ext = lower_ext(filename);
    return ext == "iso" || ext == "bin" || ext == "cue";
}
```

Change unsupported-format text to:

```cpp
"supported PS1 formats: .iso, .bin, .cue"
```

In `src/core/disc_media.cpp`, remove the `.gdi` dispatch from `open_logical_sector_source`. Do not change existing CUE parsing or raw BIN offsets.

- [ ] **Step 4: Run GREEN**

Run the same build/CTest commands. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/disc_image.cpp src/core/disc_media.cpp tests/test_main.cpp tests/test_disc_media.cpp
git commit -m "fix: make media acceptance PS1-only"
```

---

### Task 2: Add Strict `SYSTEM.CNF` Boot Discovery

**Files:**
- Create: `src/core/ps1_system_cnf.h`
- Create: `src/core/ps1_system_cnf.cpp`
- Create: `tests/test_ps1_system_cnf.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct Ps1SystemCnf {
    std::string boot_iso_path;
};

Result<Ps1SystemCnf> parse_ps1_system_cnf(std::string_view text);
Result<Ps1SystemCnf> read_ps1_system_cnf(const Iso9660Image& image);
```

Normalized path starts with `/` and has no `;1`, e.g. `/SLUS_TEST.00`.

- [ ] **Step 1: Create an actual RED test harness**

`tests/test_ps1_system_cnf.cpp` starts with:

```cpp
#include "core/ps1_system_cnf.h"
#include <iostream>
#include <string_view>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void expect_ok(std::string_view input, std::string_view expected) {
    const auto parsed = jojo::parse_ps1_system_cnf(input);
    CHECK(parsed);
    if (parsed) CHECK(parsed.value.boot_iso_path == expected);
}

static void expect_fail(std::string_view input) {
    const auto parsed = jojo::parse_ps1_system_cnf(input);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::unsupported_format);
}

int main() {
    expect_ok("BOOT = cdrom:\\SLUS_TEST.00;1\r\n", "/SLUS_TEST.00");
    expect_ok("  boot=CDROM:\\DIR\\GAME.EXE;1  \n", "/DIR/GAME.EXE");
    expect_fail("TCB = 4\n");
    expect_fail("BOOT = cdrom:\\..\\GAME.EXE;1\n");
    expect_fail("BOOT = cdrom:\\A.EXE;1\nBOOT = cdrom:\\B.EXE;1\n");
    expect_fail("BOOT = host0:GAME.EXE\n");
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Register and run RED**

Add `src/core/ps1_system_cnf.cpp` to `jojo_core` and:

```cmake
add_executable(jojo_ps1_system_cnf_tests tests/test_ps1_system_cnf.cpp)
target_link_libraries(jojo_ps1_system_cnf_tests PRIVATE jojo_core)
add_test(NAME jojo_ps1_system_cnf_tests COMMAND jojo_ps1_system_cnf_tests)
```

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_ps1_system_cnf_tests --parallel 2
```

Expected: compile failure until the new header/interface exists.

- [ ] **Step 3: Implement parser**

Rules:

```text
trim ASCII whitespace
BOOT key case-insensitive
exactly one BOOT declaration
cdrom: prefix case-insensitive
convert backslashes to slashes
strip terminal ;1
reject host drive letters
reject empty normalized target
reject . and .. components
prepend /
malformed or ambiguous input -> ErrorCode::unsupported_format
```

`read_ps1_system_cnf` reads `/SYSTEM.CNF` with `read_iso9660_file`, converts bytes verbatim to `std::string`, then parses.

- [ ] **Step 4: Run GREEN and commit**

```bash
ctest --test-dir build -R jojo_ps1_system_cnf_tests --output-on-failure
git add src/core/ps1_system_cnf.h src/core/ps1_system_cnf.cpp tests/test_ps1_system_cnf.cpp CMakeLists.txt
git commit -m "feat: parse PS1 SYSTEM.CNF"
```

---

### Task 3: Add PS-X EXE Validation and Synthetic PS1 Fixture

**Files:**
- Create: `src/core/ps1_exe.h`
- Create: `src/core/ps1_exe.cpp`
- Create: `tests/ps1_fixture.h`
- Create: `tests/test_ps1_exe.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct Ps1ExeMetadata {
    std::uint32_t entry_pc{};
    std::uint32_t initial_gp{};
    std::uint32_t text_load_address{};
    std::uint32_t text_size{};
    std::uint32_t stack_base{};
    std::uint32_t stack_size{};
    std::string fnv1a64_hex;
};

struct Ps1Executable {
    Ps1ExeMetadata metadata;
    std::vector<std::uint8_t> file_bytes;
};

Result<Ps1Executable> parse_ps1_executable(std::span<const std::uint8_t> bytes);
Result<Ps1Executable> read_ps1_executable(const Iso9660Image& image,
                                          std::string_view iso_path);
```

- [ ] **Step 1: Add synthetic PS-X EXE builder**

`tests/ps1_fixture.h` provides a helper returning `0x800 + 16` bytes with:

```text
0x000 "PS-X EXE"
0x010 entry=0x80010000 LE32
0x014 gp=0x80018000 LE32
0x018 load=0x80010000 LE32
0x01C text_size=16 LE32
0x030 stack_base=0x801FFF00 LE32
0x034 stack_size=0x100 LE32
0x800 16 synthetic bytes owned by the test
```

- [ ] **Step 2: Write RED parser tests**

Check all six metadata values, 16-digit lowercase FNV-1a hash, and byte-for-byte preservation. Negative cases mutate synthetic buffers to reject:

```text
wrong magic
file < 0x800
text size > payload
unaligned entry PC
unaligned text load address
unaligned text size
```

- [ ] **Step 3: Register and run RED**

Add `ps1_exe.cpp` to `jojo_core` and register `jojo_ps1_exe_tests`. Expected: compile failure before implementation.

- [ ] **Step 4: Implement parser**

Use explicit little-endian reads, not a packed host struct. Hash the complete PS-X EXE file with FNV-1a 64 and render 16 lowercase hex digits. Validate bounds before every header/payload access. Do not add PS1 RAM-range checks in M1; those belong to the memory/runtime milestone.

- [ ] **Step 5: GREEN + commit**

```bash
ctest --test-dir build -R jojo_ps1_exe_tests --output-on-failure
git add src/core/ps1_exe.h src/core/ps1_exe.cpp tests/ps1_fixture.h tests/test_ps1_exe.cpp CMakeLists.txt
git commit -m "feat: validate PS-X EXE"
```

---

### Task 4: Replace Manifest v1 Native Claims with Manifest v2 Evidence

**Files:**
- Modify: `src/core/conversion.h`
- Modify: `src/core/conversion.cpp`
- Create: `tests/test_ps1_manifest.cpp`
- Modify: `CMakeLists.txt`

**Produces:**

```cpp
struct ConversionManifest {
    std::string manifest_version{"2"};
    std::string converter_version;
    std::string platform{"playstation"};
    std::string game_id{"jojo-ps1"};
    std::string source_name;
    std::string source_format;
    std::uint64_t source_size{};
    std::string source_hash_fnv1a64;
    std::string revision_id;
    std::string system_cnf_path;
    std::string boot_executable;
    std::string psx_exe_hash_fnv1a64;
    std::optional<std::uint32_t> psx_exe_entry;
    std::optional<std::uint32_t> psx_exe_load_address;
    std::optional<std::uint32_t> psx_exe_initial_gp;
    std::optional<std::uint32_t> psx_exe_text_size;
    std::optional<std::uint32_t> psx_exe_stack_base;
    std::optional<std::uint32_t> psx_exe_stack_size;
    std::string media_status{"pending"};
    std::string executable_status{"pending"};
    std::string mips_analysis_status{"pending"};
    std::string reference_runtime_status{"pending"};
    std::string native_codegen_status{"pending"};
    std::string hardware_runtime_status{"pending"};
    std::string boot_status{"pending"};
    std::string rendering_status{"pending"};
    std::string audio_status{"pending"};
    std::string input_status{"pending"};
    std::string gameplay_status{"pending"};
};
```

Remove `backend`, `boot_program_hash_hex`, backend ABI/hash/block counts, and `has_complete_native_backend_metadata`.

- [ ] **Step 1: Write RED round-trip and truth tests**

Create a fully populated synthetic v2 manifest; save/load and compare every field. Negative saves must return `invalid_installation` for:

```text
manifest_version != 2
platform != playstation
executable_status=verified while media_status!=verified
verified executable with empty system_cnf_path
verified executable with empty boot_executable
verified executable with empty psx_exe_hash_fnv1a64
verified executable missing any PS-X numeric metadata field
mips/reference/native/hardware/boot/render/audio/input/gameplay=verified while executable_status!=verified
```

Zero numeric values remain legal because zero can be present in the PS-X EXE header.

- [ ] **Step 2: Run RED**

Expected: old schema fails the new test.

- [ ] **Step 3: Implement strict v2 serialization/parsing**

Use stable keys matching field names. Addresses serialize as `0x` plus eight lowercase hex digits; sizes serialize decimal. Reject duplicate truth-sensitive keys, malformed hex, overflow, and missing required v2 keys.

- [ ] **Step 4: GREEN + commit**

```bash
ctest --test-dir build -R jojo_ps1_manifest_tests --output-on-failure
git add src/core/conversion.h src/core/conversion.cpp tests/test_ps1_manifest.cpp CMakeLists.txt
git commit -m "feat: define PS1 manifest v2"
```

---

### Task 5: Add Destination Validation and Transactional Install Generations

**Files:**
- Create: `src/core/ps1_installation.h`
- Create: `src/core/ps1_installation.cpp`
- Create: `tests/test_ps1_installation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct PendingInstallGeneration {
    std::string generation_id;
    std::filesystem::path staging_dir;
    std::filesystem::path final_dir;
};

struct ActiveInstallGeneration {
    std::filesystem::path install_root;
    std::string generation_id;
    std::filesystem::path generation_dir;
    std::filesystem::path manifest_path;
};

Result<void> validate_install_destination(const std::filesystem::path& install_root,
                                          std::uint64_t required_bytes);
Result<PendingInstallGeneration> begin_install_generation(const std::filesystem::path& install_root);
Result<void> commit_install_generation(const std::filesystem::path& install_root,
                                       const PendingInstallGeneration& generation);
Result<ActiveInstallGeneration> resolve_active_install_generation(const std::filesystem::path& install_root);
```

- [ ] **Step 1: Write RED destination tests**

Use a temp root and assert:

```text
empty path rejected
existing regular file used as install_root rejected
existing writable directory accepted
required_bytes greater than available space rejected when std::filesystem::space succeeds
```

For the space test, do not allocate huge files. Obtain `space(root).available` and call validation with `available + 1` only if increment does not overflow; otherwise skip only that assertion and keep the rest of the test active.

- [ ] **Step 2: Write RED generation tests**

Assert first ID `generation-000001`; after commit, next ID `generation-000002`. Staging path is `<root>/.staging/<id>`, final path is `<root>/generations/<id>`.

Commit generation A with a regular `game_manifest.ini`; begin B but do not commit. `resolve_active_install_generation` must still return A. Corrupt active pointer with `../escape` and assert rejection.

- [ ] **Step 3: Run RED**

Expected: interfaces missing.

- [ ] **Step 4: Implement destination validation**

`validate_install_destination`:

```text
reject empty root
reject root if it exists as a non-directory
find nearest existing ancestor for std::filesystem::space
if space() succeeds and available < required_bytes -> io_error
if space() itself is unsupported/fails -> do not fabricate free-space data; let real writes be authority
```

- [ ] **Step 5: Implement staging + atomic active pointer**

`begin_install_generation` creates `<root>/.staging/<id>` only after destination validation by the caller. `commit_install_generation` requires staging `game_manifest.ini`, renames staging to `generations/<id>` on the same volume, then writes `active_install.ini.tmp`:

```ini
format=1
generation_id=generation-000001
manifest=generations/generation-000001/game_manifest.ini
```

Replace the pointer atomically with `MoveFileExW(... MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows and `std::filesystem::rename` on non-Windows. Older valid pointer must remain untouched if pre-activation steps fail.

`resolve_active_install_generation` rejects absolute manifest paths, `..`, ID/path mismatch, missing generation directory, and missing manifest.

- [ ] **Step 6: GREEN + commit**

```bash
ctest --test-dir build -R jojo_ps1_installation_tests --output-on-failure
git add src/core/ps1_installation.h src/core/ps1_installation.cpp tests/test_ps1_installation.cpp CMakeLists.txt
git commit -m "feat: add transactional PS1 installations"
```

---

### Task 6: Rewrite Conversion as PS1 Foundation + Executable Discovery

**Files:**
- Modify: `src/core/conversion.h`
- Modify: `src/core/conversion.cpp`
- Modify: `tests/ps1_fixture.h`
- Create: `tests/test_ps1_conversion.cpp`
- Modify: `tests/test_observed_disc_revision.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class ConversionStage {
    validating_source,
    fingerprinting_source,
    discovering_filesystem,
    identifying_revision,
    reading_system_cnf,
    reading_psx_exe,
    preparing_installation,
    copying_local_data,
    writing_manifest,
    activating_installation,
    completed
};

struct ConversionOptions {
    std::vector<GameRevisionProfile> revision_profiles;
};
```

Remove `allow_unverified_base_conversion`.

- [ ] **Step 1: Make a complete synthetic PS1 ISO fixture**

`tests/ps1_fixture.h` emits:

```text
/SYSTEM.CNF
/SLUS_TEST.00
/DATA/ASSET.DAT
```

`SYSTEM.CNF`:

```text
BOOT = cdrom:\SLUS_TEST.00;1
TCB = 4
EVENT = 10
STACK = 801FFF00
```

`SLUS_TEST.00` is Task 3's synthetic PS-X EXE. Provide cooked ISO and raw MODE2/2352 variants.

- [ ] **Step 2: Write RED conversion tests**

Inject a `GameRevisionProfile` with synthetic hashes for all three fixture files. Successful conversion must create:

```text
<root>/active_install.ini
<root>/generations/generation-000001/game_manifest.ini
<root>/generations/generation-000001/data/SYSTEM.CNF
<root>/generations/generation-000001/data/boot.psxexe
```

Manifest assertions:

```cpp
CHECK(m.manifest_version == "2");
CHECK(m.platform == "playstation");
CHECK(m.revision_id == "synthetic-ps1-jojo");
CHECK(m.system_cnf_path == "/SYSTEM.CNF");
CHECK(m.boot_executable == "/SLUS_TEST.00");
CHECK(m.media_status == "verified");
CHECK(m.executable_status == "verified");
CHECK(m.mips_analysis_status == "pending");
CHECK(m.native_codegen_status == "pending");
CHECK(m.boot_status == "pending");
CHECK(m.gameplay_status == "pending");
```

Delete source after conversion; installed `data/boot.psxexe` must still equal synthetic executable bytes.

Run conversion against cooked ISO and MODE2/2352 BIN.

Failure cases:

```text
missing SYSTEM.CNF
malformed BOOT
invalid PS-X EXE magic
unknown revision
install root is an existing regular file
```

None may create/change `active_install.ini`.

- [ ] **Step 3: Run RED**

Expected: current conversion routes to Dreamcast backend or old v1 layout.

- [ ] **Step 4: Implement M1 conversion**

Production overload keeps the already observed whole-image fingerprint only:

```text
format=bin
size=666806112
fnv1a64=b8b5dbf79cdb9fcf
revision_id=jojo-usa-observed-b8b5dbf79cdb9fcf
```

No additional commercial bytes/metadata are committed.

Sequence:

```text
validate source
fingerprint source
open logical PS1 media + ISO9660
identify revision
read raw /SYSTEM.CNF + parse boot target
read/validate PS-X EXE
required_bytes = SYSTEM.CNF bytes + PS-X EXE bytes + 65536
validate selected destination
begin generation
create staging data/ and logs/
write exact SYSTEM.CNF -> data/SYSTEM.CNF
write exact executable -> data/boot.psxexe
construct v2 manifest with media/executable verified; all later statuses pending
save staging game_manifest.ini last
commit generation and active pointer
return manifest
```

Source file streams are input-only. Do not call any Dreamcast/SH-4/native-backend preparation.

- [ ] **Step 5: GREEN + commit**

```bash
ctest --test-dir build -R "jojo_ps1_conversion_tests|jojo_media_tests|jojo_iso_tests|observed" --output-on-failure
git add src/core/conversion.h src/core/conversion.cpp tests/ps1_fixture.h tests/test_ps1_conversion.cpp tests/test_observed_disc_revision.cpp CMakeLists.txt
git commit -m "feat: discover and install JoJo PS1 executable"
```

---

### Task 7: Truthful Runtime Validation + Explicit Legacy Classification

**Files:**
- Modify: `src/core/runtime.h`
- Modify: `src/core/runtime.cpp`
- Create: `tests/test_ps1_runtime_installation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
enum class InstallationKind {
    absent,
    legacy_v1,
    ps1_m1
};

struct InstallationInfo {
    std::filesystem::path install_root;
    std::filesystem::path generation_dir;
    ConversionManifest manifest;
};

Result<InstallationKind> classify_installation(const std::filesystem::path& install_root);
Result<InstallationInfo> validate_installation(const std::filesystem::path& install_root);
Result<void> bootstrap_runtime(const std::filesystem::path& install_root);
```

- [ ] **Step 1: Write RED validation tests**

For a synthetic M1 conversion, `classify_installation` returns `ps1_m1` and `validate_installation` succeeds after source deletion.

Create a root-level v1 manifest and assert `classify_installation` returns `legacy_v1`. `validate_installation` must fail with detail containing both `legacy` and `Dreamcast/SH-4`.

Other negative cases:

```text
platform != playstation
missing data/boot.psxexe
one modified executable byte -> hash mismatch
active pointer to missing generation
verified executable metadata different from re-parsed local file
```

`bootstrap_runtime` on a valid M1 install must return:

```cpp
CHECK(!boot);
CHECK(boot.error == jojo::ErrorCode::backend_unavailable);
CHECK(boot.detail.find("R3000A") != std::string::npos);
CHECK(boot.detail.find("not implemented") != std::string::npos);
```

- [ ] **Step 2: Run RED**

Expected: current runtime requires SH-4 native cache.

- [ ] **Step 3: Implement classification/validation**

Rules:

```text
no active_install.ini and no root game_manifest.ini -> absent
root game_manifest.ini manifest_version=1 -> legacy_v1
valid active pointer + v2 manifest -> ps1_m1
```

`validate_installation` resolves active generation, loads v2, requires `platform=playstation`, `media_status=verified`, `executable_status=verified`, parses local `data/boot.psxexe`, and compares stored hash/entry/load/gp/text/stack metadata.

Legacy files are not deleted or rewritten by validation.

`bootstrap_runtime` validates first, then returns explicit R3000A-not-implemented `backend_unavailable`; it creates no cache and changes no status.

- [ ] **Step 4: GREEN + commit**

```bash
ctest --test-dir build -R jojo_ps1_runtime_installation_tests --output-on-failure
git add src/core/runtime.h src/core/runtime.cpp tests/test_ps1_runtime_installation.cpp CMakeLists.txt
git commit -m "fix: validate PS1 M1 installs truthfully"
```

---

### Task 8: User-Selectable Install Root in Settings and Win32 UI

**Files:**
- Modify: `src/core/settings.h`
- Modify: `src/core/settings.cpp`
- Modify: `tests/test_main.cpp`
- Modify: `src/app_win32/main.cpp`
- Modify: `tests/test_win32_image_selection.cpp`

**Interfaces:**
- `AppSettings` contains `std::string install_root{}`.
- Settings serialize `install_root=`; legacy `install_dir=` is read-only migration input.
- UI exposes separate source-image and install-root controls.

- [ ] **Step 1: Write RED settings tests**

Round trip:

```cpp
jojo::AppSettings in{};
in.install_root = "C:/Games/JOJO-Recompiled";
CHECK(jojo::save_settings_atomic(path, in));
const auto loaded = jojo::load_settings(path);
CHECK(loaded);
if (loaded) CHECK(loaded.value.install_root == in.install_root);
```

Legacy file:

```ini
install_dir=D:/Old/JoJo
```

must load into `install_root`. Re-saving must contain `install_root=` and not `install_dir=`.

- [ ] **Step 2: Run RED and implement settings migration**

Replace `AppSettings::install_dir` with `install_root`. Parser:

```cpp
if (key == "install_root") result.install_root = value;
else if (key == "install_dir" && result.install_root.empty()) result.install_root = value;
```

Serializer writes only `install_root=`.

- [ ] **Step 3: Add RED Win32 control assertions**

Stable IDs:

```cpp
ID_SOURCE_PATH = 1001;
ID_SELECT_SOURCE = 1002;
ID_PREPARE = 1003;
ID_INSTALL_PATH = 1004;
ID_SELECT_INSTALL = 1005;
```

`tests/test_win32_image_selection.cpp` asserts both edit fields and both chooser buttons are visible/usable. Clicking install chooser must open native `#32770`; cancelling leaves install field unchanged. Source drop loop is only `.iso`, `.cue`, `.BIN`; a `.gdi` drop must not replace selection.

- [ ] **Step 4: Implement Win32 install-root UX**

Use `IFileOpenDialog` with `FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM`.

Settings path remains `<app_root>/settings.ini`. Startup:

```text
load valid settings if present
if install_root empty -> propose <app_root>/game
show proposed/stored root
classify installation at that root
```

Do not create the game root at startup.

On folder selection:

```text
update game_dir
update install-path edit
save settings atomically
refresh installation classification
```

If classification is `legacy_v1`, show explicit incompatible-legacy text and set prepare button to `RECONVERTER NESTA PASTA`; reconversion leaves old root-level v1 manifest/log untouched and creates the new generations/active pointer beside them.

If classification is `absent`, show preparation prompt. If `ps1_m1`, show:

```text
Executável PS1 identificado e instalação validada. Análise R3000A/MIPS é o próximo marco.
```

Source picker accepts `.iso;*.bin;*.cue`, not GDI. Replace fixed `%LOCALAPPDATA%` install copy with actual `game_dir` display.

- [ ] **Step 5: Run GREEN**

Portable:

```bash
ctest --test-dir build -R jojo_tests --output-on-failure
```

Windows:

```powershell
ctest --test-dir build -C Release -R jojo_win32_image_selection_tests --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/core/settings.h src/core/settings.cpp tests/test_main.cpp src/app_win32/main.cpp tests/test_win32_image_selection.cpp
git commit -m "feat: let users choose JoJo install root"
```

---

### Task 9: Remove Dreamcast/SH-4 Guest Architecture and Old Backend Contracts

**Files:**
- Create: `cmake/CheckPs1ActiveArchitecture.cmake`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/build.yml`
- Modify: `tests/iso_fixture.h`
- Modify: `tests/test_main.cpp`
- Delete: all production guest/backend files listed above.
- Delete: old guest/backend tests listed below.

Delete tests:

```text
tests/test_dreamcast_analysis.cpp
tests/test_dreamcast_boot.cpp
tests/test_dreamcast_boot_runner.cpp
tests/test_dreamcast_bus_diagnostics.cpp
tests/test_dreamcast_executable_memory.cpp
tests/test_dreamcast_irq_delivery.cpp
tests/test_dreamcast_maple.cpp
tests/test_dreamcast_pvr2_vblank.cpp
tests/test_dreamcast_system_asic.cpp
tests/test_game_backend.cpp
tests/test_native_backend.cpp
tests/test_manifest_backend_metadata.cpp
tests/test_runtime_native_backend.cpp
tests/test_unverified_conversion.cpp
tests/test_sh4_addressing_pipeline.cpp
tests/test_sh4_block_boundary.cpp
tests/test_sh4_carry_overflow.cpp
tests/test_sh4_cfg.cpp
tests/test_sh4_data_manipulation.cpp
tests/test_sh4_decoder.cpp
tests/test_sh4_flds_fsts.cpp
tests/test_sh4_float_ftrc.cpp
tests/test_sh4_fmov_memory.cpp
tests/test_sh4_fmov_registers.cpp
tests/test_sh4_fpscr_banks.cpp
tests/test_sh4_fpu_arithmetic.cpp
tests/test_sh4_fpu_compare.cpp
tests/test_sh4_fpu_completion.cpp
tests/test_sh4_fpu_constants.cpp
tests/test_sh4_fpu_disable.cpp
tests/test_sh4_fpu_mode_switches.cpp
tests/test_sh4_fpu_unary.cpp
tests/test_sh4_fpul_transfers.cpp
tests/test_sh4_gbr_memory.cpp
tests/test_sh4_gbr_register_transfer.cpp
tests/test_sh4_integer_pipeline.cpp
tests/test_sh4_interrupt_entry.cpp
tests/test_sh4_ir.cpp
tests/test_sh4_memory_bus.cpp
tests/test_sh4_memory_pipeline.cpp
tests/test_sh4_multiply.cpp
tests/test_sh4_opcode_census.cpp
tests/test_sh4_pr_mac_register_transfer.cpp
tests/test_sh4_reference_executor.cpp
```

- [ ] **Step 1: Add a RED architecture hygiene gate that allows legitimate legacy error text**

Create `cmake/CheckPs1ActiveArchitecture.cmake`:

```cmake
if(NOT DEFINED JOJO_SOURCE_DIR)
  message(FATAL_ERROR "JOJO_SOURCE_DIR is required")
endif()

file(GLOB _guest_files
  "${JOJO_SOURCE_DIR}/src/core/dreamcast_*"
  "${JOJO_SOURCE_DIR}/src/core/sh4_*")
if(_guest_files)
  message(FATAL_ERROR "Dreamcast/SH-4 guest source still exists: ${_guest_files}")
endif()

foreach(_path IN ITEMS
  "src/core/game_backend.cpp" "src/core/game_backend.h"
  "src/core/native_backend.cpp" "src/core/native_backend.h"
  "src/core/native_x64.cpp" "src/core/native_x64.h")
  if(EXISTS "${JOJO_SOURCE_DIR}/${_path}")
    message(FATAL_ERROR "Old guest backend file still exists: ${_path}")
  endif()
endforeach()

file(READ "${JOJO_SOURCE_DIR}/CMakeLists.txt" _cmake)
string(TOLOWER "${_cmake}" _cmake_lower)
if(_cmake_lower MATCHES "dreamcast_|sh4_|jojo_native_backend|jojo_game_backend")
  message(FATAL_ERROR "CMake still wires old guest architecture")
endif()

file(READ "${JOJO_SOURCE_DIR}/.github/workflows/build.yml" _workflow)
string(TOLOWER "${_workflow}" _workflow_lower)
if(_workflow_lower MATCHES "usa native backend|runtime native backend|native backend manifest|jojo_game_backend|sh4")
  message(FATAL_ERROR "Workflow still runs old guest-backend contracts")
endif()

foreach(_path IN ITEMS
  "src/core/disc_image.cpp"
  "src/core/disc_media.cpp"
  "src/app_win32/main.cpp")
  file(READ "${JOJO_SOURCE_DIR}/${_path}" _text)
  string(TOLOWER "${_text}" _lower)
  if(_path STREQUAL "src/core/disc_image.cpp" AND _lower MATCHES "ext == \"gdi\"")
    message(FATAL_ERROR "disc_image still accepts GDI")
  endif()
  if(_path STREQUAL "src/core/disc_media.cpp" AND _lower MATCHES "open_gdi_source|ext == \"\\.gdi\"")
    message(FATAL_ERROR "disc_media still dispatches GDI")
  endif()
  if(_path STREQUAL "src/app_win32/main.cpp" AND _lower MATCHES "\\*\\.gdi|l\"\\.gdi\"")
    message(FATAL_ERROR "Win32 UI still offers GDI")
  endif()
endforeach()
```

This intentionally does **not** reject human-readable `Dreamcast/SH-4` text in the runtime's legacy-installation error or negative tests.

Run:

```bash
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckPs1ActiveArchitecture.cmake
```

Expected: FAIL before cleanup.

- [ ] **Step 2: Delete guest/backend sources and targets**

Delete every listed source/test file. Remove corresponding `jojo_core` sources and CTest targets from `CMakeLists.txt`.

Keep generic media/ISO/revision/settings/presentation/input/networking/mod/training code and all new PS1 M1 components.

Strip Dreamcast-only helpers (`install_dreamcast_ip_metadata`, SH-4 payload constants, SEGAKATANA fields) from `tests/iso_fixture.h`; keep neutral ISO helpers needed by media/ISO tests or migrate them to `ps1_fixture.h`.

Remove old `game_backend` include/helpers/tests from `tests/test_main.cpp`.

- [ ] **Step 3: Replace CI contracts**

Delete workflow steps:

```text
Native backend manifest contract
Runtime native backend validation contract
USA native backend promotion contract
Unverified base conversion contract
```

CTest runs the registered new tests. Add an explicit PS1 architecture gate in Linux and Windows jobs:

```bash
cmake -DJOJO_SOURCE_DIR=${{ github.workspace }} -P cmake/CheckPs1ActiveArchitecture.cmake
```

Keep observed-disc revision contract and generic networking contract.

- [ ] **Step 4: Run clean GREEN verification**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
```

Expected: all pass; no old guest target compiles.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: remove Dreamcast SH-4 commercial architecture"
```

---

### Task 10: Correct Current Documentation, CI Evidence, and M1 Handoff

**Files:**
- Modify: `README.md`
- Modify: `PROJECT-STATE.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`
- Modify: `docs/BUILD-WINDOWS.md` only if command/target names changed.

- [ ] **Step 1: Update current-state wording to exact truth**

Current docs must state:

```text
Platform: PlayStation 1
Scope: this JoJo only
Observed USA whole-image fingerprint: recognized
PS1 media/SYSTEM.CNF/PS-X EXE M1 pipeline: implemented and synthetic-test verified
Install directory: user-selectable
Transactional active generation: implemented
R3000A execution: not implemented in M1
Native x64 codegen: not implemented in M1
Commercial PS-X EXE discovery on the user's real image: awaiting a new local run until observed
Boot/render/audio/input/gameplay: not verified
```

Do not present old Dreamcast work as active. Historical design files under `docs/superpowers/` remain as history.

- [ ] **Step 2: Run clean Linux verification**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
```

- [ ] **Step 3: Commit docs and push feature branch**

```bash
git add README.md PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-ROADMAP.md docs/architecture/PRODUCTION-READINESS.tsv docs/BUILD-WINDOWS.md
git commit -m "docs: mark JoJo PS1 M1 truthfully"
git push
```

- [ ] **Step 4: Require GitHub Actions Linux + Windows GREEN**

Windows authority must show:

```text
Configure success
Build Release success
Production readiness gate success
PS1 active architecture gate success
CTest Release success
artifact JOJO-Recompiled-Windows-x64 contains only JOJO-Recompiled.exe
```

Record actual workflow run ID and artifact digest in `PROJECT-STATE.md`; do not promote commercial M1 evidence based on CI alone.

- [ ] **Step 5: Give the user the real-image M1 test contract**

The user selects their same legally obtained PS1 BIN/CUE and any install directory. Expected success boundary:

```text
source recognized
PS1 filesystem opened
SYSTEM.CNF resolved
PS-X EXE validated
local generation installed
manifest v2 activated
R3000A/MIPS analysis pending
```

Request only application log and non-proprietary manifest metadata if diagnosis is needed. Do not request upload of the BIN, PS-X EXE, BIOS, or extracted assets.

- [ ] **Step 6: Promote commercial M1 only after real evidence**

If that real run demonstrates the M1 sequence, update only the M1 commercial evidence status. These remain unverified:

```text
R3000A execution
native x64 codegen
hardware runtime
boot
rendering
audio
input
gameplay
```

If the real run fails, do not mark M1 commercially verified; use the observed failure as the next systematic-debugging input.

- [ ] **Step 7: Commit evidence status only if observed**

```bash
git add PROJECT-STATE.md docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "docs: record verified JoJo PS1 M1 evidence"
```

Skip this commit when real-image evidence has not succeeded.

---

## Exit Criteria

M0+M1 is complete only when:

1. Linux and Windows CI are green.
2. Active build/workflow has no Dreamcast/SH-4/GDI commercial path.
3. `.iso`, `.bin`, `.cue`, raw MODE1/2352, raw MODE2/2352, and CUE data-track contracts remain green where represented by tests.
4. Synthetic `SYSTEM.CNF` parsing is green.
5. Synthetic PS-X EXE metadata/full-file hash is green.
6. Manifest v2 truth validation is green.
7. Destination validation rejects invalid roots and insufficient known free space.
8. Incomplete conversion cannot replace the old active generation.
9. Win32 UI allows choosing/persisting install root.
10. M1 installs `SYSTEM.CNF` and validated boot executable locally and remains valid after source deletion.
11. Runtime validates local PS-X EXE but explicitly reports R3000A runtime unavailable.
12. Current docs contain no false active Dreamcast/native-ready claims.
13. Commercial PS-X EXE discovery remains pending until the user's local run proves it.

## Follow-On Plan Boundaries

After M1 commercial evidence, create separate plans in this order, each using evidence from the prior milestone:

1. R3000A reference executor + PS1 memory foundation.
2. MIPS CFG + PS1 IR.
3. Windows x64 native codegen/cache.
4. BIOS/HLE services used by this JoJo.
5. GTE/GPU + first real rendering checkpoint.
6. CD-ROM/DMA/timers/interrupts.
7. SPU/audio.
8. input + first real boot checkpoint.
9. gameplay completion for this JoJo only.

This sequencing keeps hardware work evidence-driven rather than building generic PS1 compatibility.