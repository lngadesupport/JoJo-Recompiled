# JOJO Recompiled — PlayStation 1 Architecture Migration Design

**Date:** 2026-09-09  
**Status:** Design approved in chat; awaiting written-spec review  
**Branch:** `design/jojo-ps1-architecture-migration`  
**Base:** `main` at `f114a74c3cbf51496d1baaf5d7a4ba2744df953a`

## 1. Purpose

Correct JOJO Recompiled at the architectural root.

The active codebase currently treats the supported user image as Dreamcast software and routes recognized media through Dreamcast/IP.BIN, SH-4, Maple/PVR2, and a Dreamcast-specific native backend. The user's actual game image is PlayStation 1 software. The existing Dreamcast path must therefore stop being part of the active product rather than being patched around.

The new product is intentionally **exclusive to this JoJo PlayStation 1 game/revision family**. It is not a general-purpose PlayStation emulator and must not spend schedule on compatibility with unrelated games.

The end goal remains a native Windows x64 product, `JOJO-Recompiled.exe`, using a user-supplied legally obtained game image and containing no commercial game data in the repository, CI, or release artifacts.

## 2. Binding Product Decisions

The following decisions are approved and binding for implementation:

1. The active platform is **Sony PlayStation 1**.
2. The active CPU architecture is **R3000A / MIPS I**, with PlayStation-specific behavior where required.
3. Dreamcast/SH-4 code is removed from the active build, active test suite, and current product documentation. Git history preserves it.
4. The runtime is **JoJo-only**, not a generic PS1 compatibility layer.
5. The architecture uses a **hybrid recompilation model**:
   - a deterministic R3000A reference executor is the semantic authority;
   - analyzed guest blocks lower through PS1-specific IR to Windows x64 native code;
   - explicit, counted fallback is permitted during development but may not be hidden.
6. No proprietary PlayStation BIOS file is required. BIOS behavior is implemented as explicit HLE only for services actually required by this game.
7. The user chooses the installation directory. `%LOCALAPPDATA%\JOJO Recompiled\game` may be suggested but is never mandatory.
8. Game files extracted or derived from the user's image may be persisted **only in the user's chosen local installation**. They never enter source control, CI, releases, or downloadable project artifacts.
9. The original source image is read-only.
10. Every readiness state must describe a fact actually demonstrated by tests or commercial-image evidence. No synthetic fixture can prove commercial gameplay readiness.

## 3. Non-Goals

This migration does **not** aim to:

- emulate arbitrary PlayStation 1 games;
- provide generic BIOS compatibility;
- require or distribute a Sony BIOS;
- preserve Dreamcast support in the active target;
- preserve SH-4 code merely because it already exists;
- claim boot, graphics, audio, input, or gameplay support before evidence exists;
- upload the user's BIN/CUE, PS-X EXE, music, textures, executable bytes, or extracted game assets to GitHub;
- use PCSX-Redux, DuckStation, Mednafen, or another emulator as the shipping runtime;
- silently ignore unsupported hardware accesses or guest instructions;
- make the conversion destination fixed to `%LOCALAPPDATA%`.

## 4. Existing Code: Preserve vs Replace

### 4.1 Preserve when console-independent

The following areas are expected to remain useful, subject to dependency cleanup:

- Windows single-executable application shell;
- source image picker and drag/drop UX;
- source fingerprint/revision infrastructure;
- `disc_media` logical-sector abstraction;
- CUE parsing;
- raw 2352-byte BIN handling;
- MODE1/2352 and MODE2/2352 user-data extraction;
- ISO9660 traversal and file reads;
- result/error infrastructure;
- atomic file replacement patterns;
- configuration storage;
- Windows controller adapter where host-side semantics remain useful;
- presentation/windowing infrastructure where not Dreamcast-specific;
- SHA/hash utilities;
- CI, MSVC/CMake foundation, production-content checks;
- networking/mod/training systems only where they are genuinely independent of the removed guest architecture.

`disc_media` is particularly valuable because it already models a logical 2048-byte data-sector view over cooked and raw media, including raw 2352 layouts and CUE-selected data tracks.

