# USA Native Backend Promotion v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote the recognized USA disc revision to `backend=native-ready` only after the local Dreamcast boot program is analyzed, a host-specific native backend cache is generated/reused and reloaded successfully, and `bootstrap_runtime()` can independently revalidate that persisted cache.

**Architecture:** Add one focused `game_backend` component between revision identification and the existing generic SH-4/native backend. Conversion owns progress plus pending/final manifest transitions; `game_backend` owns revision eligibility, boot analysis, cache generation/reuse and cache reload verification; runtime reloads the persisted plan and verifies it against manifest metadata instead of trusting the `native-ready` string.

**Tech Stack:** C++20, CMake 3.20+, existing Dreamcast ISO9660/IP.BIN pipeline, SH-4 CFG/IR/reference executor, existing x64 native backend/cache, MSVC 2022 x64, GitHub Actions Linux + Windows.

**Spec:** `docs/superpowers/specs/2026-09-08-usa-native-backend-promotion-v0-design.md`

## Global Constraints

- The only commercial revision eligible for this v0 game-specific path is `jojo-usa-observed-b8b5dbf79cdb9fcf`.
- `native-ready` means a validated game-specific backend package exists; it does **not** claim complete commercial gameplay, rendering, audio, input, timing, or end-to-end boot validation.
- The repository, tests, CI artifacts, and release package must contain no commercial game image, boot executable, assets, music, or extracted proprietary files.
- Tests use only synthetic Dreamcast/ISO9660 fixtures and synthetic SH-4 programs.
- The user's selected image remains read-only; persistent outputs are derived metadata/cache under the per-user installation directory.
- Unknown/unverified revisions retain the existing successful base-conversion behavior and remain `pending-game-specific-recompiler`.
- A supported revision must be written to disk as **pending** before game-backend preparation begins, so a failed re-prepare cannot preserve stale readiness.
- Final `native-ready` promotion is an atomic manifest replacement only after cache reload verification succeeds.
- CMake 3.20+, C++20, MSVC v143/Visual Studio 2022 x64; Windows GitHub Actions is authoritative for the x64 native backend ABI.

---

### Task 1: Add the game-specific backend preparation boundary

**Files:**
- Create: `src/core/game_backend.h`
- Create: `src/core/game_backend.cpp`
- Modify: `tests/iso_fixture.h`
- Create: `tests/test_game_backend.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Iso9660Image`, `read_dreamcast_boot_program(...)`, `analyze_dreamcast_boot_program(...)`, `ensure_native_backend_cache(...)`, `load_native_backend_cache(...)`, `native_backend_abi_version()`.
- Produces:

```cpp
inline constexpr std::string_view kJojoUsaObservedRevisionId =
    "jojo-usa-observed-b8b5dbf79cdb9fcf";

enum class GameBackendStage {
    boot_analyzed,
    cache_ready,
    cache_verified,
};

using GameBackendProgressCallback = std::function<void(GameBackendStage)>;

struct GameNativeBackendSummary {
    std::string boot_program_hash_hex;
    std::uint32_t abi_version{};
    std::string program_hash;
    std::uint64_t block_count{};
    std::uint64_t native_block_count{};
    std::uint64_t fallback_block_count{};
    std::uint64_t native_code_bytes{};
};

[[nodiscard]] bool supports_game_native_backend(
    std::string_view revision_id) noexcept;

[[nodiscard]] Result<GameNativeBackendSummary> prepare_game_native_backend(
    std::string_view revision_id,
    const Iso9660Image& image,
    const std::filesystem::path& install_dir,
    const GameBackendProgressCallback& on_progress = {});
```

`prepare_game_native_backend(...)` assumes revision identification has already happened, but still rejects every revision except `kJojoUsaObservedRevisionId`. `GameBackendStage` is semantic only; conversion owns percentages and user-visible strings.

- [ ] **Step 1: Extend the synthetic ISO fixture with reusable Dreamcast helpers**

Add `<array>` to `tests/iso_fixture.h`, then add:

