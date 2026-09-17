# R3000A Reference Core M2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic PS1 R3000A/MIPS-I reference executor that becomes the semantic oracle for later JoJo CFG/IR/x64 work, without claiming commercial boot or implementing PS1 devices.

**Architecture:** A pure decoder maps one 32-bit MIPS instruction word to a typed operation. A one-step reference executor mutates explicit R3000A state, routes every guest memory access through an abstract bus, and returns a structured step result separating architectural exceptions from implementation boundaries. Delay slots, delayed GPR loads, COP0 state, interrupt admission and COP2/GTE boundaries are explicit state transitions.

**Tech Stack:** C++20, CMake 3.20+, CTest, MSVC 2022 x64, GCC/Clang-compatible portable core, existing `jojo_core` static library.

**Spec:** `docs/superpowers/specs/2026-09-09-r3000a-reference-core-m2-design.md`

## Global Constraints

- Active guest platform remains Sony PlayStation 1 only.
- Scope remains this JoJo PS1 title/revision family, not a general PlayStation emulator.
- No commercial image, PS-X EXE, BIOS, extracted asset or derived commercial byte enters source, tests, CI or release artifacts.
- Guest arithmetic and addresses are exactly 32-bit; host signed-overflow behavior may not define guest semantics.
- Executor code never dereferences guest addresses as host pointers; all traffic crosses `R3000aBus`.
- `$zero` is forced to zero at every public step boundary.
- Reserved encodings raise architectural RI; recognized operations outside M2 return a structured implementation boundary.
- A control transfer encountered in an already active delay slot returns `unpredictable_delay_slot_control_transfer`.
- COP2/GTE arithmetic stays out of scope. Disabled COP2 raises CpU; enabled COP2 returns `cop2_unimplemented`.
- Synthetic tests prove CPU semantics only; they never promote boot/render/audio/input/gameplay claims.
- M2 completion requires Linux and Windows/MSVC CI plus the PS1 architecture gate on the exact final head.

## Files and Locked Test Targets

Create core files:

- `src/core/mips_decoder.h/.cpp`
- `src/core/r3000a_state.h`
- `src/core/r3000a_bus.h`
- `src/core/r3000a_diagnostics.h`
- `src/core/r3000a_reference_executor.h/.cpp`

Create test support and tests:

- `tests/mips_test_encode.h`
- `tests/r3000a_test_bus.h`
- `tests/test_mips_decoder.cpp`
- `tests/test_r3000a_integer.cpp`
- `tests/test_r3000a_exceptions.cpp`
- `tests/test_r3000a_hilo.cpp`
- `tests/test_r3000a_control_flow.cpp`
- `tests/test_r3000a_memory.cpp`
- `tests/test_r3000a_unaligned.cpp`
- `tests/test_r3000a_cop0.cpp`
- `tests/test_r3000a_boundary.cpp`
- `tests/test_r3000a_init.cpp`

Register these exact CMake tests as they appear:

```cmake
add_jojo_test(jojo_mips_decoder_tests tests/test_mips_decoder.cpp)
add_jojo_test(jojo_r3000a_integer_tests tests/test_r3000a_integer.cpp)
add_jojo_test(jojo_r3000a_exception_tests tests/test_r3000a_exceptions.cpp)
add_jojo_test(jojo_r3000a_hilo_tests tests/test_r3000a_hilo.cpp)
add_jojo_test(jojo_r3000a_control_flow_tests tests/test_r3000a_control_flow.cpp)
add_jojo_test(jojo_r3000a_memory_tests tests/test_r3000a_memory.cpp)
add_jojo_test(jojo_r3000a_unaligned_tests tests/test_r3000a_unaligned.cpp)
add_jojo_test(jojo_r3000a_cop0_tests tests/test_r3000a_cop0.cpp)
add_jojo_test(jojo_r3000a_boundary_tests tests/test_r3000a_boundary.cpp)
add_jojo_test(jojo_r3000a_init_tests tests/test_r3000a_init.cpp)
```

Modify status docs only in Task 10:

