# JOJO Recompiled — PS1 Visible Boot M3 Design

Date: 2026-09-09
Status: design approved in chat; written-spec review pending
Base: `feature/r3000a-reference-core-m2` at `38a8f3e756dd4510c02dd49233ba9227f4690bc2`
Design branch: `design/ps1-visible-boot-m3`
Scope: **JoJo PS1 only**

## 1. Goal

M3 moves JOJO Recompiled from the verified synthetic R3000A reference core to the first **visible frame produced by the supported commercial JoJo program** on Windows.

The selected strategy is execution-first:

1. use the M2 R3000A reference executor as the temporary production CPU;
2. load the verified JoJo PS-X EXE into a real PS1 memory/bus model;
3. implement only the BIOS/HLE and hardware behavior that this JoJo actually requires on the path to a visible frame;
4. use structured boundaries and derived local logs to identify each next missing behavior;
5. defer CFG/IR and native Windows x64 code generation until after visible boot, using the reference executor as the semantic oracle.

A black frame, synthetic test pattern, fixed clear color, or host-generated image is not a successful visible boot. The success frame must originate from commands emitted by the commercial JoJo code.

## 2. JoJo-only product boundary

This project is **not** a general PlayStation emulator, general PlayStation recompiler, compatibility layer, or multi-game runtime.

There is no requirement to:

- boot any PlayStation game other than the supported JoJo title/revision family;
- preserve behavior solely for compatibility with unrelated games;
- build a game-compatibility database or compatibility matrix;
- add fallback emulation paths for arbitrary titles;
- implement complete PS1 hardware when JoJo does not require it;
- generalize a JoJo-specific solution merely to make another title work.

PS1 architectural behavior is implemented only where it is required for correct JoJo execution or where it forms a necessary invariant of a component JoJo uses. A clean internal interface is still required for maintainability and testing, but cross-game compatibility is explicitly outside the product goal.

If behavior is not required by the supported JoJo path, it remains unimplemented unless it is a prerequisite for the correctness of behavior that JoJo does require.

## 3. Chosen architecture

The runtime data flow is:

```text
validated active JoJo PS1 installation
        |
        v
verified boot.psxexe
        |
        v
Ps1ExecutableLoader
        |
        v
Ps1MemoryBus <----> JoJo-required BIOS/HLE + devices
        |
        v
R3000a reference executor
        |
        v
Ps1BootRuntime
        |
        +--> structured boundaries / Ps1BootReport
        |
        +--> GPU VRAM/display state
                  |
                  v
         existing Win32/D3D11 host
```

The R3000A executor continues to access guest memory only through `R3000aBus`. PS1 address decoding, memory storage and device behavior stay outside the CPU core.

### Rejected alternative: implement CFG/IR/x64 first

Native code generation before a verified boot machine model would optimize an unverified semantic target. CFG/IR and x64 therefore remain downstream of M3.

### Rejected alternative: implement complete PS1 hardware first

M3 is driven by the supported JoJo revision. Building unobserved PS1 functionality would enlarge the debugging surface without advancing this product goal.

## 4. M3 decomposition

M3 is a visible-boot umbrella divided into sequential, independently testable submilestones:

- **M3A — memory + PS-X EXE loader + boot loop**
- **M3B — JoJo-observed BIOS/HLE**
- **M3C — JoJo-required IRQ + timers + DMA**
- **M3D — installed media + JoJo-required CD-ROM behavior**
- **M3E — JoJo-required GPU + VRAM + Windows presentation + first commercial frame**

The submilestones must remain separate RED→GREEN units. Each successful stage exposes the next real boundary.

If JoJo requires COP2/GTE before the first visible frame, a narrowly scoped **M3G** may be inserted for exactly the observed operations. This does not authorize a complete GTE implementation.

## 5. Component ownership

### `Ps1MemoryBus`

A concrete implementation of `R3000aBus` that:

- owns or references 2 MiB main RAM and 1 KiB scratchpad storage;
- normalizes the supported R3000A virtual aliases to PS1 physical addresses;
- routes MMIO to explicit device objects;
- provides little-endian 8/16/32-bit accesses;
- records the precise unsupported access instead of fabricating a value;
- never treats a guest address as a host pointer.

### `Ps1ExecutableLoader`

A focused loader that:

- consumes an already validated `Ps1Executable`;
- copies the PS-X EXE payload into mapped main RAM;
- initializes R3000A state using the M2 initializer;
- rejects overflow, truncation and unmapped destination ranges.

### `Ps1BootRuntime`

The JoJo boot coordinator that:

- owns the instruction-budget loop;
- intercepts BIOS/HLE entries before an ordinary CPU fetch when applicable;
- advances deterministic device state after retired work;
- maps CPU/bus/device boundaries into stable stop reasons;
- accumulates a `Ps1BootReport`;
- never treats budget exhaustion as successful boot.

