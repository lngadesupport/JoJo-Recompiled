# M6 Mod Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the complete deterministic M6 mod runtime: manifests, semantic compatibility, dependencies/load order, data overlays, SHA-256 content identity, online policy, and opt-in native plugins.

**Architecture:** Keep parsing/version/hash utilities independent, then build a portable `mod_runtime` over filesystem paths and deterministic catalog/resolution. Keep dynamic-library code isolated in `native_mod_loader` so the core manifest/runtime tests stay portable and the platform-specific ABI can be tested with a tiny shared library.

**Tech Stack:** C++20, `std::filesystem`, existing `jojo::Result<T>`, Win32 `LoadLibraryW`/`GetProcAddress`, POSIX `dlopen`/`dlsym`, CTest.

**Spec:** `docs/superpowers/specs/2026-08-30-mod-runtime-design.md`

**Status:** Tasks 1–7 and the final branch gate are complete. PR integration and post-merge verification remain authoritative.

## Global Constraints

- No proprietary game data, commercial fingerprints, save data, or extracted assets.
- Host mod API version is exactly `1.0.0` for M6.
- Native plugins are disabled unless explicitly opted in.
- All ordering and hashing must be deterministic across Linux and Windows.
- Symlinks and path traversal may not escape a mod root.
- M6 completion does not claim real-game mod integration or `native-ready`.

---

### Task 1: SemVer and dependency requirements

**Files:**
- Create: `src/core/semver.h`
- Create: `src/core/semver.cpp`
- Test: `tests/test_semver.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `SemanticVersion`, `VersionRequirement`, `parse_semver(std::string_view)`, `parse_version_requirement(std::string_view)`, `matches(VersionRequirement, SemanticVersion)`.

- [x] **Step 1: Write the failing test**

```cpp
CHECK(parse_semver("1.2.3").value == SemanticVersion{1,2,3});
CHECK(!parse_semver("1.2"));
CHECK(matches(parse_version_requirement(">=1.2.3").value, {1,9,0}));
CHECK(!matches(parse_version_requirement("^1.2.3").value, {2,0,0}));
CHECK(matches(parse_version_requirement("^1.2.3").value, {1,8,0}));
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target jojo_semver_tests && ctest --test-dir build -R jojo_semver_tests --output-on-failure`
Expected: build failure because `core/semver.h` does not exist.

- [x] **Step 3: Write minimal implementation**

```cpp
struct SemanticVersion { std::uint32_t major{}, minor{}, patch{}; friend bool operator==(const SemanticVersion&, const SemanticVersion&) = default; };
enum class VersionRequirementKind { any, exact, at_least, compatible_major };
struct VersionRequirement { VersionRequirementKind kind{VersionRequirementKind::any}; SemanticVersion version{}; };
```

Parsers must reject signs, whitespace inside numeric components, missing components, overflow, prerelease/build suffixes, and unknown operators.

- [x] **Step 4: Run test to verify it passes**

Run the Task 1 CTest on Linux and Windows.
Expected: PASS.

- [x] **Step 5: Commit**

Commit: `feat: add semantic version requirements`

### Task 2: Portable SHA-256

**Files:**
- Create: `src/core/sha256.h`
- Create: `src/core/sha256.cpp`
- Test: `tests/test_sha256.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `Sha256Digest = std::array<std::uint8_t,32>`, `sha256(std::span<const std::uint8_t>)`, `sha256_file(path)`, `sha256_hex(digest)`.

- [x] **Step 1: Write the failing test**

```cpp
CHECK(sha256_hex(sha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
const std::string abc = "abc";
CHECK(sha256_hex(sha256(std::as_bytes(std::span{abc}))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
```

- [x] **Step 2: Run test to verify it fails**

Expected: missing `core/sha256.h`.

- [x] **Step 3: Write minimal implementation**

Implement FIPS 180-4 SHA-256 with explicit big-endian word loading/storing, 64-byte chunks, and file streaming. No platform APIs or external dependencies.

- [x] **Step 4: Run test to verify it passes**

Expected: known vectors and file-vs-memory digest agree.

- [x] **Step 5: Commit**

Commit: `feat: add portable SHA-256`

### Task 3: Manifest parser and catalog discovery

