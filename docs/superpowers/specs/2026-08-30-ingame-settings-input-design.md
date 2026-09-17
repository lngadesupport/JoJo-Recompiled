# M5 In-game Settings + Input Design

## Scope

M5 closes the reusable runtime settings/input contract from `docs/architecture/PRODUCTION-ROADMAP.md`. Graphics, audio and controls are modeled as runtime/in-game settings and remain absent from the first-run conversion shell. The implementation supports two players, keyboard, XInput and generic HID devices, per-player/per-device bindings, binding capture and hot-plug refresh.

USB cable, Bluetooth and wireless dongle are transport details owned by Windows and the controller driver. Persisted bindings identify the logical input device and control, not the transport, so the same binding model works for every transport exposed by XInput or HID.

M5 completion does **not** claim that the commercial game currently reaches a rendered settings screen, that Maple input reaches the original game code, or that AICA audio playback is complete. Those are real-game/device integration gates. M5 provides the complete reusable menu/settings/input contract that the game runtime consumes once that integration path is live.

## Settings model

`AppSettings` contains three runtime groups:

- `GraphicsSettings`: resolution, aspect ratio, texture filtering, MSAA, display mode, VSync, UI scale and HUD safe area.
- `AudioSettings`: master, music and effects volume from 0 through 100 plus mute-when-unfocused.
- `InputSettings`: two `PlayerInputSettings` entries, each with a selected device and a full action-to-binding map.

Settings validation rejects unsupported enum values, out-of-range resolutions/volumes, malformed bindings and invalid player indexes.

The INI format stores new input keys as `selected_device.p1`, `selected_device.p2`, `bind.p1.<action>` and `bind.p2.<action>`. Existing legacy `selected_device` and `bind.<action>` keys continue to load as player 1 so older development configs are not broken.

## Input contract

A runtime input frame is a deterministic snapshot of connected logical device states. Each device state contains:

- stable `device_id`;
- device kind (`keyboard`, `xinput`, `hid`);
- currently pressed button/key codes;
- normalized axes in the range `[-1, 1]` (triggers may use `[0, 1]`).

Button bindings match exact codes. Axis bindings use a base axis code followed by `+` or `-`; the portable resolver applies a fixed activation threshold. The same resolver produces per-player `GameAction` states from the two player binding maps.

Binding capture compares a previous and current frame. The first newly pressed control or newly crossed axis threshold on the selected device becomes a new `InputBinding`. This makes every host-reported keyboard key, XInput control or HID usage remappable without hard-coded game actions.

## Device catalog and hot-plug

`InputDeviceRegistry` owns a sorted, deduplicated catalog of `InputDeviceInfo` values. Refreshing it against a newly enumerated snapshot produces explicit connected/disconnected changes and updates the live catalog atomically.

A disconnected device does not delete persisted bindings. When it reconnects with the same logical ID the existing bindings become usable again. The in-game settings session can refuse selection of a device that is not currently present while preserving old settings on cancel.

## In-game settings session

`SettingsMenuSession` owns a baseline and a draft `AppSettings` value. It exposes three pages (`graphics`, `audio`, `controls`), player selection, device selection and per-action rebinding. Changes are draft-only until `commit()`; `discard()` restores the baseline. This keeps menu interaction separate from file I/O and lets the future rendered game menu drive one portable controller.

The conversion UI remains untouched by this model.

## Windows host

Windows uses one reusable input host:

- keyboard state is sampled into stable key codes;
- XInput is loaded dynamically (`xinput1_4`, `xinput1_3`, `xinput9_1_0`) and sampled for all four slots;
- HID devices are enumerated through SetupAPI/HID and receive Raw Input events registered for joystick/gamepad usage pages;
- XInput HID shadow endpoints are filtered to avoid duplicate logical controllers;
- `WM_DEVICECHANGE`/caller-triggered refresh re-enumerates the catalog;
- Raw HID reports are decoded from HID preparsed data into stable usage-based button and axis codes.

The host never encodes USB/Bluetooth/dongle into the binding schema.

## File boundaries

- `src/core/input.{h,cpp}`: device/input frame types, per-player settings, resolver, binding capture and device registry.
- `src/core/settings.{h,cpp}`: graphics/audio/input persistence and validation.
- `src/core/settings_menu.{h,cpp}`: in-game settings draft/commit/discard state.
- `src/platform/windows/controller_win32.{h,cpp}`: Windows enumeration plus runtime input host.
- `tests/test_m5_settings_input.cpp`: portable M5 behavior.
- `tests/test_win32_input.cpp`: Windows host/enumeration behavior that does not require a physical controller.

## Completion gates

M5 reaches 100% only after failing RED tests are observed before production code, Linux and Windows/MSVC pass the complete suite, Windows still uploads the single `JOJO-Recompiled.exe`, the roadmap records explicit M5 completion criteria and readiness boundaries, the PR is merged at a validated head SHA, and post-merge `main` CI is green. No proprietary game bytes, assets or commercial fingerprints are added.