#pragma once

#include "core/r3000a_bus.h"
#include "core/r3000a_diagnostics.h"
#include "core/r3000a_ir.h"
#include "core/r3000a_state.h"
#include "core/r3000a_x64_cache.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace jojo {

enum class R3000aDispatchMode : std::uint8_t {
    native_x64,
    reference_fallback,
};

enum class R3000aDispatchStatus : std::uint8_t {
    completed,
    stopped,
    native_host_error,
};

struct R3000aDispatchResult {
    R3000aDispatchStatus status{R3000aDispatchStatus::stopped};
    R3000aDispatchMode mode{R3000aDispatchMode::reference_fallback};
    std::size_t instructions_retired{};
    std::optional<R3000aStepResult> stop_result;
};

[[nodiscard]] R3000aDispatchResult dispatch_r3000a_block(
    const R3000aIrBlock& block,
    R3000aState& state,
    R3000aBus& bus,
    R3000aX64BlockCache& cache) noexcept;

} // namespace jojo
