# Production Foundation Design

## Scope

This milestone converts the verified prototype into the stable base for the larger JOJO Recompiled architecture. It deliberately does not implement the game-specific recompiler, renderer, mod runtime or online stack yet; it establishes interfaces they will consume.

## End-user application

Windows produces one shipping executable named `JOJO-Recompiled.exe`. Developer tests remain separate build artifacts but are not part of distribution. The old standalone converter and runtime executables are removed from the default build.

The app owns a writable root under `%LOCALAPPDATA%/JOJO Recompiled`:

- `settings.ini`
- `game/game_manifest.ini`
- `game/data/`
- `game/cache/`
- `game/logs/conversion.log`

First launch detects that the game installation is absent and presents only the conversion screen. Once a native-ready installation exists, later milestones route directly into the game. While the game-specific backend is pending, the app reports that state accurately rather than pretending the port is playable.

## Conversion progress

Core conversion accepts an optional progress callback. Events contain:

- stable stage enum;
- integer percentage 0..100;
- short machine-readable message key;
- human-readable UTF-8 detail.

The current foundation emits progress for validation, fingerprinting, directory preparation, manifest writing and completion. Future backend work inserts extraction/recompiler stages into the same contract. The Windows UI marshals worker-thread progress messages back to the UI thread.

The conversion log writes every stage to `game/logs/conversion.log`.

## First-run UI

The old tabbed settings launcher is removed. The first-run shell uses a custom-drawn dark purple/gold/magenta visual identity inspired by the dramatic geometric language of JoJo without shipping copyrighted artwork. It contains:

- product mark/title;
- source-image selector;
- primary prepare button;
- real progress bar + numeric percentage;
- current stage text;
- expandable/simple log area;
- close/minimize chrome.

Graphics and controls are not exposed here.

## Graphics model

The persisted model is prepared now for the in-game options milestone:

- resolution 640x480 through 7680x4320;
- aspect 4:3, 16:9, 16:10, 21:9, 32:9;
- texture filter Off/2/4/8/16;
- MSAA Off/2/4/8;
- display mode Windowed / Fullscreen / Borderless;
- V-Sync;
- UI scale Auto / 75 / 80 / 90 / 100 / 110 / 125 / 150;
- HUD safe-area mode 16:9-safe / expanded.

This milestone only persists/validates the values. Renderer application comes later.

## Build and CI

CMake 3.20+, C++20, MSVC v143 on Windows. GitHub Actions builds x64 Release and runs CTest on `windows-latest`. Linux CI also builds `jojo_core` + tests to keep portable core logic healthy.

## Failure behavior

- Missing/unsupported source: conversion fails before touching a valid existing installation.
- Conversion errors include stage + detail and leave no false `native-ready` manifest.
- Settings parser rejects unsupported enum values rather than silently coercing corrupted files.
- Backend pending is a distinct runtime state.

## Security/content boundary

The repository and release package contain no game image or game assets. The conversion flow only reads the user's selected source and writes derived metadata/data into the per-user application directory.