### `Ps1BootReport`

A derived diagnostic record. It may contain addresses, register values, command IDs, counters, hashes and bounded summaries required for debugging. It must never contain full commercial executable payloads, unrestricted guest-memory dumps, full raw sectors, or proprietary BIOS bytes.

## 6. M3A — memory, loader and boot loop

### 6.1 Address normalization

For KUSEG/KSEG0/KSEG1 addresses used by JoJo, the bus first resolves the guest virtual address to the corresponding PS1 physical address. KSEG0 and KSEG1 normalization uses the architectural physical-address mask rather than host pointer arithmetic.

The initial main-RAM contract is 2 MiB of physical storage:

```text
physical 0x00000000..0x001FFFFF -> 2 MiB main RAM
```

At minimum these guest aliases address the same storage:

```text
0x00000000..0x001FFFFF
0x80000000..0x801FFFFF
0xA0000000..0xA01FFFFF
```

If JoJo touches an architectural RAM mirror outside this initial alias set, that exact mirror is added from evidence and tested before execution continues.

The PS1 scratchpad is mapped at physical:

```text
0x1F800000..0x1F8003FF
```

with KSEG aliases resolving to the same scratchpad where applicable.

Recognized MMIO ranges route to devices. Unknown or not-yet-implemented regions return a structured unsupported result; reads do not silently return zero and writes do not silently succeed.

CPU alignment exceptions remain owned by the M2 executor. The bus itself remains deterministic for the access width requested.

### 6.2 PS-X EXE loader

The loader copies exactly:

```text
source offset = 0x800
byte count    = metadata.text_size
destination   = metadata.text_load_address
```

The destination may use a supported main-RAM alias, but the entire translated payload must fit writable physical main RAM without wrapping.

The initialized R3000A state is:

```text
PC      = entry_pc
next_PC = entry_pc + 4
$gp     = initial_gp
$sp     = stack_base + stack_size only when both stack fields are non-zero
HI/LO   = 0
pending load = empty
delay slot   = empty
COP0         = deterministic M2 initial state
```

The loader rejects:

- a file smaller than the validated header/payload contract;
- `0x800 + text_size` overflow or truncation;
- 32-bit guest destination overflow;
- destination bytes outside writable main RAM;
- any silent wrap or partial copy.

### 6.3 Initial boot loop

Conceptually:

```text
validate active JoJo installation
parse and revalidate installed boot.psxexe
construct PS1 memory/bus and device shell
load PS-X EXE payload
initialize R3000A
repeat:
    if PC is an HLE entry, dispatch HLE
    else step R3000A once
    advance deterministic device state for retired work
    update derived boot report
until explicit stop reason or instruction budget
```

Every run has an instruction budget. Reaching it produces `execution_budget_exhausted`.

### 6.4 M3A completion gate

Synthetic Linux and Windows tests must prove:

- main-RAM alias coherence;
- scratchpad isolation and aliasing;
- correct little-endian bus reads/writes;
- exact PS-X EXE payload placement;
- PC/GP/SP initialization;
- execution of a synthetic MIPS program directly from `Ps1MemoryBus` RAM;
- precise stop on an unsupported BIOS/MMIO/device boundary;
- deterministic replay for identical synthetic inputs.

M3A does not claim commercial boot.

## 7. M3B — JoJo-observed BIOS/HLE

No proprietary PlayStation BIOS ROM is stored, distributed or required by Git, CI or release artifacts.

### 7.1 Entry interception

The runtime recognizes the BIOS call tables conventionally named A0/B0/C0 by **normalized physical entry**, so the low, KSEG0 and KSEG1 aliases resolve to the same HLE table entry where JoJo uses them.

The physical entries are:

```text
0x000000A0
0x000000B0
0x000000C0
```

The dispatcher uses the R3000A call ABI, including function selector `$t1` (`r9`), argument registers and stack arguments where a specific observed function requires them.

An implemented HLE function applies only its explicit register/memory side effects and resumes via the guest return state. `$zero` remains hard-wired and any interaction with delayed GPR state must preserve M2 ordering.

### 7.2 HLE boundary policy

- observed and implemented function -> execute and continue;
- valid JoJo-observed function not yet implemented -> `bios_call_unimplemented`;
- invalid/unknown selector -> `bios_call_unknown`.

Diagnostics include table, selector, call PC and only the bounded argument values needed for diagnosis.

### 7.3 BIOS environment beyond A0/B0/C0

Direct PS-X EXE start bypasses the proprietary BIOS boot sequence, so JoJo may depend on kernel state normally prepared before executable launch. M3B may initialize or HLE only the specific kernel state proven necessary by JoJo execution.