### 4.2 Replace, not rename

The current native backend is not architecturally generic. It directly depends on Dreamcast executable memory, SH-4 IR, SH-4 reference state, and `DreamcastBootProgram`. It must not be renamed to appear PS1-compatible.

Replace or structurally refactor the following active architecture:

- `dreamcast_*` core components;
- SH-4 decoder/CFG/IR/reference executor;
- Dreamcast interrupt/system ASIC/PVR2/Maple components;
- Dreamcast boot and boot analysis;
- Dreamcast-specific `game_backend` preparation;
- SH-4 native runtime/cache serialization formats;
- Dreamcast/SH-4 CTest targets and workflow contracts;
- current documentation that presents SH-4 as the active production core.

The old implementation remains recoverable from Git history.

## 5. Target End-to-End Data Flow

```text
User-selected BIN/CUE
        |
        v
Source fingerprint + supported-revision gate
        |
        v
PS1 logical data-track discovery
        |
        v
ISO9660
        |
        v
SYSTEM.CNF
        |
        v
BOOT path resolution
        |
        v
PS-X EXE validation + metadata extraction
        |
        +----------------------------+
        |                            |
        v                            v
Local game-data install        R3000A code analysis
                                     |
                                     v
                               PS1 CFG / IR
                                  /      \
                                 v        v
                     Reference executor   x64 native lowering
                                 \        /
                                  v      v
                                JoJo PS1 runtime
                                     |
                                     v
                 BIOS/HLE + RAM/MMIO + GTE/GPU/SPU/CD/DMA/timers/input
```

The commercial image is required only as a local conversion source unless future observed behavior demonstrates that continuous source-media access is necessary. The preferred product path is to install the required user-owned data locally so later launches do not require locating the original image again.

## 6. First Commercial-Evidence Milestone

The first corrected milestone is **PS1 Foundation + JoJo Executable Discovery**.

For the user's observed supported image, success means all of the following occurred using the real local source:

1. source fingerprint recognized;
2. PS1 data track opened with the correct physical-sector layout;
3. ISO9660 opened successfully;
4. `SYSTEM.CNF` found and parsed;
5. `BOOT` target resolved without guessing;
6. boot executable read from the image;
7. `PS-X EXE` signature/header validated;
8. entry point extracted;
9. load address extracted;
10. initial GP extracted from the executable header;
11. text size extracted and validated against the file payload;
12. executable-derived hash calculated;
13. required local data copied/extracted transactionally to the chosen install root;
14. v2 manifest persisted with only proven fields;
15. no boot/gameplay/native-codegen claim made.

This milestone is not `native-codegen-ready`, not `boot-reached`, and not `gameplay-verified`.

## 7. PS1 Media and Executable Discovery

### 7.1 Media layouts

The PS1 path must preserve support for at least the layouts already safely represented by the media layer:

- CUE + BIN;
- raw BIN MODE1/2352;
- raw BIN MODE2/2352;
- cooked 2048 ISO when valid.

GDI is Dreamcast-specific and must not be part of the JoJo production path after migration. Its parser may survive only as unreachable historical/generic code during an intermediate branch; it must not be included in the final active JoJo media acceptance contract.

### 7.2 `SYSTEM.CNF`

Implement a strict parser for the boot declaration used by the supported game. Parsing must tolerate normal whitespace/case conventions but must reject malformed or ambiguous boot declarations.

Expected semantic form:

```text
BOOT = cdrom:\\<EXECUTABLE>;1
```

The implementation must normalize the path only as required by ISO9660/PS1 naming rules. It must not guess a boot executable when `SYSTEM.CNF` is missing or invalid.

### 7.3 PS-X EXE

A dedicated PS-X EXE reader owns:

- signature validation;
- entry PC;
- initial GP;
- text load address;
- text size;
- stack base/size metadata from the header;
- safe payload bounds checks;
- derived hash over the complete PS-X EXE file used by the installation.

Commercial executable bytes may be copied to the local installation if runtime requirements justify it, but never into repository fixtures.

