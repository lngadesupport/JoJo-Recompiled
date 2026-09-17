# R2.1 Production Readiness Gates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make repository status claims, conversion readiness, CI and release criteria agree through a machine-checkable production-readiness manifest and failing consistency gate.

**Architecture:** Add one canonical, line-oriented readiness manifest under `docs/architecture` with only the status vocabulary defined by the production-completion spec. Validate it with a dependency-free CMake script registered as a CTest, then make README/roadmap/next-milestone documentation derive their wording from the same R2 workstream boundaries instead of stale pre-M8 claims. Keep this subproject limited to truth/release gates; do not add commercial fingerprints, device integration or M9 services.

**Tech Stack:** C++20 project, CMake 3.x, CTest, GitHub Actions, Markdown, dependency-free CMake scripting.

**Spec:** `docs/superpowers/specs/2026-09-01-production-completion-design.md`

## Global Constraints

- Status vocabulary is exactly `not-started`, `implemented-unverified`, `verified`, `blocked-external-evidence`.
- `blocked-external-evidence` never counts as verified.
- Generic unit/fixture tests cannot establish commercial-game compatibility.
- A build artifact cannot establish playability.
- Shipping distribution remains one `JOJO-Recompiled.exe`.
- Copyrighted game data, fingerprints guessed from game data, extracted assets and user data must not enter the repository.
- R2.1 must not claim R2.2, R2.3, R2.4, R2.5 or R2.6 completion.

---

### Task 1: Canonical production-readiness manifest and RED contract test

**Files:**
- Create: `docs/architecture/PRODUCTION-READINESS.tsv`
- Create: `cmake/CheckProductionReadiness.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: repository source root passed as `-DJOJO_SOURCE_DIR=<path>`.
- Produces: CTest named `jojo_production_readiness_contract` that returns non-zero for invalid status vocabulary, missing mandatory R2 workstreams, duplicate workstream IDs, or a falsely verified externally blocked workstream.

- [ ] **Step 1: Write the failing readiness gate before creating the manifest**

Add `cmake/CheckProductionReadiness.cmake` with the following contract:

```cmake
if(NOT DEFINED JOJO_SOURCE_DIR)
    message(FATAL_ERROR "JOJO_SOURCE_DIR is required")
endif()

set(readiness_file "${JOJO_SOURCE_DIR}/docs/architecture/PRODUCTION-READINESS.tsv")
if(NOT EXISTS "${readiness_file}")
    message(FATAL_ERROR "production readiness manifest is missing")
endif()

file(STRINGS "${readiness_file}" readiness_lines)
set(required_ids R2.1 R2.2 R2.3 R2.4 R2.5 R2.6)
set(seen_ids)
set(allowed_statuses not-started implemented-unverified verified blocked-external-evidence)

