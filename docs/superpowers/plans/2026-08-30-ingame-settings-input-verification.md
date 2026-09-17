# M5 In-game Settings + Input Verification

## Scope closed

This evidence closes the reusable M5 settings/input contract defined by `docs/architecture/PRODUCTION-ROADMAP.md` and `docs/superpowers/specs/2026-08-30-ingame-settings-input-design.md`.

Implemented and covered:

- runtime graphics, audio and controls settings remain outside the first-run conversion shell;
- two-player, per-device persisted bindings with backward-compatible player-1 INI loading;
- deterministic portable input snapshots, action resolution and binding capture;
- connected/disconnected device registry refresh without deleting persisted bindings;
- in-game settings draft/commit/discard model;
- Windows keyboard sampling;
- dynamically loaded XInput with button/trigger/stick translation;
- generic joystick/gamepad HID enumeration and Raw Input decoding;
- XInput HID-shadow filtering so the same physical controller is not exposed twice;
- USB cable, Bluetooth and wireless dongle remain transport-transparent because bindings persist logical device/control IDs rather than transport metadata.

## TDD evidence

- Windows RED run `33295128439` at `07683a4d8e15706433d8fe7917ace298249ef109`: Linux passed; Windows reached the new `test_win32_input.cpp` and failed because `DeviceKind`, `keyboard_code_from_virtual_key`, `translate_xinput_state` and `Win32InputHost` did not exist yet.
- Windows GREEN run `33295278372` at `3edddaf9a97b22a1e96ac1ffe7d7ef37888a56ba`: Portable core / Linux passed configure, build and CTest; Windows x64 / MSVC 2022 passed configure, Release build and CTest.
- The same GREEN run uploaded `JOJO-Recompiled-Windows-x64`, artifact `9727234753`, digest `sha256:7fa11db8bd93b3c98e7c25745de4acf6603efe3caf62d5d04e90b7a2336fc49d`.

## Readiness boundary

M5 completion means the reusable settings/menu/input runtime is complete. It does not claim that a commercial game revision currently reaches a rendered native settings screen, that Dreamcast Maple input is already wired into original game code, that AICA audio playback is complete, or that a converted commercial installation is `native-ready`. Those require later real-game/device integration and legally supplied media for end-to-end evidence.
