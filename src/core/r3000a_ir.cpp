#include "core/r3000a_ir.h"

#include <algorithm>
#include <utility>

namespace jojo {
namespace {

bool is_conditional_branch(MipsOp op) noexcept {
    switch (op) {
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

bool is_direct_jump(MipsOp op) noexcept {
    return op == MipsOp::j || op == MipsOp::jal;
}

bool is_indirect_jump(MipsOp op) noexcept {
    return op == MipsOp::jr || op == MipsOp::jalr;
}

std::uint32_t branch_target(
    std::uint32_t pc,
    std::uint16_t immediate) noexcept {
    const auto signed_imm = static_cast<std::int32_t>(
        static_cast<std::int16_t>(immediate));
    const auto displacement = static_cast<std::uint32_t>(signed_imm * 4);
    return pc + 4u + displacement;
}

std::uint32_t jump_target(
    std::uint32_t pc,
    std::uint32_t target) noexcept {
    return ((pc + 4u) & 0xF0000000u) | ((target & 0x03FFFFFFu) << 2u);
}

} // namespace

Result<R3000aIrBlock> lift_r3000a_basic_block(
    std::uint32_t entry_pc,
    std::span<const std::uint32_t> words,
    std::size_t max_instructions) noexcept {
    if ((entry_pc & 3u) != 0u) {
        return Result<R3000aIrBlock>::failure(
            ErrorCode::invalid_argument,
            "R3000A basic-block entry PC must be word-aligned");
    }
    if (words.empty()) {
        return Result<R3000aIrBlock>::failure(
            ErrorCode::invalid_argument,
            "R3000A basic block requires at least one instruction word");
    }
    if (max_instructions == 0u) {
        return Result<R3000aIrBlock>::failure(
            ErrorCode::invalid_argument,
            "R3000A basic-block instruction limit must be non-zero");
    }

    R3000aIrBlock block{};
    block.entry_pc = entry_pc;

    const auto limit = std::min(words.size(), max_instructions);
    std::optional<std::size_t> control_index;

    for (std::size_t i = 0u; i < limit; ++i) {
        const auto pc = entry_pc + static_cast<std::uint32_t>(i * 4u);
        const auto decoded = decode_mips(words[i]);
        if (decoded.op == MipsOp::reserved) {
            return Result<R3000aIrBlock>::failure(
                ErrorCode::backend_unavailable,
                "R3000A block contains an unsupported/reserved instruction");
        }

        const bool delay_slot =
            control_index.has_value() && i == *control_index + 1u;
        block.instructions.push_back(R3000aIrInstruction{
            pc,
            decoded,
            delay_slot,
        });

        if (delay_slot) {
            break;
        }

        if (!is_control_transfer(decoded.op)) {
            continue;
        }

        control_index = i;
        block.has_delay_slot = true;

        if (is_conditional_branch(decoded.op)) {
            block.terminator = R3000aIrTerminatorKind::conditional_branch;
            block.taken_target = branch_target(pc, decoded.immediate);
            block.fallthrough_target = pc + 8u;
        } else if (is_direct_jump(decoded.op)) {
            block.terminator = R3000aIrTerminatorKind::direct_jump;
            block.taken_target = jump_target(pc, decoded.target);
            block.fallthrough_target.reset();
        } else if (is_indirect_jump(decoded.op)) {
            block.terminator = R3000aIrTerminatorKind::indirect_jump;
            block.taken_target.reset();
            block.fallthrough_target.reset();
        }

        if (i + 1u >= limit) {
            return Result<R3000aIrBlock>::failure(
                ErrorCode::backend_unavailable,
                "R3000A control transfer is missing its delay-slot instruction");
        }
    }

    if (!control_index) {
        block.terminator = R3000aIrTerminatorKind::fallthrough;
        const auto byte_count =
            static_cast<std::uint32_t>(block.instructions.size() * 4u);
        block.fallthrough_target = entry_pc + byte_count;
    }

    return Result<R3000aIrBlock>::success(std::move(block));
}

} // namespace jojo
