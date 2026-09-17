# JOJO Recompiled — PS1 Direct Runtime M2

## Status

Milestone 2 is implemented and locally contract-verified on top of the approved M1 `Ps1DiscSession` boundary.

## Delivered

- `bootstrap_runtime_checkpoint_from_disc(...)`
- `bootstrap_runtime_checkpoint_from_disc_to_file(...)`
- direct `Ps1DiscSession -> Ps1BootRuntime` boot path
- no `convert_image()` call
- no install generation resolution
- no `boot.psxexe` materialization
- no `active_install.ini` creation
- source image remains read-only
- diagnostic overload writes only the explicitly requested bounded report

## Local RED -> GREEN evidence

RED failed because the two direct-runtime APIs did not exist.

GREEN contract:

```text
PS1 direct-runtime contract passed
```

The 4-instruction checkpoint produced:

```text
instructions_retired = 4
last_pc = 0x8001000C
stop_reason = execution_budget_exhausted
presented_frames = 0
```

## Repository integration boundary

The writable continuation repository is `lngadesupport/JoJo-Recompiled` and the feature branch is `feature/ps1-direct-runtime-m2`.

That repository was created empty, so the original `matheuz232/JOJO-Recompiled` baseline still has to be imported before a repository-wide CMake/CTest run can be authoritative. The feature implementation itself is preserved in the attached M1+M2 workspace package and integration notes.
