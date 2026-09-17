# M6 Mod Runtime Design

## Scope

M6 closes the reusable mod-loading/runtime contract defined by `docs/architecture/PRODUCTION-ROADMAP.md` without requiring proprietary game data or claiming real-game integration. The runtime supports deterministic data/script overlays and explicitly opt-in native plugins through a versioned C ABI. It owns discovery, manifest validation, semantic compatibility, dependencies, deterministic load order, conflicts, content hashing, and online policy.

M6 completion does **not** mean commercial game assets are already routed through the overlay, that a real game menu exposes mod controls, or that the installation is `native-ready`. Those remain integration work.

## Directory and manifest contract

A converted installation may contain `mods/<folder>/mod.ini`. Each mod root is self-contained. Symlinked files are rejected from hashing/overlay enumeration so a mod cannot escape its root through a link.

`mod.ini` is UTF-8, line-oriented `key=value`, with blank lines and `#`/`;` comments allowed. Required keys:

- `id`: stable lowercase identifier using `[a-z0-9][a-z0-9._-]*`.
- `name`: non-empty display name.
- `version`: SemVer `MAJOR.MINOR.PATCH`.
- `api_version`: SemVer version of the public mod API expected by the mod.
- `kind`: `data` or `native`.
- `gameplay`: `0/1` or `false/true`.

Optional keys:

- `entry`: relative native library path; required for `kind=native`, forbidden for `kind=data`.
- `depends`: comma-separated requirements `mod.id`, `mod.id@=1.2.3`, `mod.id@>=1.2.3`, or `mod.id@^1.2.3`.
- `conflicts`: comma-separated mod IDs that may not be enabled together.

Unknown keys are rejected. Relative paths must stay inside the mod root after lexical normalization and may not be absolute or contain a parent traversal.

## Semantic versions

The core exposes mod API version `1.0.0`. A manifest is API-compatible when its requested major equals the host major and its requested version is less than or equal to the host version. Dependency requirements support no version constraint, exact (`=`), at-least (`>=`), and compatible-major (`^`). `^1.2.3` accepts versions `>=1.2.3` and `<2.0.0`.

## Catalog, resolution, and load order

Discovery scans immediate subdirectories of the mods root for `mod.ini`, parses each manifest, and returns a catalog sorted by ID. Duplicate IDs are an error with both roots named in diagnostics.

`resolve_mod_set(catalog, requested_ids)` recursively enables dependencies. It rejects missing dependencies, unsatisfied dependency versions, explicit conflicts, API-incompatible mods, and cycles. The result is a stable topological load order; independent nodes use lexicographic mod ID as the tie-breaker so the same catalog resolves identically on every host.

## Data/script overlay

For each resolved `kind=data` mod, files under `<mod-root>/data/` form a virtual overlay. Relative logical paths are normalized to `/`. The loader returns the winning host path for each logical path using resolved load order (later mods override earlier mods) and emits a collision diagnostic containing both mod IDs and the logical path. This contract lets future game integration request logical assets/scripts without hard-coding host paths.

## Content identity

Every enabled mod receives a SHA-256 content digest over deterministic, sorted tuples of normalized relative path plus file bytes for all regular files in its root, including `mod.ini` and native binaries. Directory timestamps and host-specific separators are excluded.

The runtime exposes:

- `mod_set_hash`: SHA-256 over ordered `(id, version, content_hash)` for every resolved mod.
- `gameplay_hash`: SHA-256 over the same tuple only for `gameplay=true` mods.

Two installations with the same enabled mod content and order produce the same hashes on Linux and Windows.

## Online policy

Ranked sessions reject any enabled mod with `gameplay=true`; cosmetic/non-gameplay mods are allowed. Custom sessions may supply a required `mod_set_hash`; joining succeeds only on an exact match. Diagnostics distinguish ranked gameplay rejection from custom hash mismatch.

## Native plugin ABI

Native plugins are disabled unless the caller explicitly sets `allow_native_plugins=true`. A native manifest must provide `entry` and the library must export `jojo_get_native_mod_v1`.

The C header `src/mod_api/jojo_mod_api.h` defines ABI version 1 using fixed-width C types and POD structs only. The exported descriptor includes ABI version, mod ID, and `on_load`/`on_unload` callbacks. The loader validates ABI and mod ID against the manifest before calling `on_load`; partial load failure unloads already-loaded plugins in reverse order. Normal shutdown also unloads in reverse order.

Linux uses `dlopen`/`dlsym` and Windows uses `LoadLibraryW`/`GetProcAddress`. CI builds a tiny test plugin on each platform and proves opt-in, descriptor validation, callback invocation, and unload behavior.

## Error model

All public operations return existing `jojo::Result<T>` values with deterministic human-readable details. No malformed manifest, bad dependency, path traversal, plugin load failure, or hash I/O failure is silently ignored.

## File boundaries

- `src/core/semver.{h,cpp}`: SemVer and dependency requirement parsing/matching.
- `src/core/sha256.{h,cpp}`: portable SHA-256 bytes/files/hex utility.
- `src/core/mod_runtime.{h,cpp}`: manifests, discovery, resolution, overlay, hashes, online policy.
- `src/core/native_mod_loader.{h,cpp}`: opt-in dynamic-library lifecycle.
- `src/mod_api/jojo_mod_api.h`: public C ABI.
- `tests/test_mod_runtime.cpp`: portable manifest/dependency/overlay/hash/policy tests.
- `tests/test_native_mod_loader.cpp` and `tests/fixtures/test_native_mod.cpp`: real shared-library ABI tests on Linux and Windows.

## Completion gates

M6 reaches 100% only after: RED tests are observed; all implementation tests pass on Linux and Windows/MSVC; the final diff contains no temporary workflows or proprietary data; PR CI is green on both hosts; merge is protected by the validated head SHA; and post-merge CI on `main` is green on both hosts.