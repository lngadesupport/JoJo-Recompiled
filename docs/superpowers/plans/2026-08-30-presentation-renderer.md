# M4 — Renderer, presentation and aspect correction

## Scope
Close the host presentation/aspect contract in `docs/architecture/PRODUCTION-ROADMAP.md` without pretending that Dreamcast PVR2 scene rendering or commercial-game boot is complete.

## Completed contract

- [x] Simulation resolution is independent from presentation resolution.
- [x] Camera/presentation policies cover 4:3, 16:9, 16:10, 21:9 and 32:9.
- [x] Viewports are fitted and centered with one uniform presentation scale; no non-uniform stretch path is exposed.
- [x] UI uses aspect-derived logical coordinates, optional 16:9 HUD safe area, expanded HUD mode, DPI-aware automatic scale and explicit 75–150% scale from 480p through 8K.
- [x] Windowed, borderless and exclusive-fullscreen host plans are modeled; exclusive requests can fall back to borderless when the renderer/host capability is unavailable.
- [x] Texture filtering Off/2x/4x/8x/16x is negotiated against renderer capabilities.
- [x] MSAA Off/2x/4x/8x is negotiated against actual D3D11 device support.
- [x] Windows host code applies Win32 styles/display-mode requests without adding settings controls to the first-run conversion shell.
- [x] Linux portable-core and Windows x64/MSVC CI cover the complete M4 contract.

## TDD evidence

- Core RED: `a02b4cb0a1a63fdd2963b7a9a3ede85a25efb309` plus CMake wiring `504dbebb66382df803d79c14276bc4d9cb314fcb`. CI #572 failed after the existing core built because `core/presentation.h` did not exist.
- Core GREEN introduced `presentation.{h,cpp}`. The permanent presentation regression covers independent simulation/output resolutions, exact viewports for all five aspect ratios, camera horizontal expansion with preserved vertical extent, HUD safe-area/logical coordinates, DPI scaling, quality fallback and display-mode fallback.
- Win32 RED: `15bb1bd9efd58e5d50d538c6935a2af7d5a659d4`. The dedicated Windows RED run `33286254005` built the existing core and failed exactly because `app_win32/presentation_host.h` did not exist.
- Win32 GREEN: `7b31b8a1fea2493710f32b108224210d0d67d246` plus test harness `550743b56a9f9e27b930ca8537365add3bfcc4c6`. Windows run `33286382432` built and executed the host regression successfully, including a real D3D11 hardware-or-WARP device capability probe.
- Permanent integration: `9fde28e88ffea51604fc87992ca1d801e825d894` wires `presentation_host.cpp` into the Windows executable and `jojo_win32_presentation_tests` into normal CTest. Temporary workflow removal head `961c95eb68ea4f5282e50a8f3f80512881689e38` has no helper workflow in the final diff.
- Final branch CI #583 / run `33286509397` passed Linux build/tests and Windows x64/MSVC Release build/tests, including `jojo_win32_presentation_tests`, then uploaded the single Windows executable artifact.

## Implementation notes

`build_presentation_plan()` is a pure policy boundary. It selects the output extent, aspect-fitted viewport, camera expansion, UI logical/safe rectangles, requested/applied texture filtering and MSAA, and deterministic display-mode fallback. Simulation dimensions remain an input recorded separately and never drive presentation stretching.

The Win32 host layer creates a concrete style/display plan for windowed, borderless and exclusive fullscreen. `probe_d3d11_renderer_capabilities()` creates a D3D11 hardware device when possible and falls back to WARP, then queries multisample quality for 2x/4x/8x. The full anisotropic filter ladder through 16x is exposed for the D3D11 sampler contract.

## Readiness boundary

M4 completion means the **host renderer/presentation/aspect contract in the roadmap is complete**. It does **not** claim that PVR2 command-list/scene rendering is complete, that a commercial revision boots, that a real game frame/menu is visible, or that input/audio are functional. Conversion output remains non-`native-ready` until those later device/integration milestones are proven with legally supplied game data. No proprietary game data is present in M4 implementation or tests.
