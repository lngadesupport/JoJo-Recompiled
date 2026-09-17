# M7 Training Laboratory Verification

## Scope closed

M7 implements the reusable deterministic training runtime defined by `docs/superpowers/specs/2026-08-30-training-laboratory-design.md`:

- bounded monotonic frame timeline;
- startup/active/recovery frame-meter segments with mandatory labels and icon tokens;
- hitstop, hitstun, blockstun, signed advantage and cancel-window diagnostics;
- combo count, cumulative damage and scaling data;
- logical attack/vulnerable/push collision geometry with validation;
- stable two-player input history using the M5 resolved-input contract;
- simulation pause and exact bounded frame-step permissions;
- ten deterministic training-state slots with SHA-256 integrity validation.

## TDD evidence

- RED run `33295770713` at `2dcd1281a937f6328d0ac4fde9d4de90ed347558`: the permanent `tests/test_training.cpp` contract failed on Linux exactly because `core/training.h` did not exist. The temporary RED workflow was removed before final integration.
- GREEN build run `33295869315` at `6f014a0724364201e5c0bea9136d0e9dbedbe127`: Portable core / Linux configured, built and passed CTest; Windows x64 / MSVC 2022 configured, built Release, passed CTest and uploaded the single executable artifact.
- Final pre-PR verification run `33296019892` at `76ad7115e20fc523cbad11d792daffb362ac6dff`: Portable core / Linux and Windows x64 / MSVC 2022 both completed successfully, including CTest and Windows executable upload.
- Final pre-PR Windows artifact: `JOJO-Recompiled-Windows-x64`, artifact `9727451760`, digest `sha256:ddff024451d418e085f05759992248e5158ce867af4019eca26bd67cac9b7dea`.
- Final cleanup removed `.github/workflows/m7-red.yml`; the normal build workflow is the only permanent CI path.

## Readiness boundary

M7 completion closes the portable training-laboratory contract. It does not claim that a commercial revision already supplies real character phase, combat, collision or complete runtime snapshot data to the adapter. No proprietary game offsets, assets, extracted data or commercial fingerprints are included.
