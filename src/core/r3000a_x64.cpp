#include "core/r3000a_x64.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace jojo {
namespace {

static_assert(std::is_standard_layout_v<R3000aState>);
static_assert(std::is_standard_layout_v<R3000aDelaySlot>);

void emit_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void emit_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint32_t gpr_offset(std::uint8_t reg) noexcept {
    return static_cast<std::uint32_t>(
        offsetof(R3000aState, gpr) +
        static_cast<std::size_t>(reg) * sizeof(std::uint32_t));
}

void emit_mov_eax_imm32(
    std::vector<std::uint8_t>& out,
    std::uint32_t value) {
    emit_u8(out, 0xB8u);
    emit_u32(out, value);
}

void emit_load_eax_gpr(
    std::vector<std::uint8_t>& out,
    std::uint8_t reg) {
    if (reg == 0u) {
        emit_mov_eax_imm32(out, 0u);
        return;
    }
    emit_u8(out, 0x8Bu);
    emit_u8(out, 0x81u);
    emit_u32(out, gpr_offset(reg));
}

void emit_load_edx_gpr(
    std::vector<std::uint8_t>& out,
    std::uint8_t reg) {
    if (reg == 0u) {
        emit_u8(out, 0x31u);
        emit_u8(out, 0xD2u);
        return;
    }
    emit_u8(out, 0x8Bu);
    emit_u8(out, 0x91u);
    emit_u32(out, gpr_offset(reg));
}

void emit_store_eax_gpr(
    std::vector<std::uint8_t>& out,
    std::uint8_t reg) {
    if (reg == 0u) return;
    emit_u8(out, 0x89u);
    emit_u8(out, 0x81u);
    emit_u32(out, gpr_offset(reg));
}

void emit_load_eax_state(
    std::vector<std::uint8_t>& out,
    std::uint32_t offset) {
    emit_u8(out, 0x8Bu);
    emit_u8(out, 0x81u);
    emit_u32(out, offset);
}

void emit_store_eax_state(
    std::vector<std::uint8_t>& out,
    std::uint32_t offset) {
    emit_u8(out, 0x89u);
    emit_u8(out, 0x81u);
    emit_u32(out, offset);
}

void emit_store_edx_state(
    std::vector<std::uint8_t>& out,
    std::uint32_t offset) {
    emit_u8(out, 0x89u);
    emit_u8(out, 0x91u);
    emit_u32(out, offset);
}

void emit_variable_shift(
    std::vector<std::uint8_t>& out,
    std::uint8_t shift_modrm) {
    // Preserve the Windows x64 first argument (state pointer in RCX)
    // while CL is used as the variable shift count.
    emit_u8(out, 0x49u); emit_u8(out, 0x89u); emit_u8(out, 0xCBu); // mov r11, rcx
    emit_u8(out, 0x89u); emit_u8(out, 0xD1u);                    // mov ecx, edx
    emit_u8(out, 0xD3u); emit_u8(out, shift_modrm);             // shift eax, cl
    emit_u8(out, 0x4Cu); emit_u8(out, 0x89u); emit_u8(out, 0xD9u); // mov rcx, r11
}

void emit_mov_state_imm32(
    std::vector<std::uint8_t>& out,
    std::uint32_t offset,
    std::uint32_t value) {
    emit_u8(out, 0xC7u);
    emit_u8(out, 0x81u);
    emit_u32(out, offset);
    emit_u32(out, value);
}

void emit_mov_state_imm8(
    std::vector<std::uint8_t>& out,
    std::uint32_t offset,
    std::uint8_t value) {
    emit_u8(out, 0xC6u);
    emit_u8(out, 0x81u);
    emit_u32(out, offset);
    emit_u8(out, value);
}

void emit_store_al_state(
    std::vector<std::uint8_t>& out,
    std::uint32_t offset) {
    emit_u8(out, 0x88u);
    emit_u8(out, 0x81u);
    emit_u32(out, offset);
}

constexpr std::uint32_t delay_active_offset() noexcept {
    return static_cast<std::uint32_t>(
        offsetof(R3000aState, delay_slot) +
        offsetof(R3000aDelaySlot, active));
}

constexpr std::uint32_t delay_branch_pc_offset() noexcept {
    return static_cast<std::uint32_t>(
        offsetof(R3000aState, delay_slot) +
        offsetof(R3000aDelaySlot, branch_pc));
}

constexpr std::uint32_t delay_taken_offset() noexcept {
    return static_cast<std::uint32_t>(
        offsetof(R3000aState, delay_slot) +
        offsetof(R3000aDelaySlot, taken));
}

constexpr std::uint32_t delay_target_offset() noexcept {
    return static_cast<std::uint32_t>(
        offsetof(R3000aState, delay_slot) +
        offsetof(R3000aDelaySlot, target));
}

std::uint32_t sign_extend16(std::uint16_t value) noexcept {
    return (value & 0x8000u) != 0u
        ? (0xFFFF0000u | static_cast<std::uint32_t>(value))
        : static_cast<std::uint32_t>(value);
}

Result<void> materialize_executable(
    R3000aX64Code& code) noexcept {
#if defined(_WIN32) && defined(_M_X64)
    void* memory = VirtualAlloc(
        nullptr,
        code.bytes.size(),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!memory) {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "failed to allocate executable memory for R3000A x64 code");
    }

