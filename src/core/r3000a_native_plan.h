#pragma once

#include "core/r3000a_cfg.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo {

enum class R3000aNativeLoweringMode : std::uint8_t {
    native_alu,
    reference_fallback,
};

struct R3000aNativeBlockPlan {
    std::uint32_t entry_pc{};
    R3000aNativeLoweringMode mode{R3000aNativeLoweringMode::reference_fallback};
    std::size_t instruction_count{};
};

struct R3000aNativePlan {
    std::vector<R3000aNativeBlockPlan> blocks;
    std::size_t native_block_count{};
    std::size_t reference_fallback_block_count{};
    std::size_t native_instruction_count{};
};

[[nodiscard]] bool r3000a_op_is_native_alu_candidate(MipsOp op) noexcept;

[[nodiscard]] R3000aNativePlan plan_r3000a_native_lowering(
    const R3000aCfg& cfg);

} // namespace jojo