```cpp
inline void write_ascii_field(const std::filesystem::path& path,
                              std::streamoff offset,
                              std::size_t width,
                              std::string_view text) {
    std::fstream io(path, std::ios::in | std::ios::out | std::ios::binary);
    std::string field(width, ' ');
    std::copy_n(text.begin(), std::min(width, text.size()), field.begin());
    io.seekp(offset);
    io.write(field.data(), static_cast<std::streamsize>(field.size()));
}

inline void install_dreamcast_ip_metadata(
    const std::filesystem::path& path,
    std::string_view device_info = "GD-ROM1/1",
    std::string_view boot_filename = "1ST_READ.BIN") {
    write_ascii_field(path, 0x000, 16, "SEGA SEGAKATANA ");
    write_ascii_field(path, 0x010, 16, "SEGA ENTERPRISES");
    write_ascii_field(path, 0x020, 16, device_info);
    write_ascii_field(path, 0x030, 8, "JUE");
    write_ascii_field(path, 0x038, 8, "E000F10");
    write_ascii_field(path, 0x040, 10, "T-TEST0001");
    write_ascii_field(path, 0x04A, 6, "V1.001");
    write_ascii_field(path, 0x050, 16, "20000101");
    write_ascii_field(path, 0x060, 16, boot_filename);
    write_ascii_field(path, 0x070, 16, "OPENAI TEST");
    write_ascii_field(path, 0x080, 128, "JOJO RECOMPILED SYNTHETIC");
}

inline void overwrite_boot_program_12(
    const std::filesystem::path& path,
    const std::array<std::uint8_t, 12>& bytes) {
    std::fstream io(path, std::ios::in | std::ios::out | std::ios::binary);
    io.seekp(static_cast<std::streamoff>(21 * sector));
    io.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}
```

Keep `write_image(...)` unchanged for existing tests.

- [ ] **Step 2: Write the RED game-backend tests**

Create `tests/test_game_backend.cpp` with these constants:

```cpp
static constexpr std::array<std::uint8_t, 12> kValidBoot{{
    0x01, 0xE0, // MOV #1,R0
    0x02, 0x70, // ADD #2,R0
    0x09, 0x00, // NOP
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
}};

static constexpr std::array<std::uint8_t, 12> kUnsupportedBoot{{
    0xFF, 0xFF, // reachable unsupported opcode
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
    0x09, 0x00,
}};
```

Implement four concrete assertions:

```cpp
const auto supported = jojo::prepare_game_native_backend(
    jojo::kJojoUsaObservedRevisionId, image.value, root,
    [&](jojo::GameBackendStage s) { stages.push_back(s); });
CHECK(supported);
CHECK(supported.value.abi_version == jojo::native_backend_abi_version());
CHECK(supported.value.block_count > 0u);
CHECK(supported.value.native_block_count + supported.value.fallback_block_count ==
      supported.value.block_count);
CHECK(stages == std::vector<jojo::GameBackendStage>{
    jojo::GameBackendStage::boot_analyzed,
    jojo::GameBackendStage::cache_ready,
    jojo::GameBackendStage::cache_verified});
CHECK(std::filesystem::is_regular_file(root / "cache/native/compiled_plan.bin"));

const auto wrong_revision = jojo::prepare_game_native_backend(
    "other-revision", image.value, root);
CHECK(!wrong_revision);
CHECK(wrong_revision.error == jojo::ErrorCode::backend_unavailable);

const auto milcd = jojo::prepare_game_native_backend(
    jojo::kJojoUsaObservedRevisionId, milcd_image.value, milcd_root);
CHECK(!milcd);
CHECK(milcd.error == jojo::ErrorCode::unsupported_format);

const auto unsupported = jojo::prepare_game_native_backend(
    jojo::kJojoUsaObservedRevisionId, unsupported_image.value, unsupported_root);
CHECK(!unsupported);
CHECK(unsupported.error == jojo::ErrorCode::backend_unavailable);
```

Construct `image` from normal GD-ROM metadata plus `kValidBoot`; construct `milcd_image` with `install_dreamcast_ip_metadata(path, "CD-ROM1/1")`; construct `unsupported_image` with normal GD-ROM metadata plus `kUnsupportedBoot`. Delete every temp root after each case.

