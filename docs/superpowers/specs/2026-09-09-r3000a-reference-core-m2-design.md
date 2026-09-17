# JOJO Recompiled — R3000A Reference Core M2 Design

Date: 2026-09-09
Status: design approved in chat; implementation not started
Base: `main` after PS1 M0+M1 integration
Scope: JoJo PS1 only

## 1. Goal

M2 establishes a deterministic R3000A/MIPS-I reference CPU core that becomes the semantic oracle for later JoJo-specific PS1 analysis, IR lowering and Windows x64 native code generation.

M2 does **not** implement the PS1 GPU, SPU, CD-ROM, DMA engine, timers, controller hardware, complete BIOS HLE, game rendering or gameplay. It also does not claim that the commercial game boots.

The reference core exists to answer one question precisely: given an R3000A CPU state, an instruction word and a bus result, what architectural state must exist after the instruction, including delay-slot, load-delay and exception behavior?

## 2. Relationship to M1

M1 already provides the PS1-only media/conversion path:

`BIN/CUE/ISO -> ISO9660 -> SYSTEM.CNF -> PS-X EXE -> manifest v2 -> active installation generation`.

M2 consumes only verified M1 outputs. It must not reopen Dreamcast/SH-4 paths or reinterpret a v1 legacy installation as PS1-compatible.

For an installed PS-X EXE, M2 may initialize a CPU state from verified executable metadata, but executing real commercial code beyond the semantic contracts is not evidence of boot or gameplay.

## 3. Architectural choice

Use a semantic-first reference executor before CFG/IR/x64 work.

The active dependency direction is:

```text
instruction bytes / words
        |
        v
  mips_decoder
        |
        v
r3000a_reference_executor
   |        |        |
   |        |        +--> structured diagnostics
   |        +-----------> R3000A architectural state
   +--------------------> abstract R3000A bus
```

The CPU core never dereferences guest addresses as host pointers. All memory traffic crosses the bus interface.

## 4. Modules and ownership

M2 introduces focused modules with narrow interfaces:

- `src/core/mips_decoder.h/.cpp`
  - decode one 32-bit little-endian MIPS instruction word into a typed instruction representation;
  - preserve the original raw word;
  - expose decoded register/immediate/target fields;
  - never guess an unknown encoding as a supported instruction.

- `src/core/r3000a_state.h`
  - architectural CPU state only;
  - 32 GPRs, HI, LO, PC/next-PC, delayed-load state, COP0 state and exception/delay-slot bookkeeping;
  - no host window/audio/input state.

- `src/core/r3000a_bus.h`
  - abstract guest memory operations;
  - `read8/read16/read32` and `write8/write16/write32` with explicit typed failures;
  - future PS1 bus implementation plugs into this interface.

- `src/core/r3000a_diagnostics.h`
  - stable diagnostic categories and context fields;
  - at minimum guest PC, raw opcode, access address/width/value when applicable, exception code and stage.

- `src/core/r3000a_reference_executor.h/.cpp`
  - one-instruction semantic execution;
  - branch/load pipelines;
  - exception entry;
  - COP0 subset;
  - deterministic interaction with `R3000aBus`.

Tests remain synthetic and contain no commercial game bytes.

## 5. CPU state

The minimum state is conceptually:

```text
GPR[32]
HI
LO
PC
next_PC
pending_load { valid, register_index, value }
delay_slot { active, branch_pc, taken, target }
COP0.Status
COP0.Cause
COP0.EPC
COP0.BadVAddr
COP0.TargetAddress
```

`GPR[0]` is architecturally hard-wired to zero. Any attempted write to register 0 is discarded. The invariant is reasserted at the public executor boundary so a malformed test fixture cannot leave `$zero` non-zero after a step.

The executor uses 32-bit guest arithmetic. Host integer width must not leak into guest overflow, shift, sign-extension or multiplication behavior.

## 6. Program-counter model and delay slots

The executor models `PC` and `next_PC` explicitly.

For an ordinary instruction:

```text
executed instruction address = PC
PC      <- next_PC
next_PC <- next_PC + 4
```

A taken branch or jump changes the future `next_PC`; the instruction already selected at the sequential `PC + 4` executes first as the architectural delay slot.

A not-taken conditional branch still executes its sequential delay-slot instruction.

