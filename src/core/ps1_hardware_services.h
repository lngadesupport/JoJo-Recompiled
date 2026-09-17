#pragma once

#include "core/r3000a_bus.h"

#include <array>
#include <cstdint>

namespace jojo {

struct Ps1RootCounterState {
    std::uint16_t counter{};
    std::uint16_t mode{};
    std::uint16_t target{};
    std::uint32_t cycle_accumulator{};
};

class Ps1HardwareServices {
public:
    Ps1HardwareServices() = default;

    [[nodiscard]] R3000aBusResult read8(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult read16(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult read32(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult write8(std::uint32_t physical, std::uint8_t value) noexcept;
    [[nodiscard]] R3000aBusResult write16(std::uint32_t physical, std::uint16_t value) noexcept;
    [[nodiscard]] R3000aBusResult write32(std::uint32_t physical, std::uint32_t value) noexcept;

    void step(std::uint32_t cpu_cycles) noexcept;

    [[nodiscard]] std::uint16_t interrupt_status() const noexcept;
    [[nodiscard]] std::uint16_t interrupt_mask() const noexcept;
    [[nodiscard]] std::uint16_t timer_counter(std::uint32_t channel) const noexcept;
    [[nodiscard]] std::uint16_t timer_mode(std::uint32_t channel) const noexcept;
    [[nodiscard]] std::uint16_t timer_target(std::uint32_t channel) const noexcept;
    [[nodiscard]] bool interrupt_pending() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

private:
    static constexpr std::uint16_t interrupt_valid_bits = 0x07FFu;
    static constexpr std::uint16_t timer_supported_mode_mask = 0x03FFu;

    std::uint16_t interrupt_status_{};
    std::uint16_t interrupt_mask_{};
    std::array<Ps1RootCounterState, 3> timers_{};
};

} // namespace jojo
