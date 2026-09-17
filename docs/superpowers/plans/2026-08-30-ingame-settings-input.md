# M5 In-game Settings + Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the reusable M5 runtime contract for graphics/audio/control settings, two-player remapping, hot-plug and Windows keyboard/XInput/HID input.

**Architecture:** Keep portable settings, input resolution and in-game menu state in `jojo_core`; keep device enumeration and Windows event/polling details in `src/platform/windows`. Persist logical devices/control codes only, so cable/Bluetooth/dongle transport does not leak into bindings.

**Tech Stack:** C++20, CMake/CTest, Win32, SetupAPI, HID, Raw Input, dynamically loaded XInput.

**Spec:** `docs/superpowers/specs/2026-08-30-ingame-settings-input-design.md`

## Global Constraints

- Runtime graphics/audio/control settings do not appear in the first-run conversion shell.
- Support two players with per-player selected devices and full remapping.
- Support keyboard, XInput and generic HID; USB/Bluetooth/dongle remain transport-transparent.
- Preserve legacy player-1 input INI keys while writing the new two-player schema.
- No proprietary game data, assets, hashes or extracted commercial files.
- New behavior follows RED -> GREEN -> refactor and must pass Linux + Windows/MSVC CI.

---

### Task 1: Portable M5 RED contract

**Files:**
- Create: `tests/test_m5_settings_input.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `AppSettings`, `InputBinding`, `GameAction`.
- Produces requirements for `AudioSettings`, `PlayerInputSettings`, `InputDeviceInfo`, `InputDeviceRegistry`, `InputFrame`, `resolve_player_actions`, `capture_binding`, and `SettingsMenuSession`.

- [ ] **Step 1: Write the failing test**

Create a portable test that asserts: audio validation and INI round-trip; two independent players round-trip; legacy P1 keys still load; device registry emits connected/disconnected diffs; per-player action resolution works from synthetic keyboard/XInput/HID states; binding capture detects a newly pressed button and axis direction; settings-menu draft/commit/discard and device selection work.

- [ ] **Step 2: Wire the test into CMake**

Add `jojo_m5_settings_input_tests` linked against `jojo_core` and register it with CTest.

- [ ] **Step 3: Verify RED**

Push only tests/CMake. Expected Linux and Windows failure: compile errors for the missing M5 types/functions, proving the contract is not already implemented.

- [ ] **Step 4: Commit**

Commit message: `test: require complete M5 settings and input contract`.

---

### Task 2: Portable input/settings/menu GREEN

**Files:**
- Modify: `src/core/input.h`
- Modify: `src/core/input.cpp`
- Modify: `src/core/settings.h`
- Modify: `src/core/settings.cpp`
- Create: `src/core/settings_menu.h`
- Create: `src/core/settings_menu.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `PlayerInputSettings`, `InputDeviceInfo`, `InputDeviceState`, `InputFrame`, `ResolvedPlayerInput`, `InputDeviceRegistry`, `resolve_player_actions`, `capture_binding`, `AudioSettings`, `validate_audio`, `SettingsPage`, `SettingsMenuSession`.

- [ ] **Step 1: Implement two-player input state**

Replace the single input settings payload with `std::array<PlayerInputSettings, 2> players`; player 1 retains existing defaults, player 2 receives a separate keyboard default map. Add deterministic device/frame structures, action resolution and thresholded axis bindings.

- [ ] **Step 2: Implement capture and hot-plug registry**

`capture_binding` compares previous/current snapshots for one device and returns the first newly pressed key/button or newly crossed axis (`<axis>+` / `<axis>-`). `InputDeviceRegistry::refresh` sorts/deduplicates by ID and returns explicit connected/disconnected changes.

- [ ] **Step 3: Add audio and backward-compatible persistence**

Validate volumes in `[0,100]`; load/save master/music/effects/mute fields. Read legacy `selected_device` and `bind.<action>` into player 1, and new `selected_device.pN` / `bind.pN.<action>` keys for both players. Save only the new schema.

- [ ] **Step 4: Add settings-menu session**

