#pragma once

#include "core/r3000a_ir.h"
#include "core/r3000a_state.h"

#include <cstddef>
#include <cstdint>

namespace jojo {

enum class R3000aNativeAluStatus : std::uint8_t {
    executed,
    reference_required,
};

struct R3000aNativeAluResult {
    R3000aNativeAluStatus status{R3000aNativeAluStatus::reference_required};
    std::size_t instructions_retired{};
};

[[nodiscard]] R3000aNativeAluResult execute_r3000a_native_alu_block(
    const R3000aIrBlock& block,
    R3000aState& state) noexcept;

} // namespace jojo
