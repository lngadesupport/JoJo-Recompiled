#pragma once

#include "core/r3000a_bus.h"

#include <array>
#include <cstdint>
#include <deque>

namespace jojo {

class Ps1Sio0 {
public:
    static constexpr std::uint32_t data_address = 0x1F801040u;
    static constexpr std::uint32_t status_address = 0x1F801044u;
    static constexpr std::uint32_t mode_address = 0x1F801048u;
    static constexpr std::uint32_t control_address = 0x1F80104Au;
    static constexpr std::uint32_t baud_address = 0x1F80104Eu;

    [[nodiscard]] R3000aBusResult read8(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult read16(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult read32(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult write8(
        std::uint32_t physical,
        std::uint8_t value) noexcept;
    [[nodiscard]] R3000aBusResult write16(
        std::uint32_t physical,
        std::uint16_t value) noexcept;
    [[nodiscard]] R3000aBusResult write32(
        std::uint32_t physical,
        std::uint32_t value) noexcept;

    void set_digital_pad_buttons(
        std::uint32_t port,
        std::uint16_t active_low_buttons) noexcept;

    [[nodiscard]] bool irq_pending() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

private:
    enum class TransactionState : std::uint8_t {
        idle,
        controller_command,
        controller_id_high,
        controller_buttons_low,
        controller_buttons_high,
        done,
    };

    void reset_transaction() noexcept;
    void reset_registers() noexcept;
    void transfer_byte(std::uint8_t value) noexcept;
    [[nodiscard]] std::uint32_t status_value() const noexcept;
    [[nodiscard]] std::uint32_t selected_port() const noexcept;

    std::uint16_t mode_{};
    std::uint16_t control_{};
    std::uint16_t baud_{};
    std::array<std::uint16_t, 2> pad_buttons_{0xFFFFu, 0xFFFFu};
    std::deque<std::uint8_t> rx_fifo_{};
    TransactionState transaction_{TransactionState::idle};
    bool dsr_{};
    bool irq_{};
};

} // namespace jojo