## 8. User-Selectable Installation Root

### 8.1 UX

On first conversion the Windows UI shows:

- selected source image;
- proposed installation directory;
- `Alterar pasta...` action;
- explicit conversion/install action.

The suggested default is:

```text
%LOCALAPPDATA%\JOJO Recompiled\game
```

but conversion must work with another writable local path selected by the user.

### 8.2 Persistent location pointer

Global application settings remain under `%LOCALAPPDATA%\JOJO Recompiled`, because they are application metadata rather than installed commercial game data.

A global settings record stores the selected installation root, e.g.:

```ini
install_root=C:\Games\JOJO Recompiled
```

If the directory is later unavailable, startup must not silently create a replacement installation. Offer an explicit recovery path:

- `Localizar instalação existente`;
- `Alterar pasta de instalação`;
- `Reconverter jogo`.

### 8.3 Installation layout

The selected root is a stable container. The active generation is selected by a small atomically replaced pointer record:

```text
<install_root>\
    active_install.ini
    generations\
        <generation-id>\
            game_manifest.ini
            data\
            cache\
                ps1\
                native\
            logs\
                conversion.log
                runtime.log
            saves\
```

`active_install.ini` contains only non-proprietary local metadata needed to select the current generation, including the generation identifier and manifest-relative path. The referenced generation is immutable after activation except for explicitly mutable subtrees such as saves and runtime logs.

Only directories actually needed by the current milestone are materialized.

## 9. Local Commercial Data Policy

The project distinguishes distribution data from user-local installed data.

### Distribution boundary

Must contain no commercial content:

- Git repository;
- pull requests;
- CI fixtures;
- GitHub Actions caches/artifacts;
- releases;
- downloadable Windows artifact.

The intended application artifact remains only:

```text
JOJO-Recompiled.exe
```

### User-local installation

May contain files copied, extracted, transformed, or cached from the user's own source image when required by the runtime.

The converter must:

- open source media read-only;
- never modify the source BIN/CUE;
- never upload extracted data;
- keep proprietary data under the chosen install root;
- make clear that deleting the local installation removes those generated/extracted files.

## 10. Transactional Conversion and Re-Conversion

Conversion creates a new immutable generation instead of mutating the currently active generation.

Required sequence:

1. validate destination semantics and permissions;
2. query free space when the OS/filesystem exposes it reliably and reject a conversion known to be too large;
3. create a unique inactive generation directory;
4. analyze source;
5. extract/copy required user-local data;
6. generate derived metadata/cache for the current milestone;
7. validate generation contents;
8. write that generation's final `game_manifest.ini` last;
9. write `active_install.ini.tmp` pointing to the fully validated generation;
10. atomically replace `active_install.ini` with the temporary pointer record;
11. only after activation, prune obsolete **known-generated** generations according to retention policy.

The pointer record is the commit point. Runtime accepts only the generation named by a valid `active_install.ini` and then independently validates the referenced manifest/content metadata required by the current runtime.

If conversion fails before step 10, the previous pointer and previous active generation remain untouched.

A failed conversion must never leave a manifest or active pointer claiming a readiness state that was not reached.

## 11. Legacy Dreamcast Installation Handling

Any existing v1 installation created by the incorrect Dreamcast/SH-4 architecture is incompatible with the PS1 runtime.

Rules:

- v1 Dreamcast installation cannot boot through the new runtime;
- Dreamcast cache cannot be trusted or migrated as PS1 native cache;
- the UI explicitly identifies the installation as legacy/incompatible;
- automatic silent conversion to v2 is forbidden;
- user may choose `Reconverter nesta pasta`;
- before destructive cleanup, copy any existing v1 manifest and JOJO-generated conversion log into the new generation's `migration/` directory when those files exist;
- only files positively identified by the legacy schema/path allowlist as JOJO-generated may be removed automatically;
- unknown user files must not be deleted.

Diagnostic preservation layout:

```text
migration\
    legacy-manifest-v1.ini
    legacy-conversion.log
```

Missing legacy diagnostics do not block reconversion.

