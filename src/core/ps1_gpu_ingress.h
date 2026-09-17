#pragma once

#include "core/r3000a_bus.h"

#include <cstdint>
#include <optional>

namespace jojo {

class Ps1GpuIngress {
public:
    [[nodiscard]] R3000aBusResult write_gp0(std::uint32_t value) noexcept;
    [[nodiscard]] R3000aBusResult write_gp1(std::uint32_t value) noexcept;

    [[nodiscard]] std::uint32_t status() const noexcept;
    [[nodiscard]] std::uint64_t gp0_word_count() const noexcept;
    [[nodiscard]] std::uint64_t gp1_command_count() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& last_unsupported_gp0_command() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& last_unsupported_gp1_command() const noexcept;

private:
    static constexpr std::uint32_t reset_status = 0x14802000u;

    std::uint32_t status_{reset_status};
    std::uint64_t gp0_word_count_{};
    std::uint64_t gp1_command_count_{};
    std::optional<std::uint8_t> last_unsupported_gp0_command_{};
    std::optional<std::uint8_t> last_unsupported_gp1_command_{};
};

} // namespace jojo
