# PS1 Commercial Boot Frontier — M7 Design

Date: 2026-09-17
Status: Design approved in chat; written spec pending final user review
Base: `feature/ps1-retire-install-runtime-m6`

## 1. Roadmap boundary

This specification starts a new phase. It does not extend the six milestones that were previously defined for the direct-source migration.

### Phase A — Direct-source migration (complete)

M1–M6 are complete. Their purpose was to replace the extracted-installation model with a PS1 direct-source product contract:

1. direct `Ps1DiscSession` over ISO/BIN/CUE;
2. R3000A checkpoint directly from the original image;
3. persistent source binding and `Data/ROM` discovery;
4. Win32 validate/play/checkpoint UX without a prepare/install flow;
5. sector/file streaming contract from the original media;
6. retirement of the legacy conversion/installation runtime from the shipping build and default CTest graph.

Phase A is closed.

### Phase B — Commercial-game execution (starts here)

M7 is the first milestone of the next phase. The goal of Phase B is to advance the supported commercial JoJo PS1 image from deterministic boot evidence toward first frame, then gameplay, by implementing only the hardware/BIOS behavior the game actually requires.

## 2. Purpose of M7

M7 introduces a deterministic **commercial boot frontier** workflow. The runtime must execute the supported user-supplied PS1 image until it reaches the first dependency that is not yet implemented, then emit enough evidence to implement that dependency without guessing.

M7 is not a general PlayStation emulator milestone. It is a JoJo-specific reverse-engineering and compatibility milestone.

## 3. Chosen approach

Use a frontier-driven implementation strategy.

The alternatives considered were:

- pre-implement broad PS1 subsystems before observing demand from JoJo;
- begin broad static decompilation before resolving runtime hardware dependencies.

Both increase scope prematurely. The selected approach preserves a narrow product-specific architecture: execute, observe the first real dependency, implement exactly what is required, and repeat.

## 4. Architecture

The commercial evidence path is:

```text
user BIN/CUE/ISO
    -> Ps1DiscSession (kept alive for the execution)
    -> SYSTEM.CNF / PS-X EXE
    -> Ps1BootRuntime / R3000A reference executor
    -> BIOS/MMIO/CD-ROM/DMA/GPU boundary instrumentation
    -> CommercialBootFrontierReport
```

The original image remains read-only and authoritative. No extracted installation, `boot.psxexe`, `generations/`, or copied commercial assets may be introduced.

## 5. Components

### 5.1 Direct commercial evidence runner

Add a direct-disc evidence entry point that owns or retains the `Ps1DiscSession` for the whole run rather than extracting only the boot executable and discarding media context.

Responsibilities:

- open and validate the supported image through `Ps1DiscSession`;
- retain media access for future runtime CD-ROM requests;
- run with explicit diagnostic options;
- return a deterministic frontier report;
- never write to the source image.

### 5.2 Commercial boot frontier report

The report must identify the first blocking dependency and include only metadata/evidence needed for engineering. It must not serialize game assets or large raw memory dumps by default.

Required fields:

- source format, size/fingerprint and revision id;
- stop reason;
- retired instruction count;
- current PC and last opcode;
- bounded recent instruction trace;
- most recent BIOS table/selector and arguments when applicable;
- unsupported MMIO address, width, direction and value when applicable;
- recent CD-ROM command summaries;
- DMA transfer summaries/counters;
- GP0/GP1 command summaries/counters;
- VRAM write and presented-frame counters;
- whether diagnostic/speculative fallbacks were enabled;
- a stable frontier classification suitable for regression tests.

### 5.3 Diagnostic mode isolation

Speculative behavior used for exploration must never silently become normal runtime behavior.

Rules:

- normal mode stops on an unimplemented dependency;
- diagnostic mode may use explicitly selected fallbacks to gather evidence;
- every fallback decision is recorded in the report;
- a fallback cannot be promoted to production behavior without a dedicated test proving the real semantics needed by JoJo.

### 5.4 Max3 direct-disc integration

The existing Max3 exploration machinery may be reused for bounded diagnostic branching, but it must consume direct-disc runtime state rather than an installed-generation path.

The Max3 explorer remains a development/reverse-engineering tool, not a user-facing emulator mode.

## 6. Frontier taxonomy

M7 recognizes these frontier classes:

- `bios_call`;
- `mmio_access`;
- `cdrom_command`;
- `dma_operation`;
- `gpu_gp0_command`;
- `gpu_gp1_command`;
- `cpu_boundary`;
- `diagnostic_stall`;
- `commercial_frame_presented`;
- `fatal_runtime_error`.

The report may carry the more detailed existing `Ps1BootStopReason`, but it must also expose one stable high-level frontier class.

## 7. Data flow

1. Resolve the source through binding, `Data/ROM`, or an explicit path.
2. Open the image read-only with `Ps1DiscSession`.
3. Validate revision and boot executable.
4. Create the boot runtime while retaining the disc session.
5. Execute with bounded trace/event capacities.
6. On the first unimplemented production dependency, stop deterministically.
7. Classify the frontier.
8. Emit a compact report under user diagnostics.
9. Use that report to define the next narrow implementation milestone.

## 8. Error handling

- Unsupported/revision-mismatched media fails before execution.
- Missing CUE companion tracks fail with a media-specific error.
- Source mutation between binding creation and reopen invalidates the binding.
- Report-write failure must not mutate the source and must be reported separately from runtime failure.
- Diagnostic fallbacks must be visible in the report and must never be enabled by default in the shipping play path.

## 9. Testing strategy

### Synthetic tests

Use existing PS1 fixtures to prove:

- direct-disc evidence runner retains sector access after boot-executable parsing;
- each frontier class is classified deterministically;
- bounded traces/events do not grow without limit;
- diagnostic fallback decisions are recorded;
- normal mode stops rather than guessing on unsupported behavior;
- reports contain metadata but do not embed commercial payloads;
- source bytes are unchanged before and after the run.

### Regression matrix

Run full CMake/CTest on Linux and Windows x64 for every M7 integration commit.

### Commercial evidence

A user-supplied supported JoJo image is used only to produce local diagnostic evidence. The repository and CI must not contain the commercial image or derived copyrighted assets.

## 10. Success criteria

M7 is complete when all of the following are true:

1. a direct-disc commercial evidence runner exists;
2. it keeps media available for runtime requests;
3. it stops deterministically at the first unsupported production dependency;
4. the frontier is classified and reported with enough context for a targeted implementation;
5. diagnostic fallbacks are isolated from normal runtime behavior;
6. synthetic regression tests pass on Linux and Windows x64;
7. the supported commercial JoJo image can produce a reproducible first-frontier report locally without extraction.

M7 does **not** require first frame, audio, full GPU, full CD-ROM, or gameplay. Those become later Phase B milestones in the order revealed by real JoJo execution.

## 11. Non-goals

M7 will not:

- implement a general PS1 emulator;
- pre-implement every BIOS call or hardware register;
- add BIOS redistribution;
- add ROM acquisition/downloading;
- restore the legacy extracted-installation architecture;
- claim the commercial game is playable.

## 12. Follow-on milestone rule

After M7 identifies the first real frontier, the next milestone is named and scoped from that evidence, for example `M8 — JoJo CD-ROM Getloc/ReadN frontier` or `M8 — JoJo GPU GP0 primitive frontier`.

Therefore the total number of Phase B milestones is intentionally not fixed in advance. They are evidence-driven and stop when the target game reaches the desired completeness level.