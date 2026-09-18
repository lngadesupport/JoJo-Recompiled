#include "core/r3000a_native_alu.h"

#include "core/r3000a_native_plan.h"

#include <bit>
#include <cstdint>

namespace jojo {
namespace {

constexpr std::uint32_t kInterruptMask = 0x0000FF00u;

std::uint32_t sign_extend16(std::uint16_t value) noexcept {
    return (value & 0x8000u) != 0u
        ? (0xFFFF0000u | static_cast<std::uint32_t>(value))
        : static_cast<std::uint32_t>(value);
}

std::int32_t signed_view(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

std::uint32_t arithmetic_shift_right(
    std::uint32_t value,
    std::uint32_t amount) noexcept {
    amount &= 31u;
    if (amount == 0u) return value;
    const auto shifted = value >> amount;
    if ((value & 0x80000000u) == 0u) return shifted;
    return shifted | (0xFFFFFFFFu << (32u - amount));
}

void write_gpr(
    R3000aState& state,
    std::uint8_t reg,
    std::uint32_t value) noexcept {
    if (reg != 0u) state.gpr[reg] = value;
}

void retire_pending_load(R3000aState& state) noexcept {
    if (!state.pending_load.valid) return;
    write_gpr(
        state,
        state.pending_load.reg,
        state.pending_load.value);
    state.pending_load = {};
}

bool block_is_safe(
    const R3000aIrBlock& block,
    const R3000aState& state) noexcept {
    if (block.instructions.empty()) return false;
    if (block.terminator != R3000aIrTerminatorKind::fallthrough) return false;
    if (block.has_delay_slot || state.delay_slot.active) return false;
    if (state.pc != block.entry_pc ||
        state.next_pc != block.entry_pc + 4u) {
        return false;
    }

    const auto cause_with_external =
        (state.cop0.cause & ~0x0000FC00u) |
        (static_cast<std::uint32_t>(
             state.external_interrupt_pending & 0xFCu) << 8u);
    const bool interrupt_enabled = (state.cop0.status & 1u) != 0u;
    if (interrupt_enabled &&
        (cause_with_external & state.cop0.status & kInterruptMask) != 0u) {
        return false;
    }

    std::uint32_t expected_pc = block.entry_pc;
    for (const auto& instruction : block.instructions) {
        if (instruction.pc != expected_pc || instruction.delay_slot) return false;
        if (!r3000a_op_is_native_alu_candidate(instruction.decoded.op)) {
            return false;
        }
        expected_pc += 4u;
    }
    return true;
}

} // namespace

R3000aNativeAluResult execute_r3000a_native_alu_block(
    const R3000aIrBlock& block,
    R3000aState& state) noexcept {
    if (!block_is_safe(block, state)) {
        return {};
    }

    state.cop0.cause =
        (state.cop0.cause & ~0x0000FC00u) |
        (static_cast<std::uint32_t>(
             state.external_interrupt_pending & 0xFCu) << 8u);
    state.gpr[0] = 0u;
    std::size_t retired = 0u;

    for (const auto& ir : block.instructions) {
        const auto& instruction = ir.decoded;
        const std::uint32_t rs = state.gpr[instruction.rs];
        const std::uint32_t rt = state.gpr[instruction.rt];

        retire_pending_load(state);

        std::uint8_t write_reg = 0u;
        std::uint32_t write_value = 0u;
        bool write_valid = false;
        const auto queue_write = [&](std::uint8_t reg, std::uint32_t value) noexcept {
            if (reg == 0u) return;
            write_reg = reg;
            write_value = value;
            write_valid = true;
        };

        switch (instruction.op) {
            case MipsOp::sll:
                queue_write(instruction.rd, rt << instruction.sa);
                break;
            case MipsOp::srl:
                queue_write(instruction.rd, rt >> instruction.sa);
                break;
            case MipsOp::sra:
                queue_write(
                    instruction.rd,
                    arithmetic_shift_right(rt, instruction.sa));
                break;
            case MipsOp::sllv:
                queue_write(instruction.rd, rt << (rs & 31u));
                break;
            case MipsOp::srlv:
                queue_write(instruction.rd, rt >> (rs & 31u));
                break;
            case MipsOp::srav:
                queue_write(
                    instruction.rd,
                    arithmetic_shift_right(rt, rs));
                break;
            case MipsOp::mfhi:
                queue_write(instruction.rd, state.hi);
                break;
            case MipsOp::mthi:
                state.hi = rs;
                break;
            case MipsOp::mflo:
                queue_write(instruction.rd, state.lo);
                break;
            case MipsOp::mtlo:
                state.lo = rs;
                break;
            case MipsOp::addu:
                queue_write(instruction.rd, rs + rt);
                break;
            case MipsOp::subu:
                queue_write(instruction.rd, rs - rt);
                break;
            case MipsOp::bit_and:
                queue_write(instruction.rd, rs & rt);
                break;
            case MipsOp::bit_or:
                queue_write(instruction.rd, rs | rt);
                break;
            case MipsOp::bit_xor:
                queue_write(instruction.rd, rs ^ rt);
                break;
            case MipsOp::bit_nor:
                queue_write(instruction.rd, ~(rs | rt));
                break;
            case MipsOp::slt:
                queue_write(
                    instruction.rd,
                    signed_view(rs) < signed_view(rt) ? 1u : 0u);
                break;
            case MipsOp::sltu:
                queue_write(instruction.rd, rs < rt ? 1u : 0u);
                break;
            case MipsOp::addiu:
                queue_write(
                    instruction.rt,
                    rs + sign_extend16(instruction.immediate));
                break;
            case MipsOp::andi:
                queue_write(
                    instruction.rt,
                    rs & static_cast<std::uint32_t>(instruction.immediate));
                break;
            case MipsOp::ori:
                queue_write(
                    instruction.rt,
                    rs | static_cast<std::uint32_t>(instruction.immediate));
                break;
            case MipsOp::xori:
                queue_write(
                    instruction.rt,
                    rs ^ static_cast<std::uint32_t>(instruction.immediate));
                break;
            case MipsOp::lui:
                queue_write(
                    instruction.rt,
                    static_cast<std::uint32_t>(instruction.immediate) << 16u);
                break;
            case MipsOp::slti:
                queue_write(
                    instruction.rt,
                    signed_view(rs) <
                            signed_view(sign_extend16(instruction.immediate))
                        ? 1u
                        : 0u);
                break;
            case MipsOp::sltiu:
                queue_write(
                    instruction.rt,
                    rs < sign_extend16(instruction.immediate) ? 1u : 0u);
                break;
            default:
                return {R3000aNativeAluStatus::reference_required, retired};
        }

        if (write_valid) write_gpr(state, write_reg, write_value);

        state.pc = state.next_pc;
        state.next_pc += 4u;
        state.gpr[0] = 0u;
        ++retired;
    }

    return {R3000aNativeAluStatus::executed, retired};
}

} // namespace jojo