    std::memcpy(memory, code.bytes.data(), code.bytes.size());
    DWORD old_protection = 0u;
    if (!VirtualProtect(
            memory,
            code.bytes.size(),
            PAGE_EXECUTE_READ,
            &old_protection)) {
        VirtualFree(memory, 0u, MEM_RELEASE);
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "failed to protect R3000A x64 code as executable");
    }
    FlushInstructionCache(
        GetCurrentProcess(),
        memory,
        code.bytes.size());

    code.executable_owner = std::shared_ptr<void>(
        memory,
        [](void* allocation) noexcept {
            if (allocation) {
                VirtualFree(allocation, 0u, MEM_RELEASE);
            }
        });
    code.executable_entry = memory;
#else
    (void)code;
#endif
    return Result<void>::success();
}

bool state_is_safe_for_x64(
    const R3000aX64Code& code,
    const R3000aState& state) noexcept {
    if (code.bytes.empty() || code.instruction_count == 0u) return false;
    if (state.pc != code.entry_pc ||
        state.next_pc != code.entry_pc + 4u) {
        return false;
    }
    if (state.pending_load.valid || state.delay_slot.active) return false;

    const auto cause_with_external =
        (state.cop0.cause & ~0x0000FC00u) |
        (static_cast<std::uint32_t>(
             state.external_interrupt_pending & 0xFCu) << 8u);
    const bool interrupt_enabled = (state.cop0.status & 1u) != 0u;
    return !interrupt_enabled ||
           (cause_with_external & state.cop0.status & 0x0000FF00u) == 0u;
}

} // namespace

bool r3000a_op_is_x64_lowerable(MipsOp op) noexcept {
    switch (op) {
        case MipsOp::sll:
        case MipsOp::srl:
        case MipsOp::sra:
        case MipsOp::sllv:
        case MipsOp::srlv:
        case MipsOp::srav:
        case MipsOp::mfhi:
        case MipsOp::mthi:
        case MipsOp::mflo:
        case MipsOp::mtlo:
        case MipsOp::mult:
        case MipsOp::multu:
        case MipsOp::addu:
        case MipsOp::subu:
        case MipsOp::bit_and:
        case MipsOp::bit_or:
        case MipsOp::bit_xor:
        case MipsOp::bit_nor:
        case MipsOp::slt:
        case MipsOp::sltu:
        case MipsOp::addiu:
        case MipsOp::andi:
        case MipsOp::ori:
        case MipsOp::xori:
        case MipsOp::lui:
        case MipsOp::slti:
        case MipsOp::sltiu:
            return true;
        default:
            return false;
    }
}