- [ ] **Step 3: Register the RED target**

Add `src/core/game_backend.cpp` to `jojo_core`, then add:

```cmake
add_executable(jojo_game_backend_tests tests/test_game_backend.cpp)
target_link_libraries(jojo_game_backend_tests PRIVATE jojo_core)
add_test(NAME jojo_game_backend_tests COMMAND jojo_game_backend_tests)
```

- [ ] **Step 4: Run RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target jojo_game_backend_tests --parallel 2
ctest --test-dir build -R jojo_game_backend_tests --output-on-failure
```

Expected: compile failure because `core/game_backend.h`/API do not exist.

- [ ] **Step 5: Implement minimal `game_backend`**

`src/core/game_backend.cpp` must perform this exact sequence:

```cpp
if (!supports_game_native_backend(revision_id)) {
    return Result<GameNativeBackendSummary>::failure(
        ErrorCode::backend_unavailable,
        "no game-specific native backend is enabled for this revision");
}

auto boot = read_dreamcast_boot_program(image);
if (!boot) return Result<GameNativeBackendSummary>::failure(boot.error, boot.detail);

auto analysis = analyze_dreamcast_boot_program(boot.value);
if (!analysis) return Result<GameNativeBackendSummary>::failure(analysis.error, analysis.detail);
if (on_progress) on_progress(GameBackendStage::boot_analyzed);

auto cache = ensure_native_backend_cache(boot.value, install_dir);
if (!cache) return Result<GameNativeBackendSummary>::failure(cache.error, cache.detail);
if (on_progress) on_progress(GameBackendStage::cache_ready);

auto loaded = load_native_backend_cache(cache.value.plan_path);
if (!loaded) return Result<GameNativeBackendSummary>::failure(loaded.error, loaded.detail);
```

Then require loaded ABI to equal both `native_backend_abi_version()` and the cache ABI, loaded `program_hash` to equal the cache program hash, and loaded IR block count to equal cache block count. Sum every `compiled.native_code.size()` and require equality with `cache.value.native_code_bytes`. On mismatch return `ErrorCode::invalid_installation`. Populate the summary from `boot.value.hash_hex`, loaded counts, current ABI, cache program hash and verified native-code byte total. Emit `cache_verified` only after all comparisons succeed. Do not write `game_manifest.ini`.

- [ ] **Step 6: Run GREEN**

```bash
cmake --build build --target jojo_game_backend_tests jojo_native_backend_tests --parallel 2
ctest --test-dir build -R "jojo_(game_backend|native_backend)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit Task 1**

```bash
git add CMakeLists.txt src/core/game_backend.h src/core/game_backend.cpp tests/iso_fixture.h tests/test_game_backend.cpp
git commit -m "feat: add USA game backend preparation boundary"
```

---

### Task 2: Extend the manifest with verifiable backend metadata

**Files:**
- Modify: `src/core/conversion.h`
- Modify: `src/core/conversion.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**

Add to `ConversionManifest`:

```cpp
std::string boot_program_hash_hex;
std::optional<std::uint32_t> backend_abi_version;
std::string backend_program_hash;
std::optional<std::uint64_t> backend_block_count;
std::optional<std::uint64_t> backend_native_block_count;
std::optional<std::uint64_t> backend_fallback_block_count;
std::optional<std::uint64_t> backend_native_code_bytes;
```

Add:

```cpp
[[nodiscard]] bool has_complete_native_backend_metadata(
    const ConversionManifest& manifest) noexcept;
