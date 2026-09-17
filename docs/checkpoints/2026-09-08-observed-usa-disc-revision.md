# Observed USA disc revision checkpoint

The user-supplied conversion manifest established one observed whole-disc fingerprint for a USA BIN image without requiring proprietary game data in the repository.

Observed signature:

- format: `bin`
- source size: `666806112`
- FNV-1a 64: `b8b5dbf79cdb9fcf`
- revision id: `jojo-usa-observed-b8b5dbf79cdb9fcf`

Safety/readiness boundary:

- this is an observed whole-disc signature, not yet an internal-file verified revision profile;
- format, size, and hash must all match;
- mismatches remain unknown and may use the existing unverified base-conversion path;
- the installation remains `pending-game-specific-recompiler` and must not become `native-ready` from this identification alone.

TDD evidence:

- RED: workflow run #1209 (`34290207438`) failed only the new observed-disc revision contract after existing Linux build/readiness/tests passed;
- GREEN implementation: Linux passed the new contract in run #1211 (`34290355038`); Windows exposed a CI-only `/MT` vs `/MD` runtime mismatch while its production build and 56/56 CTest suite passed;
- GREEN final: workflow run #1212 (`34290702978`) passed Linux and Windows x64/MSVC 2022, including the observed-disc contract, readiness gate, full CTest suite, UDP contract, and executable upload.