- `PROJECT-STATE.md`
- `docs/NEXT-MILESTONES.md`
- `docs/architecture/PRODUCTION-READINESS.tsv`

---

### Task 1: Pure MIPS-I Decoder

**Files:** Create decoder files/test; modify `CMakeLists.txt`.

**Interfaces:**

```cpp
enum class MipsOp : std::uint16_t {
    reserved,
    sll,srl,sra,sllv,srlv,srav,
    add,addu,sub,subu,bit_and,bit_or,bit_xor,bit_nor,slt,sltu,
    addi,addiu,andi,ori,xori,lui,slti,sltiu,
    mfhi,mthi,mflo,mtlo,mult,multu,div,divu,
    j,jal,jr,jalr,beq,bne,blez,bgtz,bltz,bgez,bltzal,bgezal,
    lb,lbu,lh,lhu,lw,sb,sh,sw,lwl,lwr,swl,swr,
    syscall,break_,mfc0,mtc0,rfe,mfc2,cfc2,mtc2,ctc2,cop2_command
};
struct MipsInstruction {
    std::uint32_t raw{}; MipsOp op{MipsOp::reserved};
    std::uint8_t rs{},rt{},rd{},sa{}; std::uint16_t immediate{}; std::uint32_t target{};
};
[[nodiscard]] MipsInstruction decode_mips(std::uint32_t raw) noexcept;
[[nodiscard]] bool is_control_transfer(MipsOp op) noexcept;
```

- [ ] **Step 1: Write RED decoder cases**

```cpp
CHECK(jojo::decode_mips(0x012A4021u).op==jojo::MipsOp::addu);
CHECK(jojo::decode_mips(0x2528FFF0u).op==jojo::MipsOp::addiu);
CHECK(jojo::decode_mips(0x0C004000u).op==jojo::MipsOp::jal);
CHECK(jojo::decode_mips(0x04100000u).op==jojo::MipsOp::bltzal);
CHECK(jojo::decode_mips(0x42000010u).op==jojo::MipsOp::rfe);
CHECK(jojo::decode_mips(0x48000000u).op==jojo::MipsOp::mfc2);
CHECK(jojo::decode_mips(0x4A000000u).op==jojo::MipsOp::cop2_command);
CHECK(jojo::decode_mips(0x70000000u).op==jojo::MipsOp::reserved);
```

- [ ] **Step 2: Verify RED**

```bash
cmake -S . -B build
cmake --build build --target jojo_mips_decoder_tests -j2
```

Expected: compile failure because decoder API is absent.

- [ ] **Step 3: Implement decoder**

Extract fields once:

```cpp
MipsInstruction out{raw,MipsOp::reserved,
 static_cast<std::uint8_t>((raw>>21)&31u),static_cast<std::uint8_t>((raw>>16)&31u),
 static_cast<std::uint8_t>((raw>>11)&31u),static_cast<std::uint8_t>((raw>>6)&31u),
 static_cast<std::uint16_t>(raw&0xffffu),raw&0x03ffffffu};
```

Dispatch SPECIAL, REGIMM, J/I opcodes, memory opcodes, COP0 and COP2. Every unmatched encoding stays `reserved`.

- [ ] **Step 4: Verify GREEN and commit**

```bash
cmake --build build --target jojo_mips_decoder_tests -j2
ctest --test-dir build -R jojo_mips_decoder_tests --output-on-failure
git add CMakeLists.txt src/core/mips_decoder.* tests/test_mips_decoder.cpp
git commit -m "feat: add R3000A MIPS decoder"
```

---

### Task 2: State, Bus, Diagnostics and Non-Trapping Integer Core

**Files:** Create `r3000a_state.h`, `r3000a_bus.h`, `r3000a_diagnostics.h`, executor files, test helpers and integer test; modify CMake.

**Interfaces:**