foreach(line IN LISTS readiness_lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()

    string(REPLACE "\t" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 4)
        message(FATAL_ERROR "invalid readiness row: ${line}")
    endif()

    list(GET fields 0 workstream)
    list(GET fields 1 status)
    list(GET fields 2 evidence)
    list(GET fields 3 blocker)

    if(NOT workstream IN_LIST required_ids)
        message(FATAL_ERROR "unknown workstream: ${workstream}")
    endif()
    if(workstream IN_LIST seen_ids)
        message(FATAL_ERROR "duplicate workstream: ${workstream}")
    endif()
    list(APPEND seen_ids "${workstream}")

    if(NOT status IN_LIST allowed_statuses)
        message(FATAL_ERROR "invalid status ${status} for ${workstream}")
    endif()
    if(status STREQUAL "verified" AND evidence STREQUAL "none")
        message(FATAL_ERROR "verified workstream ${workstream} has no evidence")
    endif()
    if(status STREQUAL "verified" AND NOT blocker STREQUAL "none")
        message(FATAL_ERROR "verified workstream ${workstream} still has blocker ${blocker}")
    endif()
endforeach()

foreach(required_id IN LISTS required_ids)
    if(NOT required_id IN_LIST seen_ids)
        message(FATAL_ERROR "missing mandatory workstream: ${required_id}")
    endif()
endforeach()
```

Register it in `CMakeLists.txt`:

```cmake
add_test(
    NAME jojo_production_readiness_contract
    COMMAND ${CMAKE_COMMAND}
        -DJOJO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckProductionReadiness.cmake
)
```

- [ ] **Step 2: Run the targeted test and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
ctest --test-dir build -R jojo_production_readiness_contract --output-on-failure
```

Expected: FAIL with `production readiness manifest is missing`.

- [ ] **Step 3: Add the canonical manifest with truthful baseline statuses**

Create `docs/architecture/PRODUCTION-READINESS.tsv`:

```text
# workstream\tstatus\tevidence\tblocker
R2.1\timplemented-unverified\tdocs/superpowers/specs/2026-09-01-production-completion-design.md\tnone
R2.2\tblocked-external-evidence\tnone\tlegally-supplied-supported-commercial-image
R2.3\tnot-started\tnone\tnone
R2.4\tnot-started\tnone\tnone
R2.5\tnot-started\tnone\tnone
R2.6\tnot-started\tnone\tnone
```

- [ ] **Step 4: Re-run the targeted test and verify GREEN**

Run the same CTest command.

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add cmake/CheckProductionReadiness.cmake CMakeLists.txt docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "test: add production readiness contract gate"
```

---

### Task 2: Make readiness states self-consistent and machine-check release claims

**Files:**
- Modify: `cmake/CheckProductionReadiness.cmake`
- Create: `tests/fixtures/production-readiness-invalid.tsv`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: optional `JOJO_READINESS_FILE`; defaults to the canonical manifest.
- Produces: positive CTest for the canonical manifest and negative CTest proving a false verified claim is rejected.

- [ ] **Step 1: Add a RED fixture for a false production claim**

Create `tests/fixtures/production-readiness-invalid.tsv`:

```text
# workstream\tstatus\tevidence\tblocker
R2.1\tverified\tnone\tnone
R2.2\tverified\tnone\tlegally-supplied-supported-commercial-image
R2.3\tnot-started\tnone\tnone
R2.4\tnot-started\tnone\tnone
R2.5\tnot-started\tnone\tnone
R2.6\tnot-started\tnone\tnone
```

Register a CTest expected to fail:

```cmake
add_test(
    NAME jojo_production_readiness_rejects_false_verified
    COMMAND ${CMAKE_COMMAND}
        -DJOJO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DJOJO_READINESS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/production-readiness-invalid.tsv
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckProductionReadiness.cmake
)
set_tests_properties(jojo_production_readiness_rejects_false_verified PROPERTIES WILL_FAIL TRUE)
```

- [ ] **Step 2: Run the negative test before adding override support**

Run:

```bash
ctest --test-dir build -R jojo_production_readiness_rejects_false_verified --output-on-failure
```

Expected: FAIL as a CTest result because the script still ignores `JOJO_READINESS_FILE` and validates the canonical file successfully.

- [ ] **Step 3: Implement explicit manifest override**

Replace the fixed manifest assignment with:

```cmake
if(DEFINED JOJO_READINESS_FILE)
    set(readiness_file "${JOJO_READINESS_FILE}")
else()
    set(readiness_file "${JOJO_SOURCE_DIR}/docs/architecture/PRODUCTION-READINESS.tsv")
endif()
```

- [ ] **Step 4: Run both readiness tests**

Run:

```bash
ctest --test-dir build -R "jojo_production_readiness_(contract|rejects_false_verified)" --output-on-failure
```

Expected: both PASS; the second passes because the script exits non-zero for the intentionally invalid fixture and CTest has `WILL_FAIL TRUE`.

- [ ] **Step 5: Commit**

```bash
git add cmake/CheckProductionReadiness.cmake CMakeLists.txt tests/fixtures/production-readiness-invalid.tsv
git commit -m "test: reject false verified readiness claims"
```

---

### Task 3: Align README, roadmap and next-milestone documentation with R2

**Files:**
- Modify: `README.md`
- Modify: `docs/NEXT-MILESTONES.md`
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv`

**Interfaces:**
- Consumes: the R2 workstream names/status vocabulary from the production-completion spec.
- Produces: human-readable documentation that points to the canonical readiness manifest and no longer says M2 is the next milestone or that the reusable native backend is simply “not finished” without explaining the commercial-integration boundary.

- [ ] **Step 1: Add documentation assertions to the readiness script and verify RED**

Append checks that read the three human-facing files and require exact anchor phrases:

```cmake
file(READ "${JOJO_SOURCE_DIR}/README.md" readme_text)
file(READ "${JOJO_SOURCE_DIR}/docs/NEXT-MILESTONES.md" next_text)
file(READ "${JOJO_SOURCE_DIR}/docs/architecture/PRODUCTION-ROADMAP.md" roadmap_text)

foreach(required_readme_phrase
        "R2 — Production completion"
        "PRODUCTION-READINESS.tsv"
        "commercial-game integration is not yet verified")
    string(FIND "${readme_text}" "${required_readme_phrase}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "README is missing required readiness phrase: ${required_readme_phrase}")
    endif()
endforeach()

string(FIND "${next_text}" "R2.1 — Repository truth and release gates" found_next)
if(found_next EQUAL -1)
    message(FATAL_ERROR "NEXT-MILESTONES does not identify R2.1")
endif()

string(FIND "${roadmap_text}" "Production completion program (R2)" found_roadmap)
if(found_roadmap EQUAL -1)
    message(FATAL_ERROR "roadmap does not reference R2 production completion")
endif()
```

Run the readiness contract.

Expected: FAIL on the first missing documentation phrase.

- [ ] **Step 2: Rewrite the README production-status section**

Replace the stale “Current production milestone” wording with a compact section that states:

```markdown
## Current production program

The reusable architecture milestones M1–M8 are complete within their scoped contracts. The active program is **R2 — Production completion**, which requires evidence for real commercial-game integration before any global playability claim.

Canonical machine-checkable status: [`docs/architecture/PRODUCTION-READINESS.tsv`](docs/architecture/PRODUCTION-READINESS.tsv).

Commercial-game integration is not yet verified. In particular, a supported commercial revision still requires independently verified fingerprints from a legally supplied user image, and real game boot/render/audio/input/gameplay evidence remains outside the completed reusable contracts.
```

Keep the one-executable product direction and copyright boundary unchanged.

- [ ] **Step 3: Replace `docs/NEXT-MILESTONES.md` with the actual next workstream**

Use:

```markdown
# Next milestones

The reusable architecture roadmap is [`architecture/PRODUCTION-ROADMAP.md`](architecture/PRODUCTION-ROADMAP.md). Production-completion status is machine-checked from [`architecture/PRODUCTION-READINESS.tsv`](architecture/PRODUCTION-READINESS.tsv).

The active subproject is **R2.1 — Repository truth and release gates**. It aligns repository claims, conversion readiness, CI and release criteria before commercial revision enablement or device/gameplay integration.

After R2.1 is verified, **R2.2 — Commercial revision enablement** may advance only when legally supplied media is available for independently verified fingerprints; otherwise it remains `blocked-external-evidence`.
```

- [ ] **Step 4: Extend the roadmap with an explicit R2 section**

After M9, add:

```markdown
### Production completion program (R2)

M1–M8 completion means their reusable contracts are complete; it does not establish commercial-game playability. The evidence-based production-completion program is defined by [`../superpowers/specs/2026-09-01-production-completion-design.md`](../superpowers/specs/2026-09-01-production-completion-design.md) and tracked by [`PRODUCTION-READINESS.tsv`](PRODUCTION-READINESS.tsv).

Ordered workstreams: R2.1 repository truth/release gates; R2.2 commercial revision enablement; R2.3 game-specific execution/device integration; R2.4 real gameplay integration; R2.5 online product modes/M9; R2.6 production validation/release.
```

- [ ] **Step 5: Mark R2.1 verified only after the automated gate passes**

Change only the R2.1 row to:

```text
R2.1\tverified\tctest:jojo_production_readiness_contract\tnone
```

Do not change any later workstream status.

- [ ] **Step 6: Run readiness tests and full portable suite**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected: all tests PASS.

- [ ] **Step 7: Commit**

```bash
git add README.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-ROADMAP.md docs/architecture/PRODUCTION-READINESS.tsv cmake/CheckProductionReadiness.cmake
git commit -m "docs: align repository with R2 production readiness"
```

---

### Task 4: CI release gate and final branch evidence

**Files:**
- Modify: `.github/workflows/build.yml`
- Modify: `docs/architecture/PRODUCTION-READINESS.tsv` only if evidence naming needs to point at the permanent CI gate; no later R2 status changes.

**Interfaces:**
- Consumes: CTest readiness tests registered by Tasks 1–3.
- Produces: Linux and Windows CI that visibly execute the same readiness contract as part of the normal suite, with the shipping Windows executable still the only application artifact.

- [ ] **Step 1: Add named readiness steps before the full test steps**

In the Linux job, after build:

```yaml
      - name: Production readiness gate
        run: ctest --test-dir build -R "jojo_production_readiness_" --output-on-failure
```

In the Windows job, after Release build:

```yaml
      - name: Production readiness gate
        run: ctest --test-dir build -C Release -R "jojo_production_readiness_" --output-on-failure
```

Keep the existing full `ctest` steps immediately after these gates and keep artifact upload unchanged.

- [ ] **Step 2: Run local full suite and inspect workflow diff**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
git diff --check
```

Expected: all tests PASS and `git diff --check` reports no whitespace errors.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/build.yml
git commit -m "ci: enforce R2 production readiness gate"
```

- [ ] **Step 4: Verify branch CI before merge**

Expected branch evidence:

```text
Portable core / Linux: configure PASS, build PASS, Production readiness gate PASS, full CTest PASS.
Windows x64 / MSVC 2022: configure PASS, Release build PASS, Production readiness gate PASS, full Release CTest PASS, JOJO-Recompiled-Windows-x64 artifact upload PASS.
```

Only after those checks pass is R2.1 considered verified. R2.2 remains blocked on legal media evidence and R2.3–R2.6 remain unverified/not-started according to the canonical manifest.
