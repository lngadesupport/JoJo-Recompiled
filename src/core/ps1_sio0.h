#pragma once

#include "core/r3000a_bus.h"
#include "core/ps1_memory_card.h"

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
    void step(std::uint32_t cpu_cycles) noexcept;

    void set_digital_pad_buttons(
        std::uint32_t port,
        std::uint16_t active_low_buttons) noexcept;
    [[nodiscard]] std::uint16_t sample_digital_pad_buttons(
        std::uint32_t port) noexcept;
    [[nodiscard]] std::uint16_t digital_pad_buttons(
        std::uint32_t port) const noexcept;
    [[nodiscard]] Ps1MemoryCard& memory_card(std::uint32_t port) noexcept;
    [[nodiscard]] const Ps1MemoryCard& memory_card(std::uint32_t port) const noexcept;
    [[nodiscard]] std::uint64_t digital_pad_poll_count(
        std::uint32_t port) const noexcept;
    [[nodiscard]] std::uint64_t digital_pad_pressed_poll_count(
        std::uint32_t port) const noexcept;
    [[nodiscard]] std::uint64_t memory_card_read_sector_count(
        std::uint32_t port) const noexcept;
    [[nodiscard]] std::uint64_t memory_card_write_sector_count(
        std::uint32_t port) const noexcept;
    [[nodiscard]] std::uint64_t memory_card_changed_write_sector_count(
        std::uint32_t port) const noexcept;
    [[nodiscard]] std::uint64_t raw_data_read_count() const noexcept;
    [[nodiscard]] std::uint64_t raw_data_write_count() const noexcept;
    [[nodiscard]] std::uint64_t raw_status_read_count() const noexcept;
    [[nodiscard]] std::uint64_t raw_control_write_count() const noexcept;
    [[nodiscard]] std::uint64_t controller_address_byte_count() const noexcept;
    [[nodiscard]] std::uint64_t controller_command_byte_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_card_address_byte_count() const noexcept;
    [[nodiscard]] std::uint64_t controller_id_high_stage_byte_count() const noexcept;
    [[nodiscard]] std::uint64_t controller_buttons_low_stage_byte_count() const noexcept;
    [[nodiscard]] std::uint64_t controller_buttons_high_stage_byte_count() const noexcept;
    [[nodiscard]] std::uint64_t dtr_fall_reset_count() const noexcept;
    [[nodiscard]] std::uint64_t port_change_reset_count() const noexcept;
    [[nodiscard]] std::uint64_t control_reset_count() const noexcept;

    [[nodiscard]] bool irq_pending() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

private:
    enum class TransactionState : std::uint8_t {
        idle,
        controller_command,
        controller_id_high,
        controller_buttons_low,
        controller_buttons_high,
        memory_command,
        memory_transfer,
        done,
    };

    void reset_transaction() noexcept;
    void reset_registers() noexcept;
    void transfer_byte(std::uint8_t value) noexcept;
    void transfer_memory_byte(
        std::uint8_t value,
        std::uint8_t& response,
        bool& more_data) noexcept;
    [[nodiscard]] std::uint32_t status_value() const noexcept;
    [[nodiscard]] std::uint32_t selected_port() const noexcept;

    std::uint16_t mode_{};
    std::uint16_t control_{};
    std::uint16_t baud_{};
    std::array<std::uint16_t, 2> pad_buttons_{0xFFFFu, 0xFFFFu};
    std::array<Ps1MemoryCard, 2> memory_cards_{};
    std::array<std::uint64_t, 2> digital_pad_poll_count_{};
    std::array<std::uint64_t, 2> digital_pad_pressed_poll_count_{};
    std::array<std::uint64_t, 2> memory_card_read_sector_count_{};
    std::array<std::uint64_t, 2> memory_card_write_sector_count_{};
    std::array<std::uint64_t, 2> memory_card_changed_write_sector_count_{};
    // Observation-only access telemetry. These counters intentionally do not
    // participate in diagnostic_state_hash().
    std::uint64_t raw_data_read_count_{};
    std::uint64_t raw_data_write_count_{};
    std::uint64_t raw_status_read_count_{};
    std::uint64_t raw_control_write_count_{};
    std::uint64_t controller_address_byte_count_{};
    std::uint64_t controller_command_byte_count_{};
    std::uint64_t memory_card_address_byte_count_{};
    std::uint64_t controller_id_high_stage_byte_count_{};
    std::uint64_t controller_buttons_low_stage_byte_count_{};
    std::uint64_t controller_buttons_high_stage_byte_count_{};
    std::uint64_t dtr_fall_reset_count_{};
    std::uint64_t port_change_reset_count_{};
    std::uint64_t control_reset_count_{};
    std::deque<std::uint8_t> rx_fifo_{};
    TransactionState transaction_{TransactionState::idle};
    std::uint8_t memory_command_{};
    std::uint16_t memory_stage_{};
    std::uint16_t memory_sector_{};
    std::uint8_t memory_checksum_{};
    std::uint8_t memory_previous_byte_{};
    std::uint8_t memory_end_byte_{0x47u};
    Ps1MemoryCard::Sector memory_write_buffer_{};
    bool memory_sector_valid_{};
    bool dsr_{};
    std::uint32_t dsr_cycles_remaining_{};
    bool irq_{};
};

} // namespace jojo