```cpp
struct R3000aDelayedLoad { bool valid{}; std::uint8_t reg{}; std::uint32_t value{}; };
struct R3000aDelaySlot { bool active{}; std::uint32_t branch_pc{}; bool taken{}; std::uint32_t target{}; };
struct R3000aCop0 { std::uint32_t target_address{},bad_vaddr{},status{},cause{},epc{}; };
struct R3000aState {
    std::array<std::uint32_t,32> gpr{}; std::uint32_t hi{},lo{},pc{},next_pc{};
    R3000aDelayedLoad pending_load{}; R3000aDelaySlot delay_slot{}; R3000aCop0 cop0{};
    std::uint8_t external_interrupt_pending{};
};
enum class R3000aBusStatus : std::uint8_t { ok,bus_error,unsupported };
struct R3000aBusResult { R3000aBusStatus status{R3000aBusStatus::ok}; std::uint32_t value{}; };
class R3000aBus { public: virtual ~R3000aBus()=default;
 virtual R3000aBusResult read8(std::uint32_t) noexcept=0; virtual R3000aBusResult read16(std::uint32_t) noexcept=0;
 virtual R3000aBusResult read32(std::uint32_t) noexcept=0; virtual R3000aBusResult write8(std::uint32_t,std::uint8_t) noexcept=0;
 virtual R3000aBusResult write16(std::uint32_t,std::uint16_t) noexcept=0; virtual R3000aBusResult write32(std::uint32_t,std::uint32_t) noexcept=0; };
```

Diagnostics:

```cpp
enum class R3000aExceptionCode : std::uint8_t { interrupt=0,adel=4,ades=5,ibe=6,dbe=7,syscall=8,breakpoint=9,reserved_instruction=10,coprocessor_unusable=11,overflow=12 };
enum class R3000aBoundaryCode : std::uint8_t { none,unsupported_address_space,architectural_operation_unimplemented,cop2_unimplemented,unpredictable_delay_slot_control_transfer };
enum class R3000aStage : std::uint8_t { interrupt,fetch,decode,execute,memory,cop0,cop2 };
enum class R3000aStepStatus : std::uint8_t { retired,exception,boundary };
struct R3000aDiagnostic { R3000aBoundaryCode boundary{R3000aBoundaryCode::none}; R3000aStage stage{R3000aStage::execute}; std::uint32_t pc{};
 std::optional<std::uint32_t> opcode,address,write_value; std::optional<std::uint8_t> access_width,coprocessor,register_index; std::optional<R3000aExceptionCode> exception_code; };
struct R3000aStepResult { R3000aStepStatus status{R3000aStepStatus::retired}; R3000aDiagnostic diagnostic{}; };
[[nodiscard]] R3000aStepResult step_r3000a(R3000aState&,R3000aBus&) noexcept;
```

- [ ] **Step 1: Write RED integer tests and synthetic bus**

Use a sparse little-endian test bus and R/I/J encoder helpers. Minimum first case:

```cpp
s.pc=0x1000u; s.next_pc=0x1004u; s.gpr[1]=7u; s.gpr[2]=9u; s.gpr[0]=0xDEADBEEFu;
bus.store32(0x1000u,test_mips::r(1,2,3,0,0x21));
auto r=jojo::step_r3000a(s,bus);
CHECK(r.status==jojo::R3000aStepStatus::retired);
CHECK(s.gpr[3]==16u && s.gpr[0]==0u);
CHECK(s.pc==0x1004u && s.next_pc==0x1008u);
```