```

Persistent keys are exactly `boot_program_hash_fnv1a64`, `backend_abi_version`, `backend_program_hash`, `backend_block_count`, `backend_native_block_count`, `backend_fallback_block_count`, `backend_native_code_bytes`.

- [ ] **Step 1: Write RED pending-compatibility test**

Use exactly:

```cpp
jojo::ConversionManifest m{};
m.converter_version = jojo::core_version();
m.source_name = "owned.iso";
m.source_format = "iso";
m.source_size = 1234u;
m.hash_hex = "0123456789abcdef";
m.revision_id = "synthetic-test-revision";
CHECK(jojo::save_conversion_manifest_atomic(path, m));
const auto loaded = jojo::load_conversion_manifest(path);
CHECK(loaded);
CHECK(!jojo::has_complete_native_backend_metadata(loaded.value));
CHECK(loaded.value.backend == "pending-game-specific-recompiler");
```

- [ ] **Step 2: Write RED complete-ready round-trip test**

Use the same ordinary source fields, then:

```cpp
m.revision_id = std::string(jojo::kJojoUsaObservedRevisionId);
m.backend = "native-ready";
m.boot_program_hash_hex = "1111111111111111";
m.backend_abi_version = 0x00020001u;
m.backend_program_hash = "2222222222222222";
m.backend_block_count = 4u;
m.backend_native_block_count = 3u;
m.backend_fallback_block_count = 1u;
m.backend_native_code_bytes = 64u;
```

Assert save/load success, exact round-trip for all seven values, and complete metadata true.

- [ ] **Step 3: Write RED malformed-ready tests**

Hand-write one ready `.ini` containing all ordinary required fields plus all numeric backend fields but omitting `backend_program_hash`; assert `ErrorCode::invalid_installation`.

Hand-write a second complete ready `.ini` but set `backend_abi_version=not-a-number`; assert `ErrorCode::invalid_installation`.

- [ ] **Step 4: Run RED**

```bash
cmake --build build --target jojo_tests --parallel 2
ctest --test-dir build -R jojo_tests --output-on-failure
```

Expected: compile failure because metadata fields/helper do not exist.

- [ ] **Step 5: Implement manifest parser/writer**

In `conversion.h`, include `<optional>` and add the fields above.

Implement completeness without overflow:

```cpp
if (m.boot_program_hash_hex.empty() ||
    !m.backend_abi_version.has_value() ||
    m.backend_program_hash.empty() ||
    !m.backend_block_count.has_value() ||
    !m.backend_native_block_count.has_value() ||
    !m.backend_fallback_block_count.has_value() ||
    !m.backend_native_code_bytes.has_value()) {
    return false;
}
return *m.backend_native_block_count <= *m.backend_block_count &&
       *m.backend_fallback_block_count ==
           *m.backend_block_count - *m.backend_native_block_count;
```

`save_conversion_manifest_atomic(...)` rejects incomplete `native-ready`; optional backend keys are omitted for pending manifests. `load_conversion_manifest(...)` parses all seven keys, reuses `parse_u64(...)`, rejects ABI values above `UINT32_MAX`, and rejects incomplete `native-ready` after ordinary required-field validation. Existing version-1 pending manifests remain loadable.

- [ ] **Step 6: Run GREEN**

```bash
cmake --build build --target jojo_tests --parallel 2
ctest --test-dir build -R jojo_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit Task 2**

```bash
git add src/core/conversion.h src/core/conversion.cpp tests/test_main.cpp
git commit -m "feat: persist native backend manifest metadata"
```

---

### Task 3: Harden runtime bootstrap against forged or stale readiness

