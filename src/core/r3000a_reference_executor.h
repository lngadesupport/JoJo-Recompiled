#pragma once

#include "core/ps1_exe.h"
#include "core/r3000a_bus.h"
#include "core/r3000a_diagnostics.h"
#include "core/r3000a_state.h"

#include <cstddef>

namespace jojo {

[[nodiscard]] R3000aStepResult step_r3000a(R3000aState& state, R3000aBus& bus) noexcept;
[[nodiscard]] R3000aStepResult step_r3000a_prefetched(
    R3000aState& state,
    R3000aBus& bus,
    std::uint32_t raw_opcode,
    std::uint8_t* main_ram = nullptr,
    std::size_t main_ram_size = 0u) noexcept;
[[nodiscard]] R3000aState initialize_r3000a_for_psx_exe(const Ps1ExeMetadata& metadata) noexcept;

} // namespace jojo