Add shifts, variable-shift low-five-bit masking, ADDU/SUBU, logical ops, SLT/SLTU, ADDIU, ANDI/ORI/XORI, LUI, SLTI and SLTIU with signed-immediate extension.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build --target jojo_r3000a_integer_tests -j2
```

- [ ] **Step 3: Implement non-trapping core**

Fetch through `read32(state.pc)`, decode, capture source operands before writes, advance ordinary PC, and force `$zero=0`. Use `std::bit_cast<std::int32_t>` for signed views. ADD/ADDI/SUB return `architectural_operation_unimplemented` until Task 3.

- [ ] **Step 4: Verify GREEN and commit**

```bash
ctest --test-dir build -R jojo_r3000a_integer_tests --output-on-failure
git add CMakeLists.txt src/core/r3000a_* tests/mips_test_encode.h tests/r3000a_test_bus.h tests/test_r3000a_integer.cpp
git commit -m "feat: add R3000A integer reference core"
```

---

### Task 3: Precise Exceptions and Trapping ALU

**Files:** Modify executor; create exception test; modify CMake.

- [ ] **Step 1: Write RED exception cases**

```cpp
s.pc=0x1000u; s.next_pc=0x1004u; s.gpr[1]=0x7fffffffu; s.gpr[2]=1u;
bus.store32(0x1000u,test_mips::r(1,2,3,0,0x20));
auto r=jojo::step_r3000a(s,bus);
CHECK(r.status==jojo::R3000aStepStatus::exception);
CHECK(r.diagnostic.exception_code==jojo::R3000aExceptionCode::overflow);
CHECK(s.gpr[3]==0u && s.cop0.epc==0x1000u);
CHECK(s.pc==0x80000080u);
```

Add ADDI/SUB overflow, SYSCALL, BREAK, RI, misaligned fetch -> AdEL+BadVAddr, fetch bus error -> IBE, BEV=1 -> `0xBFC00180`.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build --target jojo_r3000a_exception_tests -j2
ctest --test-dir build -R jojo_r3000a_exception_tests --output-on-failure
```

- [ ] **Step 3: Implement exception entry**

```cpp
R3000aStepResult enter_exception(R3000aState&,R3000aExceptionCode,std::uint32_t fault_pc,
 std::optional<std::uint32_t> bad_vaddr,std::optional<std::uint8_t> coprocessor) noexcept;
```

Clear old ExcCode/CE/BT/BD, set new ExcCode and CE, update BadVAddr only for AdEL/AdES, push low status stack by `(low<<2)&0x3f`, and select general vector from BEV. Detect signed overflow via unsigned sign-bit predicates.

- [ ] **Step 4: Verify GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(integer|exception)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_exceptions.cpp
git commit -m "feat: add R3000A exception entry"
```

---

### Task 4: HI/LO Multiply and Divide

**Files:** Modify executor; create HI/LO test; modify CMake.

- [ ] **Step 1: Write RED edge table**

```cpp
const DivCase cases[]={{7u,0u,7u,0xffffffffu,false},{7u,0u,7u,0xffffffffu,true},{0xfffffff9u,0u,0xfffffff9u,1u,true},{0x80000000u,0xffffffffu,0u,0x80000000u,true}};
```

Also test MFHI/MFLO/MTHI/MTLO and normal MULT/MULTU/DIV/DIVU.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build --target jojo_r3000a_hilo_tests -j2
ctest --test-dir build -R jojo_r3000a_hilo_tests --output-on-failure
```

- [ ] **Step 3: Implement safely**

Use 64-bit products. Handle divisor-zero and signed minimum/-1 before host division. Do not model cycle stalls.

- [ ] **Step 4: Verify GREEN and commit**

```bash
ctest --test-dir build -R jojo_r3000a_hilo_tests --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_hilo.cpp
git commit -m "feat: add R3000A HI LO semantics"
```

---

### Task 5: Branches, Jumps and Delay Slots

**Files:** Modify executor and exception test; create control-flow test; modify CMake.

- [ ] **Step 1: Write RED delay-slot tests**

```cpp
bus.store32(0x1000u,test_mips::i(0x04,1,1,2));
bus.store32(0x1004u,test_mips::i(0x09,2,2,1));
jojo::step_r3000a(s,bus); CHECK(s.pc==0x1004u && s.next_pc==0x100Cu);
jojo::step_r3000a(s,bus); CHECK(s.pc==0x100Cu);
```

