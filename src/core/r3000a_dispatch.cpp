#include "core/r3000a_dispatch.h"

#include "core/r3000a_reference_executor.h"

namespace jojo {

R3000aDispatchResult dispatch_r3000a_block(
    const R3000aIrBlock& block,
    R3000aState& state,
    R3000aBus& bus,
    R3000aX64BlockCache& cache) noexcept {
    const auto compiled = cache.get_or_compile(block);
    if (compiled) {
        const auto native = execute_r3000a_x64_block(*compiled.value, state);
        if (native.status == R3000aX64ExecutionStatus::executed) {
            return {
                R3000aDispatchStatus::completed,
                R3000aDispatchMode::native_x64,
                native.instructions_retired,
                std::nullopt,
            };
        }
        if (native.status == R3000aX64ExecutionStatus::host_error) {
            return {
                R3000aDispatchStatus::native_host_error,
                R3000aDispatchMode::native_x64,
                native.instructions_retired,
                std::nullopt,
            };
        }
    }

    std::size_t retired = 0u;
    for (std::size_t i = 0u; i < block.instructions.size(); ++i) {
        const auto step = step_r3000a(state, bus);
        if (step.status != R3000aStepStatus::retired) {
            return {
                R3000aDispatchStatus::stopped,
                R3000aDispatchMode::reference_fallback,
                retired,
                step,
            };
        }
        ++retired;
    }

    return {
        R3000aDispatchStatus::completed,
        R3000aDispatchMode::reference_fallback,
        retired,
        std::nullopt,
    };
}

} // namespace jojo
