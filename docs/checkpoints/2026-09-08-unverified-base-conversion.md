# Unverified base conversion checkpoint

The Windows first-run converter may prepare a valid disc image even when no verified commercial revision profile is registered yet.

Safety boundary:

- the default UI path records the disc fingerprint as `unverified-fnv1a64-<hash>`;
- the installation remains `pending-game-specific-recompiler`;
- explicit conversion with `ConversionOptions{}` remains strict and rejects unknown revisions;
- no proprietary game data or commercial fingerprints are stored in the repository.

TDD evidence:

- RED: workflow run #1199 (`34288416246`) failed only the new unverified base conversion contract after the existing build, readiness gate and test suite passed;
- GREEN: workflow run #1201 (`34288587771`) passed the new contract, all existing Linux tests, Windows x64/MSVC Release tests, readiness gate, UDP contract and executable artifact upload.