**Files:**
- Create: `src/core/mod_runtime.h`
- Create: `src/core/mod_runtime.cpp`
- Test: `tests/test_mod_runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `ModKind`, `ModDependency`, `ModManifest`, `DiscoveredMod`, `ModCatalog`, `parse_mod_manifest(text, root)`, `discover_mods(mods_root)`.

```cpp
enum class ModKind { data, native };
struct ModDependency { std::string id; VersionRequirement requirement; };
struct ModManifest {
  std::string id, name;
  SemanticVersion version, api_version;
  ModKind kind{ModKind::data};
  bool gameplay{};
  std::filesystem::path entry;
  std::vector<ModDependency> dependencies;
  std::vector<std::string> conflicts;
};
struct DiscoveredMod { ModManifest manifest; std::filesystem::path root; };
```

- [x] **Step 1: Write the failing test**

Create temporary mod roots with valid and invalid `mod.ini` files. Require valid parse, native-entry validation, unknown-key rejection, ID validation, duplicate-ID rejection, deterministic ID sorting, and traversal rejection.

- [x] **Step 2: Run test to verify it fails**

Expected: missing mod runtime API.

- [x] **Step 3: Write minimal implementation**

Implement strict line parser and immediate-subdirectory discovery. Host API constant is `SemanticVersion{1,0,0}` and manifests with different major or greater requested version are rejected as incompatible.

- [x] **Step 4: Run test to verify it passes**

Expected: manifest/catalog cases PASS.

- [x] **Step 5: Commit**

Commit: `feat: add mod manifest discovery`

### Task 4: Dependency resolution and deterministic load order

**Files:**
- Modify: `src/core/mod_runtime.h`
- Modify: `src/core/mod_runtime.cpp`
- Modify: `tests/test_mod_runtime.cpp`

**Interfaces:**
- Produces: `ResolvedModSet`, `resolve_mod_set(const ModCatalog&, std::span<const std::string> requested_ids)`.

```cpp
struct ResolvedModSet {
  std::vector<const DiscoveredMod*> load_order;
  std::vector<std::string> diagnostics;
};
```

- [x] **Step 1: Write the failing test**

Require recursive dependency inclusion, version mismatch rejection, missing dependency rejection, cycle rejection, explicit conflict rejection, and lexicographic tie-breaking for independent nodes.

- [x] **Step 2: Run test to verify it fails**

Expected: resolver absent.

- [x] **Step 3: Write minimal implementation**

Use DFS for closure/cycle detection plus Kahn topological sort with a lexicographically ordered ready set. Return deterministic error details naming the involved IDs.

- [x] **Step 4: Run test to verify it passes**

Expected: all resolver cases PASS.

- [x] **Step 5: Commit**

Commit: `feat: resolve deterministic mod graphs`

### Task 5: Data overlay and deterministic content identity

**Files:**
- Modify: `src/core/mod_runtime.h`
- Modify: `src/core/mod_runtime.cpp`
- Modify: `tests/test_mod_runtime.cpp`

**Interfaces:**
- Produces: `ModOverlay`, `build_mod_overlay(ResolvedModSet)`, `compute_mod_content_hash`, `compute_mod_set_hashes`.

```cpp
struct OverlayEntry { std::string mod_id; std::filesystem::path host_path; };
struct OverlayCollision { std::string logical_path, previous_mod_id, replacing_mod_id; };
struct ModOverlay { std::map<std::string,OverlayEntry> files; std::vector<OverlayCollision> collisions; };
struct ModSetHashes { std::string mod_set_hash; std::string gameplay_hash; };
```

- [x] **Step 1: Write the failing test**

Create two data mods that both provide `data/ui/menu.txt`; require later load order to win, collision diagnostics to name both IDs, normalized `/` logical paths, symlink rejection when supported, stable content hashes despite file creation order, changed byte => changed hash, cosmetic-only changes => unchanged gameplay hash but changed full mod-set hash.

- [x] **Step 2: Run test to verify it fails**

Expected: overlay/hash API absent.

- [x] **Step 3: Write minimal implementation**

Recursively collect regular files, reject symlinks, sort normalized relative paths, hash length-delimited path bytes plus file length/content, and aggregate ordered mod tuples through SHA-256.

- [x] **Step 4: Run test to verify it passes**

Expected: overlay and deterministic hashing PASS on Linux and Windows.

- [x] **Step 5: Commit**

Commit: `feat: add mod overlays and content identity`

### Task 6: Online mod policy

**Files:**
- Modify: `src/core/mod_runtime.h`
- Modify: `src/core/mod_runtime.cpp`
- Modify: `tests/test_mod_runtime.cpp`

**Interfaces:**
- Produces: `ModSessionMode`, `ModSessionPolicy`, `validate_mod_session`.

```cpp
enum class ModSessionMode { offline, ranked, custom };
struct ModSessionPolicy { ModSessionMode mode{ModSessionMode::offline}; std::string required_mod_set_hash; };
```

- [x] **Step 1: Write the failing test**

Require ranked to accept cosmetic-only sets, ranked to reject any gameplay mod, custom without requirement to accept, custom exact hash to accept, and mismatch to reject with distinct diagnostics.

- [x] **Step 2: Run test to verify it fails**

Expected: session policy API absent.

- [x] **Step 3: Write minimal implementation**

Implement policy as a pure function over resolved manifests and `ModSetHashes`.

- [x] **Step 4: Run test to verify it passes**

Expected: all policy cases PASS.

- [x] **Step 5: Commit**

Commit: `feat: enforce online mod policy`

### Task 7: Versioned native-plugin C ABI and loader

**Files:**
- Create: `src/mod_api/jojo_mod_api.h`
- Create: `src/core/native_mod_loader.h`
- Create: `src/core/native_mod_loader.cpp`
- Create: `tests/fixtures/test_native_mod.cpp`
- Create: `tests/test_native_mod_loader.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `NativeModLoadOptions`, `NativeModSession`, `load_native_mods`.