## 12. Manifest v2

`manifest_version=2` is a clean PS1 contract. It does not pretend the Dreamcast manifest schema is still semantically valid.

### 12.1 Required M1 fields

Every successfully activated M1 generation contains these keys:

```ini
manifest_version=2
platform=playstation
game_id=jojo-ps1
revision_id=<verified-revision-id>
source_format=<observed-format>
source_size=<decimal>
source_hash_fnv1a64=<16-lowercase-hex>

system_cnf_path=<resolved-iso-path>
boot_executable=<resolved-iso-path>
psx_exe_hash_fnv1a64=<16-lowercase-hex>
psx_exe_entry=0x<8-lowercase-hex>
psx_exe_load_address=0x<8-lowercase-hex>
psx_exe_initial_gp=0x<8-lowercase-hex>
psx_exe_text_size=<decimal>
psx_exe_stack_base=0x<8-lowercase-hex>
psx_exe_stack_size=<decimal>

media_status=verified
executable_status=verified
mips_analysis_status=pending
reference_runtime_status=pending
native_codegen_status=pending
hardware_runtime_status=pending
boot_status=pending
rendering_status=pending
audio_status=pending
input_status=pending
gameplay_status=pending
```

Zero-valued PS-X EXE header fields are serialized as zero, not omitted. A successful M1 manifest therefore has a stable parseable shape.

Later milestones may add version-2 keys, but they may not redefine the meaning of these M1 keys. A schema change that changes existing meaning requires a new manifest version.

### 12.2 Parsing and validation guarantees

- a field representing observed commercial data is populated only after it was actually observed;
- numeric parsing is strict and overflow-safe;
- hexadecimal fields require their canonical width/format;
- advanced status fields cannot be inferred from source recognition alone;
- current runtime validates all metadata it relies on rather than trusting a status string;
- status promotion is monotonic inside one immutable generation;
- a new conversion begins in a new inactive generation, so the currently active generation never moves backward while preparation is incomplete.

The install root is not serialized as authoritative state inside `game_manifest.ini`; the manifest's resolved physical location under the generation selected by `active_install.ini` is authoritative.

## 13. Truthful Readiness Model

Use specific readiness concepts rather than one overloaded `native-ready` flag.

Canonical semantic states:

- `source-recognized`;
- `ps1-filesystem-ready`;
- `psx-exe-identified`;
- `mips-analysis-ready`;
- `reference-execution-ready`;
- `native-codegen-ready`;
- `hardware-runtime-partial`;
- `boot-reached`;
- `rendering-verified`;
- `audio-verified`;
- `input-verified`;
- `gameplay-verified`.

These are semantic gates. Manifest v2 represents them through the explicit status fields defined above and later additive fields when necessary.

### Evidence rules

`source-recognized`
: Exact supported-revision identification succeeded.

`ps1-filesystem-ready`
: PS1 data track and ISO9660 were parsed successfully.

`psx-exe-identified`
: `SYSTEM.CNF` boot target resolved and a valid PS-X EXE header was read.

`mips-analysis-ready`
: Required executable region(s) were analyzed into a validated MIPS control-flow representation without guessing unsupported instructions as valid.

`reference-execution-ready`
: Reference R3000A executor passes the required synthetic semantic contracts and can initialize the supported installed executable state.

`native-codegen-ready`
: Native code/cache for the current exact analyzed program was generated or reused, reloaded, ABI/hash validated, and its fallback/native coverage recorded.

`hardware-runtime-partial`
: Runtime has some real PS1 hardware services but no implication of complete boot.

`boot-reached`
: Real commercial execution was observed reaching the agreed game-code boot checkpoint.

`rendering-verified`
: Real commercial runtime produced an accepted game frame/output checkpoint.

`audio-verified`
: Real commercial runtime produced accepted game audio behavior.

`input-verified`
: Real commercial runtime demonstrated accepted controller-to-game behavior.

`gameplay-verified`
: Agreed real gameplay scenario completed successfully under the native product.