Cover taken/not-taken branches, J/JAL/JR/JALR, link=`branch_pc+8`, JALR `rd==rs` pre-write target, unconditional link for BLTZAL/BGEZAL, boundary for control transfer inside a delay slot, and delay-slot exception BD/BT/EPC/TAR.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build --target jojo_r3000a_control_flow_tests jojo_r3000a_exception_tests -j2
ctest --test-dir build -R "jojo_r3000a_(control_flow|exception)_tests" --output-on-failure
```

- [ ] **Step 3: Implement scheduling**

At entry copy `current_delay=state.delay_slot`. New branch stores `{true,instruction_pc,taken,target}`, sets `pc=instruction_pc+4`, and sets `next_pc` to target or `instruction_pc+8`. After the delay-slot instruction retires, clear delay state. Exception entry consumes `current_delay` to set BD/BT/EPC/TAR.

- [ ] **Step 4: Verify GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(control_flow|exception|integer)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_control_flow.cpp tests/test_r3000a_exceptions.cpp
git commit -m "feat: model R3000A delay slots"
```

---

### Task 6: Aligned Memory and One-Instruction Load Delay

**Files:** Modify executor/test bus; create memory test; modify CMake.

- [ ] **Step 1: Write RED memory/load-delay cases**

```cpp
s.gpr[4]=0x2000u; s.gpr[8]=0x11111111u; bus.store32(0x2000u,0xAABBCCDDu);
bus.store32(0x1000u,test_mips::i(0x23,4,8,0));
bus.store32(0x1004u,test_mips::r(8,0,9,0,0x21));
bus.store32(0x1008u,test_mips::r(8,0,10,0,0x21));
jojo::step_r3000a(s,bus); jojo::step_r3000a(s,bus); CHECK(s.gpr[9]==0x11111111u);
jojo::step_r3000a(s,bus); CHECK(s.gpr[10]==0xAABBCCDDu);
```

Add same-destination overwrite, load-to-zero fault behavior, LB/LBU/LH/LHU/LW/SB/SH/SW, AdEL/AdES, DBE, unsupported-address boundary, and pending-load retirement before following exception entry.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build --target jojo_r3000a_memory_tests -j2
ctest --test-dir build -R jojo_r3000a_memory_tests --output-on-failure
```

- [ ] **Step 3: Implement retirement order**

Capture operands first, then retire prior pending load, then apply current direct GPR write, then install current new pending load, then force `$zero=0`. Faulting current loads schedule nothing. Alignment checks precede bus calls.

- [ ] **Step 4: Verify GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(memory|exception|integer)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/r3000a_test_bus.h tests/test_r3000a_memory.cpp
git commit -m "feat: add R3000A memory load delay"
```

---

### Task 7: LWL/LWR/SWL/SWR

**Files:** Modify executor; create unaligned test; modify CMake.

- [ ] **Step 1: Write exact RED lane tables**

For memory `0x44332211` and GPR `0xAABBCCDD`:

```cpp
const std::uint32_t lwl[4]={0x11BBCCDDu,0x2211CCDDu,0x332211DDu,0x44332211u};
const std::uint32_t lwr[4]={0x44332211u,0xAA443322u,0xAABB4433u,0xAABBCC44u};
const std::uint32_t swl[4]={0x443322AAu,0x4433AABBu,0x44AABBCCu,0xAABBCCDDu};
const std::uint32_t swr[4]={0xAABBCCDDu,0xBBCCDD11u,0xCCDD2211u,0xDD332211u};
```

Also prove back-to-back LWL/LWR to the same `rt` forwards the first pending value only to the second merge; ordinary ALU consumers still observe the stale visible GPR until retirement.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build --target jojo_r3000a_unaligned_tests -j2
ctest --test-dir build -R jojo_r3000a_unaligned_tests --output-on-failure
```

- [ ] **Step 3: Implement explicit per-lane switches**

Use `aligned=effective&~3u`; no host-endian casts. For merge loads, base value is captured `rt` unless an existing pending load targets the same register, in which case use the pending value. SWL/SWR read aligned word, merge lanes, write one aligned word.

- [ ] **Step 4: Verify GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(unaligned|memory)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_unaligned.cpp
git commit -m "feat: add R3000A unaligned word merges"
```

---

