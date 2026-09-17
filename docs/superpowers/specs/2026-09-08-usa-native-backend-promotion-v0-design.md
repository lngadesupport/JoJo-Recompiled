# USA Native Backend Promotion v0 Design

## Scope

This milestone connects the observed USA disc revision `jojo-usa-observed-b8b5dbf79cdb9fcf` to the native Dreamcast/SH-4 backend that already exists in `jojo_core`.

The current converter can identify this exact user-observed BIN by whole-disc fingerprint, but it deliberately leaves the installation at `backend=pending-game-specific-recompiler`. This milestone adds a stricter promotion path. An installation may become `backend=native-ready` only after all of the following gates succeed on the user's local image:

1. the disc revision is recognized as the supported observed USA revision;
2. the Dreamcast boot program is found through the ISO9660/IP.BIN metadata path;
3. Dreamcast boot encoding and SH-4 analysis succeed without guessing;
4. the existing native backend cache is generated or reused for that exact boot program;
5. the generated cache is loaded back and verified against the current host ABI and program hash;
6. the final manifest is written atomically only after those checks succeed.

`native-ready` in this milestone means **the installation owns a validated game-specific native backend package for the recognized revision**. It does not claim that commercial gameplay, rendering, audio, input, timing, or complete end-to-end game boot have been validated. Those remain later runtime/gameplay milestones.

## Existing architecture reused

No second recompiler is introduced.

The implementation reuses the existing pipeline:

- `open_iso9660(...)` for read-only disc filesystem access;
- `read_dreamcast_boot_program(...)` for IP metadata plus boot executable discovery and FNV-1a fingerprinting;
- `analyze_dreamcast_boot_program(...)` for Dreamcast encoding checks, SH-4 opcode census, and entry CFG construction;
- `ensure_native_backend_cache(...)` for CFG -> IR -> host backend lowering, cache creation, and cache reuse;
- `load_native_backend_cache(...)` for serialized-plan validation;
- `native_backend_abi_version()` for host ABI binding.

The generic native backend remains shared infrastructure. The new game-specific layer decides whether a particular commercial revision is eligible to use it and whether the resulting cache is sufficient to promote the installation state.

## Game-specific preparation boundary

Add a focused game-backend preparation component under `src/core/` rather than expanding `conversion.cpp` with backend policy.

Its public responsibility is:

- accept a recognized `revision_id`, an already-open `Iso9660Image`, and the local installation directory;
- reject revisions that do not have an enabled game-specific backend path;
- extract and analyze the boot program in memory;
- build/reuse and then reload the native cache;
- return only derived metadata required by the conversion manifest and runtime validation.

The component does **not** write `game_manifest.ini` and does **not** decide UI progress. Conversion remains the owner of installation-state transitions.

The returned summary contains at minimum:

- boot-program FNV-1a64 hash;
- native-backend ABI version;
- native backend `program_hash`;
- block count;
- native-lowered block count;
- reference-fallback block count;
- native machine-code byte count.

The existing deterministic reference fallback remains allowed inside the current backend architecture. Its use must be recorded in diagnostics; `native-ready` therefore means that the approved backend package is valid and executable by the project's runtime, not that every SH-4 operation already has a dedicated x86-64 lowering.

## Promotion flow

The converter keeps the current source validation, whole-disc fingerprint, ISO9660 discovery, and revision-identification stages.

For unknown or unverified revisions, existing base-conversion behavior remains unchanged: the converter may produce a valid pending installation, but it must not attempt game-specific promotion.

For `jojo-usa-observed-b8b5dbf79cdb9fcf`, the converter performs the following sequence:

1. Create/validate `data/`, `cache/`, and `logs/`.
2. Write a **pending** manifest for the newly selected source before backend work starts. This invalidates any stale previous `native-ready` state after the new source has passed disc/revision validation.
3. Read the Dreamcast boot program from the already-open filesystem.
4. Run Dreamcast boot analysis. MIL-CD scrambling requirements, unknown media encoding, malformed boot metadata, empty programs, or CFG analysis errors are hard failures; the implementation must not guess.
5. Generate or reuse `cache/native/backend_cache.ini` and `cache/native/compiled_plan.bin` via `ensure_native_backend_cache(...)`.
6. Reload `compiled_plan.bin` via `load_native_backend_cache(...)`.
7. Verify that the loaded plan ABI equals `native_backend_abi_version()`, that its `program_hash` equals the cache result, and that the loaded block/native/fallback counts match the promoted summary.
8. Populate the backend metadata fields in the manifest.
9. Set `backend=native-ready` only in memory after every check above has passed.
10. Atomically replace `game_manifest.ini` with the final promoted manifest.

If any backend step fails, conversion returns the specific error. The Windows application records the progress/error in `conversion.log`, and the on-disk manifest remains `pending-game-specific-recompiler`. No partial cache or manually edited manifest can promote the installation by itself.

## Manifest contract

Keep `manifest_version=1` for compatibility and extend it with optional backend fields. Existing pending manifests remain readable.

A `native-ready` manifest requires all backend fields below:

- `boot_program_hash_fnv1a64`;
- `backend_abi_version`;
- `backend_program_hash`;
- `backend_block_count`;
- `backend_native_block_count`;
- `backend_fallback_block_count`;
- `backend_native_code_bytes`.

