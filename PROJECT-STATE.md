# JOJO Recompiled — Project State

## Active development line

- Repository: `matheuz232/JOJO-Recompiled`
- Active branch: `feature/ps1-m3c-deep-checkpoint-evidence`
- Active platform: **Sony PlayStation 1**
- Product scope: **JoJo PS1 only**
- M3A code-head commit: `814c956eefd608080320aff270d59568fbb2b28a`
- M3A verification workflow: `34334677057` (run #1339)
- M3B evidence-capture code-head commit: `c460d34c8e46f0fddae026fbdcf570c1e7e3bedf`
- M3B evidence-capture verification workflow: `34404397974` (run #1349)
- M3C deep-evidence code-head commit: `b099cc5bee6b0ab14301daa966a45b2d9409a6c2`
- M3C verification workflow: `34411091433` (run #1367)
- Linux job: passed configure, build, production-readiness gate, PS1 active-architecture gate, CTest, observed-disc revision contract, and UDP transport contract.
- Windows job: passed configure, Release build, production-readiness gate, PS1 active-architecture gate, Release CTest, observed-disc revision contract, UDP transport contract, and artifact upload.
- Shipping policy: one `JOJO-Recompiled.exe`.

## PS1 foundation evidence

Verified by repository tests/CI:

- `.iso`, `.bin`, and `.cue` are the active PS1 source formats; `.gdi` is rejected.
- raw MODE1/2352 and MODE2/2352 logical-sector extraction remains supported.
- ISO9660 access is retained.
- strict `SYSTEM.CNF` boot-path parsing is implemented.
- `PS-X EXE` validation/metadata/hash handling is implemented.
- user-selectable installation root and transactional manifest-v2 installation are implemented.
- the observed USA whole-image fingerprint `bin / 666806112 / b8b5dbf79cdb9fcf` is recognized as `jojo-usa-observed-b8b5dbf79cdb9fcf`.
- Dreamcast/SH-4 guest architecture is absent from the active build/test path.

## R3000A reference core — M2

R3000A reference CPU semantics are implemented and verified by synthetic Linux/Windows contracts.
Commercial JoJo boot, rendering, audio, input and gameplay are not verified.

The synthetic M2 contract covers MIPS decoding, integer and HI/LO semantics, precise exceptions, delay-slot control flow, aligned and unaligned memory operations, one-instruction GPR load delay, COP0/RFE/interrupt admission, explicit COP2/GTE boundaries, deterministic PS-X EXE CPU-state initialization and deterministic replay.

The R3000A reference executor is the semantic oracle for later compiler stages; it is not a Windows x64 native recompilation backend.

## JoJo PS1 visible-boot path — M3A

JoJo PS1 M3A memory/bus, PS-X EXE payload loading, and bounded R3000A boot checkpoints are implemented and verified by synthetic Linux/Windows contracts.
Commercial JoJo boot, BIOS/HLE progress, device progress, rendering, audio, input and gameplay are not verified by M3A.

The synthetic M3A contract covers:

- heap-backed 2 MiB PS1 main RAM;
- 1 KiB scratchpad;
- explicit JoJo-required KUSEG/KSEG0/KSEG1 RAM aliases without adding generic PS1 mirrors;
- little-endian 8/16/32-bit bus operations and bounded unsupported-access evidence;
- transactional PS-X EXE payload placement from offset `0x800` into the validated load address;
- exact R3000A `PC`, `next_pc`, `$gp` and `$sp` initialization through the M2 initializer;
- installation-backed checkpoint execution from the validated local `boot.psxexe`;
- explicit instruction-budget exhaustion;
- explicit A0/B0/C0 BIOS-call boundaries;
- explicit unsupported MMIO boundaries;
- bounded derived `Ps1BootReport` diagnostics;
- deterministic replay of two independent M3A runtime instances.

The immutable implementation evidence is GitHub Actions run `34334677057` on code-head `814c956eefd608080320aff270d59568fbb2b28a`, with both Linux and Windows jobs successful.

## M3B evidence-capture surface and first local commercial evidence

The Windows executable exposes `EXECUTAR CHECKPOINT` only when the selected local installation validates as PS1 M1/M3A-compatible and writes bounded derived diagnostics to `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/m3a-checkpoint.txt`.

The first user-supplied local derived report from the supported commercial JoJo installation reached the original 10,000-instruction budget and stopped with `execution_budget_exhausted`. Its final observed state was `last_pc=0x8001001c` and `last_opcode=0xac400000`. It reported zero BIOS calls, zero accepted interrupts, zero DMA transfers, zero GPU GP0/GP1 commands, zero VRAM writes and zero presented frames. BIOS, MMIO, CPU-boundary and unsupported-access fields were all `none`.

That report is evidence that the commercial executable retired 10,000 instructions through the current bounded reference path. It is **not** evidence of commercial boot, BIOS/HLE progress, device progress, rendering or playability, and it did not identify a BIOS selector/address or device boundary to implement.

## M3C deep bounded evidence probe

Because the first real report exhausted the 10,000-instruction budget before reaching a recognized boundary, M3C deepens evidence capture without fabricating any BIOS/device success:

- the local evidence policy is explicitly bounded at **1,000,000 instructions**;
- the report retains only the most recent **16 PC/opcode samples** as a bounded trace window;
- trace samples are serialized as additive `trace_*` fields in the existing derived report format;
- the installation-backed deep-evidence API reuses validation, bounded R3000A execution and atomic report writing;
- `EXECUTAR CHECKPOINT` now invokes that deep-evidence API directly;
- no BIOS/HLE, MMIO device, GPU, CD-ROM, DMA, timer, SPU or GTE behavior was added by M3C.

Synthetic RED→GREEN coverage proves the one-million-instruction policy with a looping PS1 fixture, bounded sixteen-sample trace capture, trace report serialization and no mutation of the prepared installation. GitHub Actions run `34411091433` on code-head `b099cc5bee6b0ab14301daa966a45b2d9409a6c2` passed both `Portable core / Linux` and `Windows x64 / MSVC 2022`, including the Windows executable upload.

## Truth boundary

The following are still **not implemented or not verified** at this point:

- commercial JoJo execution beyond the currently captured bounded checkpoints;
- PlayStation BIOS/HLE services required by the commercial game;
- GTE execution semantics beyond the explicit COP2 boundary;
- IRQ/timer device behavior required by JoJo;
- DMA device behavior required by JoJo;
- CD-ROM runtime/streaming behavior required by JoJo;
- GPU command processing, VRAM rendering and frame presentation from commercial JoJo execution;
- SPU audio;
- original-game controller integration;
- MIPS CFG/IR production execution;
- Windows x64 native code generation for R3000A;
- commercial boot;
- rendering, audio, input, gameplay or playability.

Synthetic fixtures and CI prove only the repository contracts they exercise. The first local commercial report proves only the bounded execution fields recorded above; it does not prove that the commercial game boots or is playable.

## Commercial-image evidence still required

The user's previously observed whole-image fingerprint is known, and a first bounded checkpoint has now been produced locally from the user's legally obtained image. The next required evidence is the deeper M3C report that either identifies the first actual JoJo BIOS/CPU/device boundary or supplies enough bounded trace evidence to diagnose a repeated execution loop.

No commercial game bytes, extracted PS-X EXE, proprietary BIOS, raw sectors, or unrestricted guest-memory dumps are to be committed to the repository or CI. Commercial checkpoint evidence must remain bounded and derived.

## Production workstreams

Canonical status remains in `docs/architecture/PRODUCTION-READINESS.tsv`.

- R2.1: repository truth/release gates — verified.
- R2.2: commercial revision enablement — blocked on the canonical local commercial-image discovery evidence required by that gate.
- R2.3: JoJo-specific reference execution checkpoint — `implemented-unverified`; M3C deep evidence capture is available, but the first JoJo BIOS/device boundary is not yet identified.
- R2.4: real gameplay integration — not started.
- R2.5: host-side online/rollback infrastructure exists but is not integrated with the commercial JoJo PS1 game.
- R2.6: production validation/release — not started.

## Next priority

Run the M3C Windows executable against the already prepared supported JoJo installation and click `EXECUTAR CHECKPOINT`. Inspect only the bounded derived report at `%LOCALAPPDATA%/JOJO Recompiled/diagnostics/m3a-checkpoint.txt`.

If the deeper report identifies an A0/B0/C0 BIOS selector, CPU boundary or MMIO/device address, reproduce exactly that boundary with the smallest synthetic RED→GREEN contract and implement only the JoJo-required service. If it again ends by instruction-budget exhaustion, use the sixteen bounded `trace_*` samples to distinguish forward initialization from a repeated execution loop before increasing the budget again.

Keep MIPS CFG/IR and Windows x64 lowering downstream of visible boot. This project has no compatibility target for unrelated games.

Historical Dreamcast/SH-4 plans/specs under `docs/superpowers/` remain project history only.