### Task 8: COP0, RFE and Interrupt Admission

**Files:** Modify executor/exception test; create COP0 test; modify CMake.

**COP0 classification:**

- M2-readable: r6 TAR, r8 BadVAddr, r12 Status, r13 Cause, r14 EPC.
- M2-writable: r12 Status through `0xF27FFF3F`; r13 Cause only through `0x00000300`.
- MTC0 to r6/r8/r14: `architectural_operation_unimplemented` boundary, state unchanged.
- r0/r1/r2/r4/r10: architectural RI.
- Existing PS1 debug/identity/garbage-register families outside M2 (r3/r5/r7/r9/r11/r15/r16-r31): `architectural_operation_unimplemented` boundary rather than RI.

- [ ] **Step 1: Write RED COP0/RFE tests**

```cpp
constexpr std::uint32_t kStatusWritableMask=0xF27FFF3Fu;
constexpr std::uint32_t kCauseSwMask=0x00000300u;
```

Test MFC0 one-instruction load delay, Status mask, Cause software bits only, read-only-register boundaries, RI set above, and RFE low-stack restore:

```cpp
const auto low=s.cop0.status&0x3fu;
const auto expected=(low&0x30u)|((low>>2)&0x0fu);
```

- [ ] **Step 2: Write RED interrupt tests**

`external_interrupt_pending` uses only bits2..7 and maps to Cause.IP10..15:

```cpp
s.cop0.cause=(s.cop0.cause&~0x0000FC00u)|(std::uint32_t(s.external_interrupt_pending&0xFCu)<<8);
```

Accept interrupt only when IEc is set, a masked pending bit exists, and no delay slot is active. Prove branch delay slot cannot be split. Prove older pending GPR load retires before handler entry.

- [ ] **Step 3: Verify RED**

```bash
cmake --build build --target jojo_r3000a_cop0_tests -j2
ctest --test-dir build -R "jojo_r3000a_(cop0|exception)_tests" --output-on-failure
```

- [ ] **Step 4: Implement COP0/RFE/interrupt logic**

RFE updates only low six stack bits. MFC0 uses delayed GPR path. Before fetch, synchronize external pending bits while preserving Cause bits8-9. Interrupt predicate is `IEc && ((Cause & Status & 0x0000FF00u)!=0) && !delay_slot.active`.