Pending manifests may omit these fields.

The loader continues to accept existing version-1 pending manifests. However, runtime validation treats a `native-ready` manifest with missing or malformed backend metadata as `invalid_installation`.

## Runtime validation

`bootstrap_runtime(...)` must no longer trust the string `backend=native-ready` by itself.

For a native-ready installation it must additionally:

- require the supported USA revision ID for this v0 game-specific path;
- require the complete backend metadata set;
- load `cache/native/compiled_plan.bin`;
- require the current `native_backend_abi_version()`;
- require the plan `program_hash` to match the manifest;
- require serialized block/native/fallback counts to match the manifest summary.

A missing cache, incompatible ABI, edited manifest, stale cache, or hash/count mismatch returns a validation error and must not bootstrap.

This closes the current bypass where changing the manifest text to `native-ready` would otherwise be enough for `bootstrap_runtime(...)` to return success.

## Progress and UI semantics

Add explicit conversion stages for the backend work while keeping progress monotonic. The intended user-visible sequence is approximately:

- 0% source validation;
- 15% whole-disc fingerprint;
- 30% filesystem discovery;
- 45% revision identification;
- 55% local installation/pending-manifest preparation;
- 65% Dreamcast boot-program extraction and analysis;
- 80% native backend generation/reuse;
- 92% native cache verification;
- 97% final manifest promotion;
- 100% backend preparation complete.

When promotion succeeds, the Windows UI should say that the **native backend for the recognized USA revision is prepared** and that end-to-end game validation is the next milestone. It must not claim that gameplay is already complete.

When promotion fails, the UI continues to show the precise converter error and the pending installation remains inspectable for diagnostics.

## Security and proprietary-content boundary

The repository, tests, CI artifacts, and GitHub Actions must contain no commercial game image, boot executable, assets, music, or extracted proprietary files.

The user's selected image is read locally and read-only. Boot-program bytes exist only in the local conversion process while analysis/backend generation is running. The persistent files added by this milestone are derived local cache/metadata under `%LOCALAPPDATA%/JOJO Recompiled/game/` and are never committed or uploaded by the project workflow.

Tests use only synthetic Dreamcast/ISO9660 fixtures and synthetic SH-4 programs. The observed commercial fingerprint and derived hashes may be represented as metadata, but no source bytes from the game are embedded in fixtures.

## Failure and atomicity rules

- Unsupported or malformed source media fails before a valid existing installation is modified.
- Unknown/unverified game revisions retain the existing successful base-conversion path and remain `pending-game-specific-recompiler`.
- After a new source is positively identified as a revision eligible for game-specific promotion, the converter first writes a pending manifest before attempting backend work, preventing stale `native-ready` state from surviving a failed re-prepare.
- Backend cache writers retain their existing temporary-file + replace behavior.
- Final `native-ready` promotion is a separate atomic manifest replacement after cache reload verification.
- A backend failure never deletes the user's source image.
- A backend failure never fabricates supported opcodes, media encoding, hashes, or revision identity.
- Re-running conversion with an unchanged program may reuse a valid ABI/core-version/program-hash cache; a stale or incompatible cache is rebuilt by the existing cache contract.

## Test strategy

All behavior is developed RED -> GREEN. No commercial data is required.

Required synthetic tests:

1. **Known revision backend preparation:** a synthetic Dreamcast ISO/boot program passed through the game-backend preparation component with the enabled USA revision ID generates a cache and returns a verified summary.
2. **Revision gate:** any other revision ID is rejected as `backend_unavailable` and cannot create a native-ready state.
3. **Boot analysis gate:** malformed Dreamcast metadata, unknown/MIL-CD encoding without normalization, or an unsupported reachable SH-4 opcode prevents promotion.
4. **Cache reload gate:** corrupt/truncated/ABI-mismatched compiled plans are rejected and cannot promote.
5. **Manifest integrity:** pending version-1 manifests remain loadable; native-ready manifests require complete backend metadata.
6. **Forged manifest defense:** `bootstrap_runtime(...)` rejects `backend=native-ready` when the cache is absent or its program hash/ABI/counts do not match.
7. **Atomic re-prepare:** an existing native-ready synthetic installation re-prepared with a backend-failing source ends in the pending state rather than retaining stale readiness.
8. **Windows x64 authority:** GitHub Actions Windows/MSVC Release runs the full promotion/bootstrap contract and remains the authority for the x64 native cache ABI. Linux CI continues to validate portable core behavior.

## Non-goals

This milestone does not:

- claim complete JoJo commercial boot or gameplay;
- verify PVR2 rendering, AICA audio, Maple input, timing, interrupts, menus, fights, saves, or rollback against the commercial game;
- replace the observed whole-disc USA fingerprint with an internal-file verified revision profile;
- eliminate every existing reference-fallback operation from the native backend;
- ship or extract game content into the repository or release artifact;
- introduce an emulator dependency or external Dreamcast runtime.

The immediate post-milestone experiment is to run the newly built Windows executable against the user's same recognized USA BIN and use the resulting local `conversion.log` plus derived backend metadata to identify the next real game-specific SH-4/runtime blocker.