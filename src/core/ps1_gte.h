#pragma once

#include "core/r3000a_state.h"

#include <cstdint>

namespace jojo {

enum class Ps1GteCommandStatus : std::uint8_t {
    ok,
    unsupported,
};

[[nodiscard]] std::uint32_t read_ps1_gte_data(
    const R3000aGte& gte,
    std::uint8_t index) noexcept;

void write_ps1_gte_data(
    R3000aGte& gte,
    std::uint8_t index,
    std::uint32_t value) noexcept;

[[nodiscard]] std::uint32_t read_ps1_gte_control(
    const R3000aGte& gte,
    std::uint8_t index) noexcept;

void write_ps1_gte_control(
    R3000aGte& gte,
    std::uint8_t index,
    std::uint32_t value) noexcept;

[[nodiscard]] Ps1GteCommandStatus execute_ps1_gte_command(
    R3000aGte& gte,
    std::uint32_t raw) noexcept;

} // namespace jojo