**Files:**
- Create: `tests/test_runtime_native_backend.cpp`
- Modify: `src/core/runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes Task 1 summary/cache and Task 2 manifest metadata.
- No new public runtime API.

- [ ] **Step 1: Add a valid synthetic ready-install helper**

The helper creates a temp root, synthetic ISO, GD-ROM IP metadata and `kValidBoot`; opens it; calls `prepare_game_native_backend(kJojoUsaObservedRevisionId, ...)`; creates `data/`; fills a manifest with ordinary source fields plus every returned summary value; sets the USA revision and `native-ready`; saves atomically; returns root and summary.

- [ ] **Step 2: Write RED strict-bootstrap cases**

Success:

```cpp
CHECK(jojo::bootstrap_runtime(install));
```

Wrong revision:

```cpp
auto m = jojo::load_conversion_manifest(install / "game_manifest.ini").value;
m.revision_id = "other-revision";
CHECK(jojo::save_conversion_manifest_atomic(install / "game_manifest.ini", m));
const auto wrong = jojo::bootstrap_runtime(install);
CHECK(!wrong);
CHECK(wrong.error == jojo::ErrorCode::backend_unavailable);
```

Missing plan:

```cpp
std::filesystem::remove(install / "cache/native/compiled_plan.bin");
const auto missing = jojo::bootstrap_runtime(install);
CHECK(!missing);
CHECK(missing.error == jojo::ErrorCode::file_not_found);
```

Program hash mismatch:

```cpp
m.backend_program_hash = "ffffffffffffffff";
CHECK(jojo::save_conversion_manifest_atomic(install / "game_manifest.ini", m));
CHECK(!jojo::bootstrap_runtime(install));
```

ABI mismatch:

```cpp
m.backend_abi_version = 0u;
CHECK(jojo::save_conversion_manifest_atomic(install / "game_manifest.ini", m));
CHECK(!jojo::bootstrap_runtime(install));
```

Block-count mismatch while metadata stays internally complete:

```cpp
m.backend_block_count = *m.backend_block_count + 1u;
m.backend_fallback_block_count = *m.backend_fallback_block_count + 1u;
CHECK(jojo::save_conversion_manifest_atomic(install / "game_manifest.ini", m));
CHECK(!jojo::bootstrap_runtime(install));
```

Truncated plan:

```cpp
{
    std::ofstream out(install / "cache/native/compiled_plan.bin",
                      std::ios::binary | std::ios::trunc);
    out.write("JOJO", 4);
}
const auto truncated = jojo::bootstrap_runtime(install);
CHECK(!truncated);
CHECK(truncated.error == jojo::ErrorCode::invalid_installation);
```

- [ ] **Step 3: Register and run RED**

```cmake
add_executable(jojo_runtime_native_backend_tests tests/test_runtime_native_backend.cpp)
target_link_libraries(jojo_runtime_native_backend_tests PRIVATE jojo_core)
add_test(NAME jojo_runtime_native_backend_tests COMMAND jojo_runtime_native_backend_tests)
```

```bash
cmake --build build --target jojo_runtime_native_backend_tests --parallel 2
ctest --test-dir build -R jojo_runtime_native_backend_tests --output-on-failure
```

Expected: forged/missing-cache cases fail because current bootstrap trusts only `native-ready`.

- [ ] **Step 4: Harden `bootstrap_runtime()`**

After `validate_installation(...)`, require `backend=native-ready`, exact USA revision, and complete metadata. Load `cache/native/compiled_plan.bin`; propagate load errors. Require loaded ABI equals both current ABI and manifest ABI; program hash equals manifest hash; loaded block/native/fallback counts equal manifest counts; summed native-code bytes equal manifest bytes. Any mismatch returns `ErrorCode::invalid_installation`. Do not rebuild from bootstrap.

- [ ] **Step 5: Run GREEN**

```bash
cmake --build build --target jojo_runtime_native_backend_tests jojo_tests jojo_native_backend_tests --parallel 2
ctest --test-dir build -R "jojo_(runtime_native_backend|tests|native_backend_tests)" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit Task 3**

```bash
git add CMakeLists.txt src/core/runtime.cpp tests/test_runtime_native_backend.cpp
git commit -m "feat: verify native backend cache during bootstrap"
```

---

### Task 4: Promote the supported revision during conversion with pending-first atomicity