- [ ] **Step 5: Verify GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(cop0|control_flow|memory|exception)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.cpp tests/test_r3000a_cop0.cpp tests/test_r3000a_exceptions.cpp
git commit -m "feat: add R3000A COP0 interrupts"
```

---

### Task 9: COP2 Boundary and PS-X EXE Initialization

**Files:** Modify executor header/source; create boundary/init tests; modify CMake.

**Interfaces:**

```cpp
[[nodiscard]] R3000aStepResult step_r3000a(R3000aState&,R3000aBus&) noexcept;
[[nodiscard]] R3000aState initialize_r3000a_for_psx_exe(const Ps1ExeMetadata&) noexcept;
```

- [ ] **Step 1: Write RED COP2 tests**

With Status.CU2 bit30 clear, MFC2/CFC2/MTC2/CTC2/COP2 command raise CpU with CE=2. With CU2 set, each returns `cop2_unimplemented`; none retire as NOP.

- [ ] **Step 2: Write RED initializer tests**

```cpp
jojo::Ps1ExeMetadata m{}; m.entry_pc=0x80010000u; m.initial_gp=0x80018000u; m.stack_base=0x801FFF00u; m.stack_size=0x100u;
auto s=jojo::initialize_r3000a_for_psx_exe(m);
CHECK(s.pc==0x80010000u && s.next_pc==0x80010004u);
CHECK(s.gpr[28]==0x80018000u && s.gpr[29]==0x80200000u);
CHECK(s.gpr[0]==0u && !s.pending_load.valid && !s.delay_slot.active);
```

Zero stack fields leave SP zero. HI/LO/COP0 initialize deterministically to zero.

- [ ] **Step 3: Verify RED**

```bash
cmake --build build --target jojo_r3000a_boundary_tests jojo_r3000a_init_tests -j2
ctest --test-dir build -R "jojo_r3000a_(boundary|init)_tests" --output-on-failure
```

- [ ] **Step 4: Implement gate and initializer**

CU2 clear -> architectural CpU; CU2 set -> explicit boundary. Initializer sets PC/next-PC/GP/SP and no BIOS/HLE or commercial-memory loading.

- [ ] **Step 5: Verify GREEN and commit**

```bash
ctest --test-dir build -R "jojo_r3000a_(boundary|init|cop0)_tests" --output-on-failure
git add CMakeLists.txt src/core/r3000a_reference_executor.* tests/test_r3000a_boundary.cpp tests/test_r3000a_init.cpp
git commit -m "feat: add R3000A COP2 boundary init"
```

---

### Task 10: Determinism, CI and Truthful R2.3 Status

**Files:** Modify init test and status docs; verify architecture gate/workflow.

- [ ] **Step 1: Add deterministic replay test**

Run the same synthetic sequence twice from cloned CPU/bus state. Sequence includes delayed load, ALU consumer, branch+delay slot, HI/LO and non-faulting COP0 access. Compare GPRs, HI/LO, PC/next-PC, COP0 fields, pending-load and delay-slot state.

- [ ] **Step 2: Verify locally and commit the deterministic test before CI evidence**

```bash
cmake -S . -B build
cmake --build build -j2
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckPs1ActiveArchitecture.cmake
ctest --test-dir build --output-on-failure
git add tests/test_r3000a_init.cpp
git commit -m "test: verify deterministic R3000A replay"
```

- [ ] **Step 3: Push the code head and require both CI jobs**

Require `Portable core / Linux` and `Windows x64 / MSVC 2022` to complete `success`, including CTest and PS1 architecture gate. Record the numeric GitHub Actions run ID returned by that successful code-head workflow; do not edit readiness docs before the number exists.

- [ ] **Step 4: Update existing R2.3 only**

In `docs/architecture/PRODUCTION-READINESS.tsv`, replace the current R2.3 line with `implemented-unverified`, set evidence to `github-actions:run-` immediately followed by the recorded numeric code-run ID, and set blocker exactly to:

```text
ps1-memory-bus-bios-hle-not-implemented
```

Do not add a parallel M2 row. Keep R2.4 `not-started`.

`PROJECT-STATE.md` must state:

```text
R3000A reference CPU semantics are implemented and verified by synthetic Linux/Windows contracts.
Commercial JoJo boot, rendering, audio, input and gameplay are not verified.
```

`docs/NEXT-MILESTONES.md` must name JoJo-specific PS1 memory/bus + BIOS/HLE as next; CFG/IR/x64 remains downstream of the reference executor.

- [ ] **Step 5: Commit evidence docs**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "docs: verify R3000A reference core M2"
```

- [ ] **Step 6: Push final head and require CI again**

The documentation-only head must again pass Linux, Windows, CTest and PS1 architecture gate. The R2.3 evidence remains the prior code-head run because that run is the one that established CPU evidence before the documentation commit.

- [ ] **Step 7: Final completion checklist**

```text
MIPS decoder green
integer/$zero/immediate green
precise exceptions/overflow/RI/bus faults green
HI/LO edge cases green
branch/jump/delay-slot green
aligned memory/load delay green
LWL/LWR/SWL/SWR green
COP0/RFE/interrupt green
COP2 boundary green
PS-X EXE initializer green
deterministic replay green
PS1 architecture gate green
Linux final CI green
Windows final CI green
R2.3 only implemented-unverified
no boot/render/audio/input/gameplay promotion
```

---

## Completion Boundary

M2 is complete only when all ten tasks are implemented and the exact final head is green on Linux and Windows. `reference-execution-ready` means the R3000A reference CPU semantic contract is ready to act as an oracle; it does not mean the JoJo commercial executable boots. The next independent design/spec cycle is JoJo-specific PS1 memory/bus + BIOS/HLE.