Synthetic fixtures cannot establish `boot-reached`, `rendering-verified`, `audio-verified`, `input-verified`, or `gameplay-verified`.

## 14. R3000A Reference Executor

A deterministic reference executor is the semantic oracle for the native backend during development.

It must model behavior required by the real JoJo code, including as observed:

- 32 general-purpose registers with `$zero` invariance;
- PC/next-PC semantics;
- branch delay slots;
- jump delay slots;
- load delay behavior;
- HI/LO;
- signed arithmetic overflow where architecturally trapping;
- address-alignment exceptions;
- MIPS I integer operations required by the title;
- COP0 behavior required by exception/interrupt handling;
- COP2/GTE boundary;
- memory access through the PS1 bus abstraction rather than host pointers.

Unsupported instructions must fail with a diagnostic containing guest PC and instruction word. They may not execute as NOP unless the architecture explicitly defines that behavior and a test proves it.

## 15. PS1 Memory and Bus Foundation

The active runtime must model only the PS1 hardware behavior required by this game while keeping correct architectural boundaries.

Initial required memory domains include:

- 2 MiB main RAM;
- RAM mirrors relevant to observed addressing;
- scratchpad;
- executable load placement;
- MMIO dispatch;
- interrupt registers;
- DMA registers;
- timers;
- GPU registers;
- CD-ROM registers;
- SPU registers;
- controller/SIO path as required.

Unknown MMIO must not return fabricated success silently.

Diagnostics identify at least:

- access type;
- address;
- width;
- value for writes;
- guest PC when known.

## 16. MIPS CFG and PS1 IR

The new CFG/IR is designed around R3000A semantics, not adapted by renaming SH-4 types.

CFG responsibilities include:

- fixed 32-bit MIPS instruction decoding;
- branches with architectural delay slots;
- direct jumps;
- JAL/JR/JALR call/return patterns;
- block termination around exceptions/hardware boundaries;
- conservative handling of indirect control flow;
- no speculative discovery presented as proven executable coverage.

PS1 IR must represent, at minimum as needed:

- integer arithmetic/logical operations;
- comparisons;
- shifts;
- HI/LO operations;
- loads/stores with delay semantics represented correctly;
- control-flow plus delay-slot semantics;
- COP0 operations;
- GTE/COP2 boundary operations;
- explicit calls/boundaries to BIOS/HLE or MMIO helpers when native inline lowering is not appropriate.

## 17. Windows x64 Native Backend

The shipping execution target is Windows x64.

Requirements:

- C++20/CMake/Visual Studio 2022;
- host ABI explicitly versioned;
- native code pages follow writable-then-executable safety rather than permanently RWX memory;
- persistent cache is derived from exact executable/program identity and backend ABI;
- cache reload verifies magic/version/ABI/program hash/block metadata;
- native and fallback block counts are explicit;
- fallback use is observable in diagnostics/metrics;
- runtime does not silently rebuild a supposedly verified cache during bootstrap unless the designed conversion/update flow explicitly owns that rebuild;
- native backend never treats old SH-4 cache as compatible.

Reference fallback is a development correctness mechanism, not proof that recompilation is complete.

## 18. BIOS HLE

No proprietary BIOS is required or distributed.

Implement only services required by observed JoJo execution. HLE scope includes only calls demonstrated necessary by the commercial execution trace or by the supported startup path, such as:

- A0/B0/C0 kernel/library calls;
- memory helpers;
- event/interrupt services;
- controller services;
- CD services;
- libc-like services exposed by the BIOS interface;
- exception entry/return behavior that the game relies on.

Every unsupported HLE call generates a stable diagnostic including call vector/function identifier and guest PC.

HLE must not be described as a complete PlayStation BIOS implementation.

## 19. GTE / COP2

Implement GTE only according to instructions and state actually used by the JoJo runtime.

The design preserves:

- GTE data/control register model;
- command decoding;
- required saturation/flag behavior;
- required fixed-point arithmetic behavior;
- deterministic reference tests for every supported command.

Unknown command or register behavior is a hard diagnostic during development, not a silent approximation.

## 20. GPU