bool r3000a_op_is_x64_direct_lowerable(MipsOp op) noexcept {
    if (r3000a_op_is_x64_lowerable(op)) return true;
    switch (op) {
        case MipsOp::j:
        case MipsOp::jal:
        case MipsOp::jr:
        case MipsOp::jalr:
        case MipsOp::beq:
        case MipsOp::bne:
        case MipsOp::blez:
        case MipsOp::bgtz:
        case MipsOp::bltz:
        case MipsOp::bgez:
        case MipsOp::bltzal:
        case MipsOp::bgezal:
            return true;
        default:
            return false;
    }
}

Result<R3000aX64Code> emit_r3000a_x64_instruction(
    std::uint32_t pc,
    const MipsInstruction& instruction) noexcept {
    if (r3000a_op_is_x64_lowerable(instruction.op)) {
        R3000aIrBlock block{};
        block.entry_pc = pc;
        block.instructions.push_back(R3000aIrInstruction{
            pc,
            instruction,
            false,
        });
        block.terminator = R3000aIrTerminatorKind::fallthrough;
        block.fallthrough_target = pc + 4u;
        return emit_r3000a_x64_alu_block(block);
    }

    if (!r3000a_op_is_x64_direct_lowerable(instruction.op)) {
        return Result<R3000aX64Code>::failure(
            ErrorCode::backend_unavailable,
            "R3000A instruction is not lowered by the direct x64 backend");
    }

    R3000aX64Code code{};
    code.entry_pc = pc;
    code.instruction_count = 1u;
    auto& out = code.bytes;
    out.reserve(128u);

    const auto emit_common_delay = [&](std::uint32_t target) {
        emit_mov_state_imm8(out, delay_active_offset(), 1u);
        emit_mov_state_imm32(out, delay_branch_pc_offset(), pc);
        emit_mov_state_imm32(out, delay_target_offset(), target);
        emit_mov_state_imm32(
            out,
            static_cast<std::uint32_t>(offsetof(R3000aState, pc)),
            pc + 4u);
    };

    switch (instruction.op) {
        case MipsOp::j:
        case MipsOp::jal: {
            const auto target =
                ((pc + 4u) & 0xF0000000u) |
                ((instruction.target & 0x03FFFFFFu) << 2u);
            emit_common_delay(target);
            emit_mov_state_imm8(out, delay_taken_offset(), 1u);
            emit_mov_state_imm32(
                out,
                static_cast<std::uint32_t>(offsetof(R3000aState, next_pc)),
                target);
            if (instruction.op == MipsOp::jal) {
                emit_mov_state_imm32(out, gpr_offset(31u), pc + 8u);
            }
            break;
        }

        case MipsOp::jr:
        case MipsOp::jalr: {
            emit_mov_state_imm8(out, delay_active_offset(), 1u);
            emit_mov_state_imm32(out, delay_branch_pc_offset(), pc);
            emit_mov_state_imm8(out, delay_taken_offset(), 1u);
            emit_load_eax_gpr(out, instruction.rs);
            emit_store_eax_state(out, delay_target_offset());
            emit_store_eax_state(
                out,
                static_cast<std::uint32_t>(offsetof(R3000aState, next_pc)));
            emit_mov_state_imm32(
                out,
                static_cast<std::uint32_t>(offsetof(R3000aState, pc)),
                pc + 4u);
            if (instruction.op == MipsOp::jalr) {
                emit_mov_state_imm32(
                    out,
                    gpr_offset(instruction.rd),
                    pc + 8u);
            }
            break;
        }

        case MipsOp::beq:
        case MipsOp::bne:
        case MipsOp::blez:
        case MipsOp::bgtz:
        case MipsOp::bltz:
        case MipsOp::bgez:
        case MipsOp::bltzal:
        case MipsOp::bgezal: {
            emit_load_eax_gpr(out, instruction.rs);
            if (instruction.op == MipsOp::beq ||
                instruction.op == MipsOp::bne) {
                emit_load_edx_gpr(out, instruction.rt);
                emit_u8(out, 0x39u);
                emit_u8(out, 0xD0u); // cmp eax, edx
            } else {
                emit_u8(out, 0x83u);
                emit_u8(out, 0xF8u);
                emit_u8(out, 0x00u); // cmp eax, 0
            }

            std::uint8_t setcc = 0u;
            switch (instruction.op) {
                case MipsOp::beq: setcc = 0x94u; break;
                case MipsOp::bne: setcc = 0x95u; break;
                case MipsOp::blez: setcc = 0x9Eu; break;
                case MipsOp::bgtz: setcc = 0x9Fu; break;
                case MipsOp::bltz:
                case MipsOp::bltzal: setcc = 0x9Cu; break;
                case MipsOp::bgez:
                case MipsOp::bgezal: setcc = 0x9Du; break;
                default: break;
            }
            emit_u8(out, 0x0Fu);
            emit_u8(out, setcc);
            emit_u8(out, 0xC0u); // setcc al
            emit_u8(out, 0x0Fu);
            emit_u8(out, 0xB6u);
            emit_u8(out, 0xC0u); // movzx eax, al

            const auto target =
                pc + 4u + (sign_extend16(instruction.immediate) << 2u);
            const auto fallthrough = pc + 8u;
            emit_mov_state_imm8(out, delay_active_offset(), 1u);
            emit_mov_state_imm32(out, delay_branch_pc_offset(), pc);
            emit_store_al_state(out, delay_taken_offset());
            emit_mov_state_imm32(out, delay_target_offset(), target);

            emit_u8(out, 0x69u);
            emit_u8(out, 0xC0u); // imul eax, eax, delta
            emit_u32(out, target - fallthrough);
            emit_u8(out, 0x05u); // add eax, fallthrough
            emit_u32(out, fallthrough);
            emit_store_eax_state(
                out,
                static_cast<std::uint32_t>(offsetof(R3000aState, next_pc)));
            emit_mov_state_imm32(
                out,
                static_cast<std::uint32_t>(offsetof(R3000aState, pc)),
                pc + 4u);

            if (instruction.op == MipsOp::bltzal ||
                instruction.op == MipsOp::bgezal) {
                emit_mov_state_imm32(out, gpr_offset(31u), pc + 8u);
            }
            break;
        }

        default:
            return Result<R3000aX64Code>::failure(
                ErrorCode::backend_unavailable,
                "R3000A direct x64 control-flow lowering is unavailable");
    }

    emit_mov_state_imm32(
        out,
        static_cast<std::uint32_t>(offsetof(R3000aState, gpr)),
        0u);
    emit_mov_eax_imm32(out, 1u);
    emit_u8(out, 0xC3u);

    const auto materialized = materialize_executable(code);
    if (!materialized) {
        return Result<R3000aX64Code>::failure(
            materialized.error,
            materialized.detail);
    }
    return Result<R3000aX64Code>::success(std::move(code));
}