Direct branch targets use sign-extended immediate displacement shifted left by two and added to `PC + 4` of the branch instruction. `J`/`JAL` targets combine the high four bits of `PC + 4` with the 26-bit target shifted left by two.

`JAL` and `JALR` write the architectural return address `branch_pc + 8`. `BLTZAL` and `BGEZAL` also write `$ra = branch_pc + 8` **whether or not the branch condition is true**. If their source register is `$ra`, the branch comparison uses the pre-link value.

All source operands are captured before destination writes, so `JALR` uses the pre-write jump target even if `rd == rs`. The resulting restart hazard after an exception is preserved rather than normalized away.

A control-transfer instruction encountered **inside an already active delay slot** is outside the supported deterministic M2 contract. The executor returns a structured `unpredictable_delay_slot_control_transfer` boundary diagnostic rather than inventing a second-order branch rule. If JoJo is later observed to rely on such code, that behavior gets a separate evidence-driven design amendment.

## 7. Delayed-load model

M2 models the R3000A one-instruction GPR load delay explicitly.

For:

```asm
LW   $t0, 0($a0)
ADDU $t1, $t0, $zero
ADDU $t2, $t0, $zero
```

the first `ADDU` observes the old `$t0`; the second observes the loaded value.

Normal execution ordering is:

1. capture source operands for the current instruction from the pre-step architectural register view;
2. calculate the current instruction result;
3. retire the previously pending delayed load;
4. apply the current instruction's direct GPR write, if any;
5. schedule a new pending load produced by the current instruction;
6. reassert `$zero == 0`.

If the instruction immediately after a load writes the same GPR directly, that newer direct architectural write wins over the older delayed load.

All GPR-producing memory loads use this delayed path. `MFC0` also uses the same one-instruction delayed GPR write. Future `MFC2/CFC2` support must use the same one-instruction GPR load-delay principle.

### 7.1 `LWL/LWR` forwarding exception

Consecutive `LWL`/`LWR` instructions targeting the same GPR are a required architectural special case. They are allowed to follow one another without an intervening NOP.

When a current `LWL` or `LWR` merges into register `rt` and a pending delayed load already targets that same `rt`, the merge base is the **pending value**, not the stale visible GPR value. The combined result then becomes the new pending value. This permits the normal back-to-back unaligned-load pair to assemble one word correctly while preserving the final one-instruction delay before ordinary consumers may read it.

No general ALU instruction receives this forwarding; it remains specific to the merge-load semantics.

Loads into `$zero` still perform memory access and fault behavior normally but do not create visible or pending register state.

### 7.2 Exceptions and pending loads

If the instruction after a successful load raises an exception, the older pending load retires before the exception handler observes the architectural GPR state. A load instruction that itself faults does not schedule a pending result.

An accepted interrupt between instructions likewise retires any older pending load before exception-handler execution.

## 8. Integer instruction baseline

M2 implements the R3000A integer instructions required as the semantic foundation for JoJo analysis.

### Shifts and register ALU

- `SLL`, `SRL`, `SRA`;
- `SLLV`, `SRLV`, `SRAV`;
- `ADD`, `ADDU`, `SUB`, `SUBU`;
- `AND`, `OR`, `XOR`, `NOR`;
- `SLT`, `SLTU`.

Variable shifts use only the low five bits of the shift-count register.

### Immediate ALU

- `ADDI`, `ADDIU`;
- `ANDI`, `ORI`, `XORI`;
- `LUI`;
- `SLTI`, `SLTIU`.

`SLTIU` sign-extends its 16-bit immediate to 32 bits before performing the unsigned comparison.

### HI/LO

- `MFHI`, `MTHI`, `MFLO`, `MTLO`;
- `MULT`, `MULTU`, `DIV`, `DIVU`.

Multiplication produces the architecturally correct 64-bit product split across HI/LO.

Division does not raise an architectural divide exception. M2 locks the R3000A edge results explicitly:

```text
DIVU rs,0                  -> HI=rs, LO=0xFFFFFFFF
DIV  nonnegative_rs,0      -> HI=rs, LO=0xFFFFFFFF
DIV  negative_rs,0         -> HI=rs, LO=0x00000001
DIV  0x80000000,0xFFFFFFFF -> HI=0,  LO=0x80000000
```

Host-language division by zero or signed-overflow behavior is never invoked to implement these cases.