GPU support is JoJo-driven rather than generic compatibility work.

Expected boundaries, when observed, include:

- GP0 packet decoding;
- GP1 control commands;
- VRAM representation;
- drawing area and offsets;
- CLUT/texture-page state;
- primitives actually emitted by the title;
- transfer commands required by the title;
- display/vblank state required by the runtime;
- GPU DMA integration.

Initial rendering may use a correctness-first software/reference representation behind a clean GPU command/state boundary if that materially accelerates validation. The production presentation path may later accelerate rendering without changing guest-visible semantics.

`rendering-verified` requires real commercial evidence, not synthetic GPU unit tests.

## 21. CD-ROM, DMA, Timers, Interrupts

These components are implemented based on runtime evidence from the title.

CD-ROM responsibilities, when observed, include:

- command/status protocol used by the game;
- sector reads from installed data representation;
- XA/CD-audio path if observed;
- interrupt behavior;
- timing sufficient for this game rather than arbitrary-title accuracy.

DMA responsibilities cover channels actually used by the title, especially GPU/CD/SPU where observed.

Timers and interrupt controller implement the behavior needed to progress the game correctly. Unsupported channels/modes generate diagnostics.

## 22. SPU / Audio

SPU support is driven by the title's real usage.

Features are implemented only when observed or required by the verified startup/gameplay path, including as applicable:

- voice registers;
- ADPCM decode;
- key on/off;
- volume/pitch;
- transfer/DMA behavior;
- IRQ behavior;
- reverb;
- XA/CD audio.

Host audio output is a Windows-native presentation concern. Guest SPU state remains deterministic enough to test independently from the host audio device.

## 23. Input

Input exposes PlayStation-side semantics needed by JoJo while reusing the existing Windows controller acquisition layer where sensible.

Requirements are evidence-based:

- pad/controller protocol or HLE path actually used;
- digital/analog mappings only as required by product design;
- deterministic mapping tests;
- real in-game verification before `input-verified`.

Existing host input code may be preserved only if decoupled from Dreamcast/Maple assumptions.

## 24. Diagnostics and Unsupported-Behavior Policy

The project's development loop depends on honest diagnostics from the real game.

Stable categories include:

- `source_error`;
- `filesystem_error`;
- `system_cnf_error`;
- `psx_exe_error`;
- `unsupported_mips`;
- `unsupported_cop0`;
- `unsupported_gte`;
- `unsupported_bios_hle`;
- `unsupported_gpu`;
- `unsupported_spu`;
- `unsupported_cdrom`;
- `unsupported_dma`;
- `unsupported_timer`;
- `unsupported_mmio`;
- `native_codegen_error`;
- `runtime_error`.

When meaningful, diagnostics record:

- stage;
- guest PC;
- opcode/instruction word;
- MMIO address;
- access width;
- write value;
- command ID;
- expected/observed metadata.

The UI may summarize the error, but the persistent log preserves the detailed technical record.

## 25. Test and Evidence Strategy

### Synthetic TDD / CI

Synthetic fixtures prove isolated implementation contracts such as:

- CUE/raw-sector parsing;
- PS1 ISO9660 path handling;
- `SYSTEM.CNF` parsing;
- PS-X EXE header parsing;
- MIPS decode;
- delay slots;
- load delay;
- exceptions;
- memory mirrors;
- MMIO dispatch;
- COP0;
- GTE commands;
- IR lowering;
- x64 ABI/cache serialization;
- BIOS/HLE calls;
- GPU command handling;
- SPU decode/state;
- CD/DMA/timer behavior.

No proprietary byte sequence from the commercial game is committed as a fixture.

### Commercial-image evidence

The user's local supported image is used only on the user's machine to establish game-specific progress.

Typical loop:

```text
synthetic RED
→ implementation GREEN
→ Linux/Windows CI
→ Windows artifact
→ user-local real-image run
→ derived manifest/log
→ next concrete unsupported behavior
```

Only derived metadata/logging needed for debugging should be shared back. The project must never require the commercial image to be committed or uploaded to GitHub.

## 26. UI Truthfulness