Result<R3000aX64Code> emit_r3000a_x64_alu_block(
    const R3000aIrBlock& block) noexcept {
    if (block.instructions.empty() ||
        block.terminator != R3000aIrTerminatorKind::fallthrough ||
        block.has_delay_slot) {
        return Result<R3000aX64Code>::failure(
            ErrorCode::backend_unavailable,
            "R3000A x64 v0 only lowers straight-line ALU blocks");
    }

    R3000aX64Code code{};
    code.entry_pc = block.entry_pc;
    code.instruction_count = block.instructions.size();
    auto& out = code.bytes;
    out.reserve(block.instructions.size() * 24u + 40u);

    std::uint32_t expected_pc = block.entry_pc;
    for (const auto& ir : block.instructions) {
        if (ir.pc != expected_pc || ir.delay_slot ||
            !r3000a_op_is_x64_lowerable(ir.decoded.op)) {
            return Result<R3000aX64Code>::failure(
                ErrorCode::backend_unavailable,
                "R3000A block contains semantics not lowered by x64 v0");
        }
        expected_pc += 4u;

        const auto& ins = ir.decoded;
        switch (ins.op) {
            case MipsOp::sll:
            case MipsOp::srl:
            case MipsOp::sra:
                emit_load_eax_gpr(out, ins.rt);
                emit_u8(out, 0xC1u);
                emit_u8(
                    out,
                    ins.op == MipsOp::sll ? 0xE0u
                    : ins.op == MipsOp::srl ? 0xE8u
                                             : 0xF8u);
                emit_u8(out, static_cast<std::uint8_t>(ins.sa & 31u));
                emit_store_eax_gpr(out, ins.rd);
                break;

            case MipsOp::sllv:
            case MipsOp::srlv:
            case MipsOp::srav:
                emit_load_eax_gpr(out, ins.rt);
                emit_load_edx_gpr(out, ins.rs);
                emit_variable_shift(
                    out,
                    ins.op == MipsOp::sllv ? 0xE0u
                    : ins.op == MipsOp::srlv ? 0xE8u
                                              : 0xF8u);
                emit_store_eax_gpr(out, ins.rd);
                break;

            case MipsOp::mfhi:
                emit_load_eax_state(
                    out,
                    static_cast<std::uint32_t>(offsetof(R3000aState, hi)));
                emit_store_eax_gpr(out, ins.rd);
                break;
            case MipsOp::mthi:
                emit_load_eax_gpr(out, ins.rs);
                emit_store_eax_state(
                    out,
                    static_cast<std::uint32_t>(offsetof(R3000aState, hi)));
                break;
            case MipsOp::mflo:
                emit_load_eax_state(
                    out,
                    static_cast<std::uint32_t>(offsetof(R3000aState, lo)));
                emit_store_eax_gpr(out, ins.rd);
                break;
            case MipsOp::mtlo:
                emit_load_eax_gpr(out, ins.rs);
                emit_store_eax_state(
                    out,
                    static_cast<std::uint32_t>(offsetof(R3000aState, lo)));
                break;

            case MipsOp::mult:
            case MipsOp::multu:
                emit_load_eax_gpr(out, ins.rs);
                emit_load_edx_gpr(out, ins.rt);
                emit_u8(out, 0xF7u);
                emit_u8(out, ins.op == MipsOp::mult ? 0xEAu : 0xE2u);
                emit_store_eax_state(
                    out,
                    static_cast<std::uint32_t>(offsetof(R3000aState, lo)));
                emit_store_edx_state(
                    out,
                    static_cast<std::uint32_t>(offsetof(R3000aState, hi)));
                break;

            case MipsOp::addu:
            case MipsOp::subu:
            case MipsOp::bit_and:
            case MipsOp::bit_or:
            case MipsOp::bit_xor:
            case MipsOp::bit_nor:
            case MipsOp::slt:
            case MipsOp::sltu:
                emit_load_eax_gpr(out, ins.rs);
                emit_load_edx_gpr(out, ins.rt);
                if (ins.op == MipsOp::addu) {
                    emit_u8(out, 0x01u); emit_u8(out, 0xD0u);
                } else if (ins.op == MipsOp::subu) {
                    emit_u8(out, 0x29u); emit_u8(out, 0xD0u);
                } else if (ins.op == MipsOp::bit_and) {
                    emit_u8(out, 0x21u); emit_u8(out, 0xD0u);
                } else if (ins.op == MipsOp::bit_or ||
                           ins.op == MipsOp::bit_nor) {
                    emit_u8(out, 0x09u); emit_u8(out, 0xD0u);
                    if (ins.op == MipsOp::bit_nor) {
                        emit_u8(out, 0xF7u); emit_u8(out, 0xD0u);
                    }
                } else if (ins.op == MipsOp::bit_xor) {
                    emit_u8(out, 0x31u); emit_u8(out, 0xD0u);
                } else {
                    emit_u8(out, 0x39u); emit_u8(out, 0xD0u);
                    emit_u8(out, 0x0Fu);
                    emit_u8(out, ins.op == MipsOp::slt ? 0x9Cu : 0x92u);
                    emit_u8(out, 0xC0u);
                    emit_u8(out, 0x0Fu); emit_u8(out, 0xB6u); emit_u8(out, 0xC0u);
                }
                emit_store_eax_gpr(out, ins.rd);
                break;

            case MipsOp::addiu:
            case MipsOp::andi:
            case MipsOp::ori:
            case MipsOp::xori:
            case MipsOp::slti:
            case MipsOp::sltiu: {
                emit_load_eax_gpr(out, ins.rs);
                const auto immediate =
                    (ins.op == MipsOp::andi ||
                     ins.op == MipsOp::ori ||
                     ins.op == MipsOp::xori)
                    ? static_cast<std::uint32_t>(ins.immediate)
                    : sign_extend16(ins.immediate);
                if (ins.op == MipsOp::addiu) {
                    emit_u8(out, 0x05u); emit_u32(out, immediate);
                } else if (ins.op == MipsOp::andi) {
                    emit_u8(out, 0x25u); emit_u32(out, immediate);
                } else if (ins.op == MipsOp::ori) {
                    emit_u8(out, 0x0Du); emit_u32(out, immediate);
                } else if (ins.op == MipsOp::xori) {
                    emit_u8(out, 0x35u); emit_u32(out, immediate);
                } else {
                    emit_u8(out, 0x3Du); emit_u32(out, immediate);
                    emit_u8(out, 0x0Fu);
                    emit_u8(out, ins.op == MipsOp::slti ? 0x9Cu : 0x92u);
                    emit_u8(out, 0xC0u);
                    emit_u8(out, 0x0Fu); emit_u8(out, 0xB6u); emit_u8(out, 0xC0u);
                }
                emit_store_eax_gpr(out, ins.rt);
                break;
            }

            case MipsOp::lui:
                emit_mov_eax_imm32(
                    out,
                    static_cast<std::uint32_t>(ins.immediate) << 16u);
                emit_store_eax_gpr(out, ins.rt);
                break;

            default:
                return Result<R3000aX64Code>::failure(
                    ErrorCode::backend_unavailable,
                    "R3000A x64 lowering reached an unsupported operation");
        }
    }

    emit_mov_state_imm32(
        out,
        static_cast<std::uint32_t>(offsetof(R3000aState, gpr)),
        0u);
    const auto final_pc =
        block.entry_pc +
        static_cast<std::uint32_t>(block.instructions.size() * 4u);
    emit_mov_state_imm32(
        out,
        static_cast<std::uint32_t>(offsetof(R3000aState, pc)),
        final_pc);
    emit_mov_state_imm32(
        out,
        static_cast<std::uint32_t>(offsetof(R3000aState, next_pc)),
        final_pc + 4u);
    emit_mov_eax_imm32(
        out,
        static_cast<std::uint32_t>(block.instructions.size()));
    emit_u8(out, 0xC3u);

    const auto materialized = materialize_executable(code);
    if (!materialized) {
        return Result<R3000aX64Code>::failure(
            materialized.error,
            materialized.detail);
    }

    return Result<R3000aX64Code>::success(std::move(code));
}

R3000aX64ExecutionResult execute_r3000a_x64_block(
    const R3000aX64Code& code,
    R3000aState& state) noexcept {
    if (!state_is_safe_for_x64(code, state)) {
        return {R3000aX64ExecutionStatus::reference_required, 0u};
    }

    state.cop0.cause =
        (state.cop0.cause & ~0x0000FC00u) |
        (static_cast<std::uint32_t>(
             state.external_interrupt_pending & 0xFCu) << 8u);

#if defined(_WIN32) && defined(_M_X64)
    if (!code.executable_owner || !code.executable_entry) {
        return {R3000aX64ExecutionStatus::host_error, 0u};
    }

    using Entry = std::uint32_t (*)(R3000aState*);
    const auto entry = reinterpret_cast<Entry>(
        const_cast<void*>(code.executable_entry));
    const auto retired = entry(&state);

    if (retired != code.instruction_count) {
        return {R3000aX64ExecutionStatus::host_error, retired};
    }
    return {R3000aX64ExecutionStatus::executed, retired};
#else
    (void)state;
    return {R3000aX64ExecutionStatus::host_unavailable, 0u};
#endif
}

} // namespace jojo