M2 models architectural values, not multiply/divide cycle timing. Cycle-accurate stalls are deferred unless later JoJo evidence requires them.

### Branch/jump

- `J`, `JAL`, `JR`, `JALR`;
- `BEQ`, `BNE`, `BLEZ`, `BGTZ`;
- `BLTZ`, `BGEZ`, `BLTZAL`, `BGEZAL`.

### Loads/stores

- `LB`, `LBU`, `LH`, `LHU`, `LW`;
- `SB`, `SH`, `SW`;
- `LWL`, `LWR`, `SWL`, `SWR`.

The unaligned merge instructions are included in M2 because deferring them would make the reference oracle incomplete for ordinary R3000A code.

### System/control

- `SYSCALL`;
- `BREAK`;
- COP0 subset described below;
- COP2/GTE decode boundary described below.

A **reserved MIPS encoding** raises the architectural Reserved Instruction (`RI`) exception. A recognized architectural operation that this milestone deliberately leaves unimplemented returns a distinct structured implementation-boundary diagnostic. These two cases must never be conflated.

## 9. Arithmetic overflow

`ADD`, `ADDI` and `SUB` trap on signed two's-complement overflow.

`ADDU`, `ADDIU` and `SUBU` wrap modulo 2^32 and never raise signed-overflow exceptions.

Overflow detection is implemented without relying on host signed-overflow behavior.

When a trapping arithmetic instruction faults, its destination register is not modified and architectural exception entry occurs.

## 10. Memory access and alignment

The CPU core is little-endian and delegates actual address mapping to `R3000aBus`.

Alignment rules:

- instruction fetch requires 4-byte alignment;
- `LH/LHU/SH` require 2-byte alignment;
- `LW/SW` require 4-byte alignment;
- byte accesses require no extra alignment;
- `LWL/LWR/SWL/SWR` align the underlying word access internally and implement architectural byte-lane merge semantics, so the original effective address need not be word-aligned.

Misaligned loads/instruction fetches raise address-error-load/fetch (`AdEL`). Misaligned stores raise address-error-store (`AdES`). `BadVAddr` records the faulting guest address.

The bus interface distinguishes at least:

- successful access;
- architectural bus error;
- unsupported/unimplemented address-space boundary.

An instruction-fetch bus error maps to `IBE`; a data bus error maps to `DBE`. An unsupported future MMIO/device region remains an explicit implementation diagnostic rather than being converted to a fabricated zero read.

M2 defines CPU byte-lane intent for `SB/SH/SWL/SWR`; device-specific PS1 behavior for partial-width MMIO writes belongs to the later PS1 bus/device milestone.

## 11. Exceptions and delay-slot precision

M2 models precise architectural exception entry for:

- external interrupt entry;
- `AdEL`;
- `AdES`;
- `IBE`;
- `DBE`;
- syscall;
- breakpoint;
- reserved instruction (`RI`);
- arithmetic overflow (`Ovf`);
- coprocessor unusable (`CpU`) where applicable.

On exception:

- `Cause.ExcCode` reflects the exception type;
- outside a delay slot, `EPC` is the fault/restart PC;
- in a branch/jump delay slot, `Cause.BD=1` and `EPC` is the branch/jump instruction address;
- when the delay-slot exception belongs to a taken or unconditional transfer, `Cause.BT` and `TargetAddress` preserve the transfer direction/target context required by the PS1 R3000A contract;
- `BadVAddr` changes only for address-error exceptions;
- current interrupt/user mode state is pushed through the R3000A three-level status stack;
- execution transfers to `0x80000080` when `Status.BEV=0` and to `0xBFC00180` when `Status.BEV=1`.

Exception entry pushes the low status stack as:

```text
Old      <- Previous
Previous <- Current
Current  <- kernel mode, interrupts disabled
```

where each level contains the KU/IE pair.

The reference executor tracks branch origin, whether the transfer was taken and the computed target so `BD`, `BT`, `EPC` and `TargetAddress` are deterministic.

## 12. Interrupt recognition

M2 accepts synthetic external interrupt-pending state through the CPU/COP0 boundary; device generation of interrupt lines belongs to the later PS1 runtime milestone.

An interrupt is accepted only when the R3000A `Status/Cause` enable-and-mask conditions allow it.

