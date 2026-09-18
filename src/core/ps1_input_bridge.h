#pragma once

#include "core/input.h"

#include <array>
#include <cstdint>

namespace jojo {

[[nodiscard]] std::uint16_t ps1_digital_pad_buttons(
    const ResolvedPlayerInput& input) noexcept;

[[nodiscard]] std::array<std::uint16_t, input_player_count>
ps1_digital_pad_frame(const ResolvedInputFrame& input) noexcept;

} // namespace jojo