If an exception reaches a BIOS/kernel vector for which no JoJo-required HLE path exists, execution stops with an explicit boundary. M3B does not invent a generic BIOS kernel.

## 8. M3C — IRQ, timers and DMA

### 8.1 Interrupt controller

Initial routed registers:

```text
I_STAT 0x1F801070
I_MASK 0x1F801074
```

The controller asserts the PS1 external CPU interrupt line when its implemented pending/mask contract requires it. The M2 R3000A field `external_interrupt_pending` bit 2 is used for this hardware line because M2 maps external bits 2..7 into COP0 interrupt-pending bits.

Register semantics are added from PS1 architecture where required to make JoJo's observed accesses correct. Unsupported behavior remains explicit.

### 8.2 Timers

Timer MMIO is routed beginning at:

```text
0x1F801100
```

The scheduler starts deterministic and instruction-retirement based. M3 does not promise cycle accuracy. Timing is refined only when a concrete JoJo execution trace proves the simpler deterministic model cannot progress correctly.

### 8.3 DMA

DMA MMIO is routed beginning at:

```text
0x1F801080
```

Channels are implemented only as JoJo requires them. GPU DMA is expected to be needed; CD-ROM DMA is added if observed. Unused channels remain explicit boundaries.

DMA must respect mapped guest memory and must not silently read or write beyond it.

## 9. M3D — installed media and JoJo-required CD-ROM

### 9.1 Installation-media requirement

The current M1 installation persists `data/SYSTEM.CNF` and `data/boot.psxexe`, which is enough to begin M3A but not enough for post-start CD-ROM sector reads.

M3D therefore adds a **private local installed-media area** to the active JoJo generation. These bytes remain user-owned local runtime data and are never committed to Git, uploaded to CI, embedded in the executable, or shipped in releases.

The preferred format preserves the selected source media rather than extracting only an ISO filesystem tree:

- `.bin`: copy the selected BIN into the generation media area;
- `.iso`: copy the selected ISO;
- `.cue`: copy the CUE plus safely resolved referenced track files needed by the installed source, preserving safe relative relationships.

A sidecar `media/installed_media.ini` records only local structural metadata required to reopen and validate the installed copy: source format, relative filenames, sizes, and hashes. It does not turn the product into a multi-game media database.

Existing M1 generations without `media/installed_media.ini` remain usable for M3A. If JoJo reaches CD-ROM behavior, such a generation stops as `installed_media_missing` and the user reconverts the owned image to create a media-capable generation.

The existing logical-sector source code remains the basis for ISO/BIN/CUE data-track interpretation. M3D extends it only as required by this JoJo's observed CD-ROM path.

### 9.2 CD-ROM controller

Initial MMIO window:

```text
0x1F801800..0x1F801803
```

M3D implements only JoJo-observed command, FIFO, status and interrupt behavior needed before the first visible frame. Sector data comes from the validated installed media.

An unsupported command produces `device_command_unimplemented` with command/index/status context rather than fake success.

## 10. M3E — GPU, VRAM and first commercial frame

### 10.1 GPU ports and VRAM

Initial GPU ports:

```text
0x1F801810 -> GP0 data/command
0x1F801814 -> GP1 control/status
```

M3E adds:

- PS1 VRAM state;
- GP0 packet decoding for commands JoJo emits before the first frame;
- GP1 display/control behavior JoJo emits before the first frame;
- GPU DMA behavior required by those commands;
- VRAM upload/copy/draw operations required by those packets;
- explicit `gpu_command_unimplemented` for unsupported packets.

Unknown GPU commands are never treated as NOPs.

### 10.2 Windows presentation

The existing Win32/D3D11 presentation host is extended rather than replaced by a parallel graphics backend.

The first-frame presenter consumes host-readable pixels derived from the JoJo-driven GPU display state/VRAM and presents them through the existing Windows window path. Filtering, MSAA and 120-FPS optimization are not part of the first-frame correctness gate.

### 10.3 Visible-boot success gate

M3 may be described as `first-visible-boot-verified` only when a local run against the supported JoJo installation demonstrates all of the following:

```text
verified JoJo PS-X EXE loaded
commercial R3000A instructions retired
required JoJo BIOS/HLE calls serviced
required JoJo MMIO/device state advances
GPU receives commands emitted by JoJo
those commands modify VRAM/display state
Windows host presents a non-synthetic frame derived from that state
```

A black frame, fixed clear color, synthetic fixture or host-created image does not satisfy this gate.

## 11. Observability

Each boot attempt produces a `Ps1BootReport`.

Minimum fields:

```text
instructions_retired
last_pc
last_opcode
stop_reason
bios_call_count
bounded recent BIOS call summaries
bounded recent MMIO summaries
interrupts_accepted
dma_transfer_count
bounded recent CD-ROM command summaries
gpu_gp0_command_count
gpu_gp1_command_count
vram_write_count
presented_frames
```

Minimum stop reasons:

```text
execution_budget_exhausted
cpu_boundary
bios_call_unimplemented
bios_call_unknown
mmio_unimplemented
installed_media_missing
device_command_unimplemented
gpu_command_unimplemented
commercial_frame_presented
fatal_runtime_error
```

Commercial diagnostics may contain addresses, widths, register values, command IDs, counts, hashes and other derived metadata. They must not emit full PS-X EXE payloads, full sectors, BIOS ROM contents or unrestricted guest-memory dumps.

## 12. Testing and evidence

### 12.1 Synthetic unit tests

Each submilestone adds focused tests for its own storage, address decoding and device state machine. Fixtures contain no commercial JoJo bytes and no proprietary BIOS bytes.

### 12.2 Synthetic integration programs

Small artificial MIPS programs exercise the R3000A through `Ps1MemoryBus`, including:

- RAM and scratchpad accesses;
- HLE entry/return behavior;
- interrupt delivery;
- DMA transactions;
- synthetic CD-ROM controller sequences;
- synthetic GPU packets and VRAM changes.

These tests verify mechanics only. They are not commercial boot evidence.

### 12.3 Local JoJo checkpoint loop

Commercial execution is a local-only checkpoint:

```text
run supported JoJo installation
    -> stop at exact missing boundary
    -> capture bounded derived diagnostic
    -> add synthetic regression reproducing the required semantic contract
    -> implement only that JoJo-required behavior
    -> pass Linux/Windows CI
    -> rerun JoJo locally
```

Commercial bytes, proprietary BIOS, extracted sectors and unrestricted memory dumps never enter Git or CI.

### 12.4 Platform authority

- Linux is an authority for the portable core and synthetic state-machine tests.
- Windows x64 / MSVC 2022 is also required for the portable core and is the authority for Win32/D3D11 presentation integration.
- Final M3E visible-boot evidence additionally requires a local Windows run with the user's legally obtained supported JoJo image/install.

## 13. Branch and integration strategy

Because M2 was intentionally kept outside `main`, M3 is based on:

```text
feature/r3000a-reference-core-m2
38a8f3e756dd4510c02dd49233ba9227f4690bc2
```

The design branch is `design/ps1-visible-boot-m3`.

Implementation must occur on a separate feature branch created from the approved M3 design/plan head. M3 must not silently fast-forward `main`; integration remains a separate user decision after verification.

Each submilestone uses TDD RED→GREEN and preserves a distinct CI evidence point. A later submilestone cannot retroactively convert an earlier unsupported boundary into fabricated success.

## 14. Explicitly outside M3

Unless a behavior is proven necessary to reach the first JoJo frame, M3 does not include:

- SPU/audio completion;
- controller/input gameplay integration;
- rollback or online integration;
- CFG/IR generation;
- Windows x64 native R3000A code generation;
- 120-FPS performance optimization;
- complete GTE;
- complete PS1 hardware coverage;
- support for, testing of, or compatibility with any game other than this JoJo project.

## 15. Truth boundary and status language

The following claims remain forbidden until separately evidenced:

- `boot verified` before commercial progress proves the actual boot path;
- `rendering verified` before JoJo-driven GPU state produces a real presented frame;
- `audio verified` without SPU/audio evidence;
- `input verified` without original-game controller integration;
- `gameplay verified` without actual gameplay evidence;
- `native recompilation verified` while execution still uses the R3000A reference executor.

Before the M3E gate, the strongest accurate statement is that the JoJo-specific reference runtime and required PS1 subsystems are being implemented/verified synthetically and through bounded local checkpoints.

After the M3E gate, the strongest accurate statement is **first visible boot verified for the supported JoJo revision**, not playable, not audio-complete, not input-complete, and not native-x64-complete.

## 16. Implementation ordering

The implementation plan must preserve this order:

1. M3A memory/address normalization;
2. M3A PS-X EXE loader;
3. M3A deterministic boot loop/reporting;
4. M3B HLE dispatch shell and first observed JoJo BIOS functions;
5. M3C interrupt controller;
6. M3C only the timers/DMA behavior required by current JoJo boundaries;
7. M3D installed-media capability and CD-ROM behavior required by JoJo;
8. M3E GPU/VRAM behavior required by JoJo;
9. M3E Win32/D3D11 frame presentation;
10. final synthetic CI plus local commercial visible-frame evidence.

The plan must stop and create a design amendment if evidence requires a materially new subsystem not covered here. It must not silently broaden the project into general PlayStation compatibility.