Interrupts are recognized at legal instruction boundaries. Once a branch or jump has issued and its delay slot is pending, an interrupt may not split the control-transfer instruction from its architectural delay slot; recognition is deferred until the delay slot retires.

If an interrupt is accepted while a prior GPR load is pending, that load retires before the exception handler begins.

M2 does not attempt cycle-level interrupt races with unfinished GTE commands because GTE execution is outside this milestone.

## 13. COP0 subset

M2 implements the subset required for architectural exceptions and the next PS1 runtime milestone:

- `Status`;
- `Cause`;
- `EPC`;
- `BadVAddr`;
- `TargetAddress`;
- `MFC0` for supported registers;
- `MTC0` with register-specific writable-bit masks;
- `RFE`.

`MFC0` schedules a delayed GPR write and therefore has the same one-instruction visibility delay as memory loads.

`RFE` does **not** jump to `EPC`. It only restores status-stack bits:

```text
Current  <- Previous
Previous <- Old
Old      <- unchanged
```

All other status bits remain unchanged except where an explicitly tested writable-mask rule says otherwise.

COP0 accesses to architecturally unavailable PS1 registers and unsupported COP0 commands that are reserved on this CPU produce architectural `RI` behavior where required. They are not silently accepted.

## 14. COP2/GTE boundary

M2 recognizes COP2/GTE instruction classes sufficiently to report them distinctly from unknown MIPS instructions.

It does **not** implement GTE arithmetic in this milestone.

If COP2 is architecturally disabled in `Status`, an attempted COP2 operation raises `CpU` with the coprocessor field set appropriately. If COP2 is enabled but the operation requires GTE functionality outside M2, the executor returns a structured `cop2_unimplemented` boundary result containing at least `pc` and `opcode`.

No COP2 instruction is treated as a NOP.

## 15. Decoder contract

The decoder is pure and side-effect free.

Given one 32-bit word, it returns:

- raw word;
- opcode class;
- `rs`, `rt`, `rd`, shift amount where applicable;
- immediate/target fields in raw form;
- typed operation identifier or reserved-encoding classification.

Sign extension, branch target calculation and architectural effects belong to execution helpers rather than being hidden inside the parser.

Decoder tests cover every implemented operation family plus representative reserved encodings.

## 16. Executor API behavior

The reference executor exposes a deterministic one-step API conceptually equivalent to:

```cpp
Result<R3000aStepResult> step_r3000a(
    R3000aState& state,
    R3000aBus& bus);
```

The actual signature may use the project's existing `Result<T>` and diagnostic conventions, but the semantic boundary is fixed:

- recognize a legal pending interrupt boundary when applicable;
- validate/fetch one instruction through the bus at `state.pc` when no interrupt preempts the step;
- execute exactly one architectural instruction or perform one exception-entry transition;
- retire/schedule delayed state in the order defined above;
- report non-architectural implementation boundaries explicitly.

A separate helper may execute an already-decoded instruction for focused unit tests, but production stepping must exercise instruction fetch through the bus so fetch alignment and fetch-bus-error behavior are covered.

## 17. PS-X EXE initialization

M2 adds a pure initialization helper that consumes verified M1 executable metadata plus an explicit caller-provided initial execution environment.

The helper initializes:

- all GPRs deterministically, then forces `$zero=0`;
- `PC = psx_exe_entry`;
- `next_PC = psx_exe_entry + 4`;
- `$gp`/R28 = `psx_exe_initial_gp`;
- `$sp`/R29 = `stack_base + stack_size` when the verified PS-X EXE stack pair is non-zero;
- HI/LO = deterministic zero values;
- no pending load;
- no active delay slot.

The `stack_size` field keeps the existing M1 project naming but corresponds to the second PS-X EXE initial-stack header word used as the offset added to the stack base.

M2 does **not** invent a proprietary BIOS reset state. Initial COP0/environment values are supplied explicitly by the caller. Synthetic tests use a named deterministic test environment; the later JoJo BIOS/HLE milestone defines the production environment used before real program entry.

The helper does not itself map RAM or copy executable payload bytes; that remains the prepared bus/memory owner's responsibility.

## 18. Diagnostics

Unsupported or failed operations expose structured context rather than prose-only errors.

At minimum, diagnostics can carry:

```text
error_code
stage
pc
opcode
address
access_width
access_type
write_value
exception_code
coprocessor
register_index
```

Fields not relevant to an error are absent/unset rather than filled with fabricated values.

Diagnostic formatting for user logs is separate from the structured semantic result.

## 19. Testing strategy

M2 is developed TDD RED -> GREEN in small instruction families.

Required test groups:

1. decoder field extraction and reserved encodings;
2. `$zero`, register ALU, shifts and immediates;
3. overflow/no-overflow pairs;
4. HI/LO multiply/divide, including all four defined divide edge cases;
5. branches, jumps, link values and the unconditional link behavior of `BLTZAL/BGEZAL`;
6. branch/jump delay slots and explicit rejection of a second control transfer inside a delay slot;
7. delayed loads, direct same-destination overwrite and exception/interrupt retirement behavior;
8. back-to-back `LWL/LWR` same-register forwarding plus final load delay;
9. aligned loads/stores and sign/zero extension;
10. `LWL/LWR/SWL/SWR` byte-lane tables for all low address bits;
11. `AdEL/AdES`, `IBE/DBE` and `BadVAddr` behavior;
12. syscall/break/RI/Ovf exception entry;
13. exception in a branch delay slot, including `Cause.BD`, `Cause.BT`, `EPC` and `TargetAddress`;
14. COP0 `MFC0/MTC0/RFE` delayed-load, masking and status-stack behavior;
15. interrupt masking/entry and delay-slot deferral using a synthetic injected line;
16. COP2 disabled (`CpU`) versus enabled-but-unimplemented GTE boundary;
17. PS-X EXE CPU-state initialization;
18. deterministic replay: identical initial state + bus contents -> identical final state/diagnostics.

Tests use synthetic instruction words and synthetic RAM/bus fixtures only.

Linux and Windows/MSVC CI are both required before M2 is considered complete.

## 20. Readiness and truth boundary

M2 may promote the semantic status `reference-execution-ready` only when:

- the complete M2 synthetic semantic suite is green on Linux and Windows;
- PS-X EXE initialization is validated against synthetic M1 metadata;
- a local validation path can initialize the supported installed executable state without uploading or embedding commercial bytes;
- unsupported CPU/COP0/COP2 boundaries remain explicit and observable;
- no unsupported hardware access is fabricated as successful.

M2 does **not** promote:

- `native-codegen-ready`;
- `hardware-runtime-partial`;
- `boot-reached`;
- `rendering-verified`;
- `audio-verified`;
- `input-verified`;
- `gameplay-verified`.

A synthetic test can prove CPU semantics, not commercial-game compatibility.

## 21. Out of scope

Explicitly deferred:

- cycle-accurate pipeline and multiply/divide timing unless later JoJo evidence requires it;
- PS1 RAM mirrors/scratchpad/MMIO implementation beyond synthetic bus fixtures;
- BIOS A0/B0/C0 HLE services;
- DMA/timers/interrupt-controller devices;
- GPU/GTE arithmetic implementation;
- SPU/audio;
- CD-ROM command/streaming model;
- controller/SIO;
- MIPS CFG discovery;
- PS1 IR;
- Windows x64 code generation/cache;
- self-modifying-code/cache-coherency policy;
- commercial boot/render/audio/input/gameplay evidence.

These belong to later milestones and consume the M2 reference executor as their semantic authority.

## 22. Implementation sequencing constraint

The implementation plan must preserve dependency order:

```text
decoder/state/bus contracts
-> integer ALU
-> HI/LO
-> branches/jumps
-> delay-slot machinery
-> memory + delayed loads + merge-load forwarding
-> exceptions + bus errors
-> COP0/RFE/interrupt hook
-> COP2 boundary
-> PS-X EXE initialization
-> full Linux + Windows verification
```

Individual tasks may be split more finely for TDD, but later layers may not be used to bypass missing earlier semantics.

## 23. Completion definition

M2 is complete only when all planned semantic contracts are implemented, the full project CI is green on Linux and Windows, the active PS1 architecture gate remains green, and documentation records the exact implemented CPU scope without claiming game boot.

The next design cycle after M2 is the JoJo-specific PS1 memory/bus and BIOS/HLE foundation. CFG/IR/native x64 work must use this reference core as the oracle rather than creating an independent second interpretation of R3000A semantics.
