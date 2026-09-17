# Building JOJO Recompiled on Windows

## Requirements

- Windows 10 or Windows 11 x64
- Visual Studio 2022 with **Desktop development with C++**
- MSVC v143 x64/x86 build tools
- Windows 10/11 SDK
- CMake 3.20+

## Build

Open **Developer PowerShell for VS 2022** in the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckProductionReadinessNegative.cmake
cmake -DJOJO_SOURCE_DIR=$PWD -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build -C Release --output-on-failure
```

Expected end-user application:

```text
build\Release\JOJO-Recompiled.exe
```

Developer test binaries are not part of the end-user package.

## First launch — current PS1 M1 contract

1. Open `JOJO-Recompiled.exe`.
2. Select a legally obtained image of your own PS1 copy (`.iso`, `.bin`, or `.cue`). `.gdi` is not supported by the active PS1 architecture.
3. Choose the installation folder. `%LOCALAPPDATA%\JOJO Recompiled\game` is only the proposed default.
4. Click **PREPARAR JOGO**.
5. The application fingerprints the source, opens the PS1 filesystem, resolves `SYSTEM.CNF`, validates the `PS-X EXE`, and installs a transactional local generation when those stages succeed.

The source image is read-only and is never modified.

At M1, successful preparation must stop truthfully after executable validation/installation with R3000A/MIPS analysis still pending. R3000A execution, native x64 code generation, boot, rendering, audio, original-game input integration, and gameplay are not M1 capabilities.

For the commercial image, the corrected PS1 path still needs a fresh local run before commercial `PS-X EXE` discovery can be marked observed.