**Files:**
- Modify: `src/core/conversion.h`
- Modify: `src/core/conversion.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**

Add:

```cpp
preparing_game_backend,
building_native_backend,
verifying_native_backend,
promoting_native_backend,
```

to `ConversionStage`. Explicit conversion remains strict about revision identity; default UI conversion still permits only unverified base preparation.

- [ ] **Step 1: Add exact synthetic-profile helpers**

Add `<array>` and `<span>` to `tests/test_main.cpp` and implement:

```cpp
static std::uint64_t test_fnv1a64(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

static jojo::GameRevisionProfile profile_for_boot(
    std::string revision_id,
    const std::array<std::uint8_t, 12>& boot) {
    return {
        std::move(revision_id),
        {
            {"/1ST_READ.BIN", 12u, test_fnv1a64(boot)},
            {"/DATA/ASSET.DAT", 5u, 0x65f9a54a4f1d65c8ull},
        }
    };
}
```

Use the same `kValidBoot` and `kUnsupportedBoot` bytes as Task 1. This test seam changes only the explicit revision profile; production default conversion still reaches the commercial USA ID through the observed whole-disc fingerprint.

- [ ] **Step 2: Write RED successful-promotion test**

Build synthetic GD-ROM image with `kValidBoot` and use:

```cpp
jojo::ConversionOptions options{};
options.revision_profiles.push_back(
    profile_for_boot(std::string(jojo::kJojoUsaObservedRevisionId), kValidBoot));
std::vector<jojo::ConversionProgress> events;
const auto converted = jojo::convert_image(
    source, install, options,
    [&](const jojo::ConversionProgress& e) { events.push_back(e); });
CHECK(converted);
CHECK(converted.value.revision_id == jojo::kJojoUsaObservedRevisionId);
CHECK(converted.value.backend == "native-ready");
CHECK(jojo::has_complete_native_backend_metadata(converted.value));
CHECK(jojo::bootstrap_runtime(install));
```

Walk `events`; require nondecreasing percentages and presence of all four new backend stages.

- [ ] **Step 3: Write RED failed-reprepare atomicity test**

First run the exact successful conversion above. Then overwrite the same source with `kUnsupportedBoot`, use a new explicit revision profile matching those bytes but retaining the USA revision ID, and run:

```cpp
const auto failed = jojo::convert_image(source, install, bad_options);
CHECK(!failed);
CHECK(failed.error == jojo::ErrorCode::backend_unavailable);
const auto pending = jojo::load_conversion_manifest(install / "game_manifest.ini");
CHECK(pending);
CHECK(pending.value.backend == "pending-game-specific-recompiler");
const auto boot = jojo::bootstrap_runtime(install);
CHECK(!boot);
CHECK(boot.error == jojo::ErrorCode::backend_unavailable);
```

Retain the current ordinary `synthetic-test-revision` conversion and assert it still ends pending.

- [ ] **Step 4: Run RED**

```bash
cmake --build build --target jojo_tests --parallel 2
ctest --test-dir build -R jojo_tests --output-on-failure
```

Expected: promotion/stale-readiness assertions fail.

- [ ] **Step 5: Implement pending-first orchestration**

After revision identification, create `data/`, `cache/`, `logs/`; construct the ordinary pending manifest; report preparation around 55%; atomically save pending **before** backend work.

If `supports_game_native_backend(manifest.revision_id)` is false, report base completion and return pending unchanged.

For the supported revision map semantic callbacks exactly:

```cpp
GameBackendProgressCallback backend_progress = [&](GameBackendStage stage) {
    switch (stage) {
        case GameBackendStage::boot_analyzed:
            report(ConversionStage::preparing_game_backend, 65,
                   "analyze_game_boot",
                   "Programa de boot Dreamcast analisado para a revisão reconhecida.");
            break;
        case GameBackendStage::cache_ready:
            report(ConversionStage::building_native_backend, 80,
                   "build_native_backend",
                   "Backend nativo gerado ou reutilizado para o programa identificado.");
            break;
        case GameBackendStage::cache_verified:
            report(ConversionStage::verifying_native_backend, 92,
                   "verify_native_backend",
                   "Cache do backend nativo recarregado e verificado.");
            break;
    }
};
```

On preparation failure return its exact error and leave pending on disk. On success copy all summary fields into the manifest, then set `backend="native-ready"`; report `promoting_native_backend` at 97%; atomically save final ready manifest; report 100%. Never persist boot-program bytes.

- [ ] **Step 6: Run GREEN**

```bash
cmake --build build --target jojo_tests jojo_game_backend_tests jojo_runtime_native_backend_tests --parallel 2
ctest --test-dir build -R "jojo_(tests|game_backend|runtime_native_backend)_tests" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit Task 4**

```bash
git add src/core/conversion.h src/core/conversion.cpp tests/test_main.cpp
git commit -m "feat: promote verified USA backend during conversion"
```

---

### Task 5: Align Windows UI semantics and make the promotion contract explicit in CI

**Files:**
- Modify: `src/app_win32/main.cpp`
- Modify: `.github/workflows/build.yml`

- [ ] **Step 1: Change ready-state copy without claiming gameplay completion**

In both `refresh_install()` and successful `WM_FINISHED` bootstrap path use:

```cpp
status = L"Backend nativo da revisão USA preparado. Validação fim a fim é o próximo marco.";
```

In `refresh_install()` log:

```cpp
add_log(L"Backend nativo verificado e cache carregável detectado.");
```

Keep the existing pending wording for `backend_unavailable`.

- [ ] **Step 2: Add explicit Linux CI contract**

After CTest:

```yaml
- name: USA native backend promotion contract
  run: ./build/jojo_game_backend_tests && ./build/jojo_runtime_native_backend_tests
```

- [ ] **Step 3: Add explicit Windows CI contract**

After `Test Release`:

```yaml
- name: USA native backend promotion contract
  shell: cmd
  run: |
    build\Release\jojo_game_backend_tests.exe
    build\Release\jojo_runtime_native_backend_tests.exe
```

- [ ] **Step 4: Run complete portable verification**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/jojo_game_backend_tests
./build/jojo_runtime_native_backend_tests
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
```

Expected: PASS with no proprietary-content readiness violation.

- [ ] **Step 5: Commit Task 5**

```bash
git add src/app_win32/main.cpp .github/workflows/build.yml
git commit -m "ci: verify USA native backend promotion contract"
```

---

### Task 6: Final review, PR, merge, and Windows artifact handoff

**Files:**
- Review all files changed in Tasks 1-5.
- PR target: `main`.

- [ ] **Step 1: Clean full verification**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
```

Expected: zero failures.

- [ ] **Step 2: Review complete diff**

```bash
git diff --check main...HEAD
git diff --stat main...HEAD
git diff main...HEAD -- \
  CMakeLists.txt \
  .github/workflows/build.yml \
  src/core/game_backend.h \
  src/core/game_backend.cpp \
  src/core/conversion.h \
  src/core/conversion.cpp \
  src/core/runtime.cpp \
  src/app_win32/main.cpp \
  tests/iso_fixture.h \
  tests/test_game_backend.cpp \
  tests/test_runtime_native_backend.cpp \
  tests/test_main.cpp
```

Reject the diff unless all are true: no game bytes/assets added; exact USA eligibility gate preserved; pending manifest written before backend work; `native-ready` set only after cache reload verification; runtime revalidates persisted cache; generic unverified/pending conversion remains; UI does not claim complete gameplay readiness.

- [ ] **Step 3: Open PR and require both CI jobs**

PR summary:

```text
- keeps the observed USA revision as the only commercial v0 backend candidate
- analyzes the user's local Dreamcast boot program and builds/reloads the existing native backend cache
- promotes to native-ready only after derived cache metadata is verified
- hardens bootstrap against forged/stale ready manifests
- contains no commercial game content; all automated fixtures are synthetic
```

Do not merge until Linux and Windows both pass, including `USA native backend promotion contract`.

- [ ] **Step 4: Merge with expected head SHA and verify post-merge `main`**

Merge only with the exact CI-validated PR head SHA as precondition. Require the new `main` workflow to pass the same Linux/Windows gates.

- [ ] **Step 5: Download and verify post-merge Windows artifact**

Download `JOJO-Recompiled-Windows-x64`; confirm the ZIP contains only `JOJO-Recompiled.exe`; compute ZIP SHA-256 and require exact equality with GitHub artifact digest.

Next user-local commercial test:

1. run that exact post-merge executable;
2. select the same recognized USA BIN;
3. if conversion reaches `native-ready`, inspect only the derived backend metadata and move to the next runtime/game-boot milestone;
4. if conversion stops during boot analysis/backend generation, use `%LOCALAPPDATA%\JOJO Recompiled\game\logs\conversion.log` to identify the first real unsupported SH-4/backend blocker.