Implement a portable draft session with pages graphics/audio/controls, selected player, validated device selection and rebinding, `commit()` and `discard()`.

- [ ] **Step 5: Verify GREEN**

Run branch CI. Expected: portable M5 test and all existing tests pass on Linux and Windows.

- [ ] **Step 6: Commit**

Commit message: `feat: add portable M5 settings and input runtime`.

---

### Task 3: Windows input-host RED contract

**Files:**
- Create: `tests/test_win32_input.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: portable `InputDeviceInfo` / `InputFrame`.
- Produces requirements for Windows enumeration, keyboard-code mapping, XInput snapshot conversion and raw-input host registration/dispatch.

- [ ] **Step 1: Write Windows-only failing tests**

Assert keyboard enumeration is always present and stable; helper mapping names common keyboard virtual keys; XInput conversion maps buttons/axes to portable codes; the host exposes refresh/snapshot APIs without requiring a physical controller.

- [ ] **Step 2: Verify RED on Windows CI**

Expected Windows compile failure for the missing host APIs while Linux remains green because the test target is under `WIN32`.

- [ ] **Step 3: Commit**

Commit message: `test: require Windows M5 input host`.

---

### Task 4: Windows keyboard/XInput/HID GREEN

**Files:**
- Modify: `src/platform/windows/controller_win32.h`
- Modify: `src/platform/windows/controller_win32.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `enumerate_input_devices`, `keyboard_code_from_virtual_key`, `Win32InputHost::refresh_devices`, `Win32InputHost::register_raw_input`, `Win32InputHost::handle_raw_input`, `Win32InputHost::snapshot`.

- [ ] **Step 1: Reuse portable device kinds**

Return UTF-8 `InputDeviceInfo` values from enumeration, preserve keyboard/XInput/HID IDs, and filter XInput shadow HID interfaces.

- [ ] **Step 2: Implement keyboard and XInput snapshotting**

Map Windows virtual keys to stable strings (named common keys, alphanumeric/function keys, deterministic fallback). Dynamically load XInput and expose A/B/X/Y, shoulders, d-pad, start/back, stick buttons, triggers and normalized stick axes.

- [ ] **Step 3: Implement generic HID Raw Input**

Register joystick/gamepad Raw Input usages; decode incoming HID reports using preparsed HID data into stable usage-based `HID_BUTTON_*` and `HID_AXIS_*` codes and normalized values. Raw input updates latest per-device state; snapshots combine keyboard/XInput/HID state.

- [ ] **Step 4: Implement refresh/hot-plug integration**

`refresh_devices` re-enumerates and returns portable registry changes; disconnected state is removed while persisted bindings remain untouched.

- [ ] **Step 5: Verify GREEN**

Run Windows CI and the full test suite. Expected: Windows host test passes and `JOJO-Recompiled.exe` still builds/uploads.

- [ ] **Step 6: Commit**

Commit message: `feat: add Windows keyboard XInput and HID host`.

---

### Task 5: Close M5 and integrate

**Files:**
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`
- Update: `docs/superpowers/plans/2026-08-30-ingame-settings-input.md`

**Interfaces:**
- Consumes: verified M5 implementation and CI evidence.
- Produces: explicit M5 100% checklist and readiness boundary.

- [ ] **Step 1: Update roadmap**

Mark M5 complete only for the reusable settings/input contract: graphics/audio/control runtime menu model, keyboard/XInput/HID, transport-transparent bindings, two-player remapping, capture and hot-plug. State explicitly that rendered commercial-game menu integration, Maple delivery and AICA audio playback remain later real-game integration work.

- [ ] **Step 2: Run final branch verification**

Require Linux + Windows/MSVC configure/build/CTest green and Windows executable artifact upload green.

- [ ] **Step 3: Open PR and verify head SHA**

Create a PR with TDD evidence and readiness boundary. Merge only the validated head SHA.

- [ ] **Step 4: Verify post-merge main**

Require `main` Linux and Windows/MSVC CI green before declaring M5 complete.
