#pragma once

#include "core/ps1_disc_session.h"
#include "core/r3000a_bus.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>

namespace jojo {

struct Ps1CdromCommandEvent {
    std::uint8_t command{};
    std::uint8_t index{};
    std::uint8_t status{};
};

class Ps1CdromController {
public:
    void attach_disc(const Ps1DiscSession* disc) noexcept;

    [[nodiscard]] R3000aBusResult read8(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult write8(std::uint32_t physical, std::uint8_t value) noexcept;

    [[nodiscard]] std::size_t response_bytes_available() const noexcept;
    [[nodiscard]] std::size_t data_bytes_available() const noexcept;
    [[nodiscard]] std::size_t read_data_words(std::span<std::uint32_t> out) noexcept;
    [[nodiscard]] std::uint64_t current_lba() const noexcept;
    [[nodiscard]] std::uint64_t command_count() const noexcept;
    [[nodiscard]] std::uint8_t request_register() const noexcept;
    [[nodiscard]] bool irq_pending() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
    [[nodiscard]] const std::deque<Ps1CdromCommandEvent>& recent_commands() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& last_unsupported_command() const noexcept;

private:
    static constexpr std::size_t parameter_capacity = 16u;
    static constexpr std::size_t response_capacity = 16u;
    static constexpr std::size_t data_capacity = 2048u;
    static constexpr std::size_t command_history_capacity = 64u;

    [[nodiscard]] bool push_response(std::uint8_t value) noexcept;
    [[nodiscard]] R3000aBusResult execute_command(std::uint8_t command) noexcept;
    void clear_transfer_fifos() noexcept;

    const Ps1DiscSession* disc_{};
    std::uint8_t index_{};
    std::uint8_t interrupt_enable_{};
    std::uint8_t interrupt_flags_{};
    std::uint8_t request_register_{};
    std::uint8_t status_byte_{};
    std::uint64_t current_lba_{};
    std::deque<std::uint8_t> parameters_{};
    std::deque<std::uint8_t> responses_{};
    std::deque<std::uint8_t> data_{};
    std::uint64_t command_count_{};
    std::deque<Ps1CdromCommandEvent> recent_commands_{};
    std::optional<std::uint8_t> last_unsupported_command_{};
};

} // namespace jojo