Public C ABI:

```c
#define JOJO_NATIVE_MOD_ABI_V1 1u
typedef struct JojoNativeModV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  const char* mod_id;
  int (*on_load)(void);
  void (*on_unload)(void);
} JojoNativeModV1;
typedef const JojoNativeModV1* (*JojoGetNativeModV1Fn)(void);
```

- [x] **Step 1: Write the failing test**

CTest builds a fixture shared library. Require disabled-by-default rejection, successful explicit opt-in, exported descriptor/ID/ABI validation, `on_load` result handling, and reverse-order unload. Use environment/file side effect inside the test temp directory to prove callbacks ran without proprietary data.

- [x] **Step 2: Run test to verify it fails**

Expected: native loader header/target absent.

- [x] **Step 3: Write minimal implementation**

Use `LoadLibraryW/GetProcAddress/FreeLibrary` under `_WIN32` and `dlopen/dlsym/dlclose` elsewhere. Keep raw handles private and make the session move-only/RAII. Reject native manifests unless opt-in is true.

- [x] **Step 4: Run test to verify it passes**

Expected: real plugin load/callback/unload PASS on Linux and Windows/MSVC.

- [x] **Step 5: Commit**

Commit: `feat: add opt-in native mod ABI`

### Task 8: M6 closure documentation and full gates

**Files:**
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`
- Modify: `docs/superpowers/plans/2026-08-30-mod-runtime.md`

- [x] **Step 1: Run the entire branch CI**

Expected: Linux build/tests PASS; Windows/MSVC Release build/tests and executable artifact PASS.

- [x] **Step 2: Review branch diff against the M4 merge base**

Expected: only M6 source/tests/build/docs, no temporary workflows, binaries, generated mod data, or proprietary assets.

- [x] **Step 3: Mark M6 Complete (100%) in roadmap**

Document exact contract and limitation that real-game asset routing/menu integration is not claimed.

- [x] **Step 4: Re-run final-head branch CI**

Expected: both hosts green on the exact documented head.

- [ ] **Step 5: Open PR, wait for PR CI, merge with expected head SHA, and verify post-merge CI**

Expected: PR Linux+Windows green, merge succeeds only on validated head, `main` post-merge Linux+Windows green.

## Verification evidence

- The online-policy RED is preserved at `a1b6a0e10ab14c1c2c16bede7187d388e92995f4`; the GREEN implementation is `caa55fa1d427de66ad19e36fb252a2161d512727`.
- The versioned native ABI/loader GREEN is `3eceb132e85adb8ba3ca5260a866d4866f9d7cb9`; workflow run `33293304909` passed Linux and Windows/MSVC with real shared-library lifecycle tests.
- Security hardening rejects incompatible API catalogs, symlinked mod roots/manifests, parent-traversing entries and dangling manifest symlinks.
- Final code head `e9d112e85c8001185c95879941891eed1f38faf7` passed workflow run `33293821593` on Linux and Windows x64/MSVC, including the single Windows executable artifact.
- The local Release suite passes 48/48 tests. Targeted M6 runs also pass AddressSanitizer and UndefinedBehaviorSanitizer; leak detection alone is unavailable under this container's ptrace environment.
- The final diff contains no temporary workflows, game/disc images, compiled binaries, archives, extracted assets or commercial fingerprints.
