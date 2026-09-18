#include "core/r3000a_native_plan.h"

namespace jojo {

bool r3000a_op_is_native_alu_candidate(MipsOp op) noexcept {
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

R3000aNativePlan plan_r3000a_native_lowering(const R3000aCfg& cfg) {
    R3000aNativePlan plan{};
    plan.blocks.reserve(cfg.blocks.size());

    for (const auto& block : cfg.blocks) {
        bool native =
            block.terminator == R3000aIrTerminatorKind::fallthrough &&
            !block.has_delay_slot;

        if (native) {
            for (const auto& instruction : block.instructions) {
                if (!r3000a_op_is_native_alu_candidate(
                        instruction.decoded.op)) {
                    native = false;
                    break;
                }
            }
        }

        R3000aNativeBlockPlan block_plan{};
        block_plan.entry_pc = block.entry_pc;
        block_plan.instruction_count = block.instructions.size();
        block_plan.mode = native
            ? R3000aNativeLoweringMode::native_alu
            : R3000aNativeLoweringMode::reference_fallback;
        plan.blocks.push_back(block_plan);

        if (native) {
            ++plan.native_block_count;
            plan.native_instruction_count += block.instructions.size();
        } else {
            ++plan.reference_fallback_block_count;
        }
    }

    return plan;
}

} // namespace jojo
