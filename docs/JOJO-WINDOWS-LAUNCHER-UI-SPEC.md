# JoJo Recompiled — Windows Launcher UI Specification

Status: **approved product target**  
Platform: **Windows 10/11 x64**  
Scope: native JoJo Recompiled launcher/runtime UI. No emulator-facing menus.

## 1. Visual target

The launcher uses the approved composition:

- native desktop window titled **JoJo Recompiled**;
- dark atmospheric background with navy/black/teal gradients and restrained gold highlights;
- large **JOJO RECOMPILED** identity block on the left;
- decorative Stone Mask / JoJo-themed collector plaque beneath the logo;
- central area intentionally kept visually open/atmospheric;
- primary vertical menu on the right;
- disc/source selector along the bottom-left;
- build version along the bottom-right.

The UI must look like a PC game/port launcher, never like an emulator frontend.

## 2. Main navigation

The main menu contains exactly:

1. **START GAME**
2. **CONTROLS**
3. **SETTINGS**
4. **EXIT**

Selection is indicated by brighter gold text and a restrained horizontal highlight/star flare.
Keyboard, mouse and connected controller navigation must be supported. The interface must remain usable with keyboard-only input.

## 3. Disc/source workflow

The bottom-left source control shows a disc icon, **Select disc (.cue)**, and the current source name/status next to it.
Supported legal user-supplied sources remain .cue, .bin, and .iso, opened read-only by the runtime.
After the source has been validated and persisted, the launcher should reopen it automatically on later launches when possible.

## 4. SETTINGS pages

SETTINGS exposes only product-facing categories: **Video, Audio, Control, Accessibility**.

### Video

- Display Mode: **Windowed / Fullscreen / Borderless**
- Monitor
- Resolution
- Refresh Rate
- V-Sync
- FPS Limit
- Internal Resolution / Render Scale
- Anti-Aliasing: **Off / 2x / 4x / 8x / 16x** when supported by the active GPU/render format
- Texture Filtering: **Off / 2x / 4x / 8x / 16x**
- Aspect Ratio
- Integer Scaling
- Image filtering/sharpness
- PS1 dithering handling
- geometry/wobble correction when implemented
- texture perspective correction when implemented
- HDR when supported
- Brightness / Gamma / Contrast
- Overscan
- Low-latency mode

Unsupported hardware options must be hidden or disabled rather than silently accepted.

### Audio

- Master Volume
- Music Volume
- Effects Volume
- Output Device
- Mute When Unfocused
- latency/quality controls when the host implementation exposes them

### Control

The runtime must support arbitrary compatible Windows controllers through XInput, Raw HID, generic USB gamepads/joysticks, multi-axis HID devices, fight sticks / arcade sticks / USB arcade encoders, and keyboard.

Requirements: hotplug, independent P1/P2 device selection, per-device remapping, arbitrary HID button and axis capture, deadzone controls where applicable, D-pad/axis choice where applicable, and persistent profiles.
The UI must not assume an Xbox layout for unknown devices.

### Accessibility

- High-contrast UI
- Reduce flashing
- Reduce screen shake
- Hold assist
- Menu text scale

Accessibility settings must be persisted in the same settings file as the rest of the launcher configuration.

## 5. CONTROLS shortcut

The top-level **CONTROLS** item opens the Control settings page directly, rather than duplicating a second control implementation.

## 6. Runtime transition

**START GAME** launches the native JoJo Recompiled runtime path.
No RetroArch, PCSX-ReARMed, core selector, BIOS menu, playlist, shader menu, emulator driver menu, or other third-party emulator UI is part of the shipping interface.
During gameplay, the launcher may hide while the game window is active. The in-game pause/settings overlay will reuse the same product-facing categories.

## 7. First launch

On the first successful Windows launch, JoJo Recompiled creates **JOJO Recompiled.lnk** on the user's Desktop.
The shortcut targets the actual JOJO-Recompiled.exe, uses the executable directory as the working directory, uses the executable icon, does not require administrator privileges, and is not forcibly recreated after the first successful creation.

## 8. Implementation rule

Visual polish must not outrun behavior. A visible option must either work, be explicitly disabled, or be omitted.
The launcher UI is a product surface over the existing native runtime; it must not replace validated source handling, input, audio, video, save, diagnostics, or native/reference execution semantics.