The product must not show a generic `Pronto` while only media analysis succeeded.

Examples of truthful user-facing progress:

```text
Imagem reconhecida
Sistema de arquivos PS1 validado
Executável PS-X EXE identificado
Análise MIPS ainda não concluída
```

Later:

```text
R3000A de referência validado
Codegen x64 validado para o programa analisado
Hardware PS1 ainda parcial
Primeiro boot comercial ainda não confirmado
```

Only after corresponding evidence may the UI say that boot, rendering, audio, input, or gameplay were verified.

## 27. Migration Milestones

### M0 — Architectural correction and active-tree cleanup

- create PS1 architecture migration line;
- remove Dreamcast/SH-4 from active build and active product documentation;
- retire Dreamcast-specific readiness contracts;
- preserve genuinely generic infrastructure;
- introduce selectable install root UX/config semantics;
- establish v2 PS1 manifest foundation;
- ensure legacy v1 cannot be treated as current-ready.

### M1 — PS1 media + JoJo executable discovery

- supported source gate;
- PS1 data track;
- ISO9660;
- `SYSTEM.CNF`;
- PS-X EXE;
- local extraction/install transaction;
- real-image derived manifest;
- no execution claim.

### M2 — R3000A reference core

- CPU state;
- required MIPS I semantics;
- branch/load delay;
- HI/LO;
- exceptions/COP0 boundary;
- deterministic tests.

### M3 — PS1 memory/runtime foundation

- 2 MiB RAM and mirrors;
- scratchpad;
- MMIO bus;
- executable load;
- unsupported-access diagnostics.

### M4 — MIPS CFG + PS1 IR

- block discovery;
- delay-slot-aware control flow;
- PS1-specific IR;
- reference execution of IR or equivalent semantic cross-check.

### M5 — Windows x64 native backend

- IR lowering;
- native cache;
- ABI/hash verification;
- fallback accounting;
- `native-codegen-ready` only after reload validation.

### M6 — BIOS HLE JoJo-only

- only observed required services;
- deterministic tests;
- unsupported-call diagnostics.

### M7 — GTE + GPU

- observed GTE instruction set;
- observed GP0/GP1 paths;
- VRAM/state/transfers;
- first real visual checkpoint eventually enables `rendering-verified`.

### M8 — CD-ROM + DMA + timers + interrupts

- observed game-required behavior;
- data streaming/timing/interrupt progress.

### M9 — SPU/audio

- observed SPU behavior;
- host audio output;
- real evidence eventually enables `audio-verified`.

### M10 — Input + first commercial boot

- PS1-side pad semantics;
- commercial startup path;
- real code execution checkpoint;
- `boot-reached` only with evidence.

### M11 — JoJo gameplay completion

- menus;
- character selection;
- fight loop;
- HUD/effects;
- loading;
- input;
- audio;
- save behavior as required;
- timing/stability;
- `gameplay-verified` only after an agreed real gameplay scenario passes.

## 28. Existing Observed Revision

The exact source identity already recorded by the current project is retained as a source-recognition fact:

```text
source_format=bin
source_size=666806112
source_hash_fnv1a64=b8b5dbf79cdb9fcf
revision_id=jojo-usa-observed-b8b5dbf79cdb9fcf
```

This tuple identifies the previously observed source only. It carries **no Dreamcast meaning** and does not by itself prove a valid PS1 filesystem, `SYSTEM.CNF`, PS-X EXE, MIPS analysis, boot, or gameplay.

The corrected migration keeps this exact recognition gate and then independently discovers the PS1 boot metadata from the local source.

If local media evidence demonstrates that the supported source must be opened through an associated CUE/track layout to read the correct PS1 data track, the implementation must treat that as a new explicitly verified source/media contract rather than silently forcing the old BIN-only interpretation.

## 29. Production Readiness Documentation

Current project state/README/roadmap documents that describe SH-4 as implemented production architecture become stale once M0 begins.

M0 must update current documentation to state:

- the previous Dreamcast architecture was invalid for the user's game;
- the active target is PS1/R3000A;
- generic host/product infrastructure may remain implemented;
- commercial boot/gameplay support is not yet established under the corrected architecture;
- previous Dreamcast-native readiness does not transfer to PS1.

Historical design/plan documents remain for audit/history but must be labeled or referenced as historical/non-current anywhere a current architecture index links to them.

## 30. CI and Content Safety

CI remains Linux + Windows/MSVC, with Windows authoritative for the final x64 ABI/runtime.

Production-readiness checks must continue to reject accidental commercial content.

New CI contracts eventually include:

- PS1 media parser tests;
- SYSTEM.CNF tests;
- PS-X EXE tests;
- R3000A semantic tests;
- PS1 memory/bus tests;
- native backend cache tests;
- manifest v2 tests;
- legacy-v1 rejection tests;
- selectable-install-path tests;
- content-safety gates.

Windows artifact upload remains a single executable unless the product design is explicitly changed later.

## 31. Error Recovery UX

Conversion/runtime failure UI exposes enough information to distinguish source problems from implementation gaps.

Suggested presentation fields:

```text
Etapa: <stage>
Código: <stable error code>
Detalhe: <human readable detail>
PC: <guest PC when applicable>
Opcode: <instruction when applicable>
Endereço: <MMIO/memory address when applicable>
```

Actions depend on failure class:

- choose another source;
- choose another installation directory;
- retry conversion;
- locate existing installation;
- open/copy log location;
- reconvert legacy installation.

No recovery action silently marks the install ready.

## 32. Security and Filesystem Constraints

Installation path handling must:

- reject paths that are files when a directory is required;
- avoid path traversal from disc filenames;
- sanitize/contain extracted ISO paths under the generation's data root;
- not follow a source-provided path outside the destination root;
- handle partial/stale inactive generations safely;
- avoid deleting unknown files during legacy migration or reconversion;
- use atomic replacement for `active_install.ini`;
- reject activation if the pointer record or referenced manifest is malformed.

## 33. Architectural Completion Criteria

This architecture migration design is considered implemented only when:

1. active shipping build no longer depends on Dreamcast/SH-4 guest architecture;
2. current documentation identifies PS1/R3000A as the sole game architecture;
3. source recognition no longer enters Dreamcast boot analysis;
4. user can choose an install directory;
5. legacy Dreamcast v1 installs cannot be mistaken for PS1-ready installs;
6. supported local source can reach the PS1 executable-discovery milestone;
7. subsequent readiness stages are represented truthfully and independently;
8. CI contains no commercial game data;
9. Windows release artifact contains no commercial game data;
10. no BIOS file is required.

This criterion does **not** mean M11 gameplay completion is automatically complete. It means the project has been corrected onto the truthful architecture from which JoJo-only recompilation can proceed.

## 34. Implementation Method

After written-spec approval, create a detailed implementation plan with task-sized TDD checkpoints.

Implementation rules:

- TDD RED → GREEN for each task;
- one architectural slice at a time;
- Windows CI remains authoritative for native x64 behavior;
- synthetic fixtures only in repository/CI;
- user-local commercial runs provide external evidence;
- unsupported real behavior becomes the next explicit engineering task;
- no playability claim without direct evidence;
- no large all-at-once rewrite if a sequence of independently testable migration slices can preserve a buildable branch.

## 35. Final Design Statement

JOJO Recompiled becomes a **JoJo-exclusive PlayStation 1 native recompilation/runtime project for Windows x64**.

Its canonical path is:

```text
user-owned PS1 BIN/CUE
→ verified source identity
→ PS1 media / ISO9660
→ SYSTEM.CNF
→ PS-X EXE
→ R3000A reference semantics
→ PS1 CFG/IR
→ Windows x64 native code
→ JoJo-only PS1 hardware/HLE runtime
→ evidence-based boot/render/audio/input/gameplay milestones
```

Dreamcast/SH-4 leaves the active product. The original media stays read-only. The user chooses where local game data is installed. Commercial bytes remain local to the user's machine. Every readiness claim remains narrower than or equal to the evidence actually obtained.
