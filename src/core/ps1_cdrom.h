#pragma once

#include "core/ps1_disc_session.h"
#include "core/r3000a_bus.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace jojo {

struct Ps1CdromCommandEvent {
    std::uint8_t command{};
    std::uint8_t index{};
    std::uint8_t status{};
    std::uint64_t lba{};
    std::uint8_t mode{};
    std::uint8_t request{};
    std::uint8_t interrupt_flags{};
    std::uint32_t data_bytes{};
    std::uint32_t sector_buffer_bytes{};
    std::uint8_t drive_queue_depth{};
    bool read_stream_active{};
};

class Ps1CdromController {
public:
    void attach_disc(const Ps1DiscSession* disc) noexcept;

    [[nodiscard]] R3000aBusResult read8(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult write8(std::uint32_t physical, std::uint8_t value) noexcept;
    void step(std::uint32_t cpu_cycles) noexcept;

    [[nodiscard]] std::size_t response_bytes_available() const noexcept;
    [[nodiscard]] std::size_t data_bytes_available() const noexcept;
    [[nodiscard]] std::size_t read_data_words(std::span<std::uint32_t> out) noexcept;
    [[nodiscard]] std::uint64_t current_lba() const noexcept;
    [[nodiscard]] std::uint64_t command_count() const noexcept;
    [[nodiscard]] std::uint8_t request_register() const noexcept;
    [[nodiscard]] bool irq_pending() const noexcept;
    [[nodiscard]] std::size_t deferred_response_count() const noexcept;
    [[nodiscard]] bool muted() const noexcept;
    [[nodiscard]] bool adpcm_muted() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 4>&
    pending_audio_matrix() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 4>&
    active_audio_matrix() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
    [[nodiscard]] const std::deque<Ps1CdromCommandEvent>& recent_commands() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& last_unsupported_command() const noexcept;

private:
    struct DeferredResponse {
        std::uint8_t interrupt_code{};
        std::uint8_t response{};
        std::uint32_t delay_cycles{};
        std::vector<std::uint8_t> data{};
        bool advance_lba{};
        bool apply_response_to_status{};
        bool stop_read_stream_on_completion{};
    };

    static constexpr std::size_t parameter_capacity = 16u;
    static constexpr std::size_t response_capacity = 16u;
    static constexpr std::size_t data_capacity = 2340u;
    static constexpr std::size_t drive_sector_buffer_capacity = 8u;
    static constexpr std::size_t command_history_capacity = 64u;

    [[nodiscard]] bool push_response(std::uint8_t value) noexcept;
    [[nodiscard]] R3000aBusResult execute_command(std::uint8_t command) noexcept;
    [[nodiscard]] std::uint32_t sector_cycles() const noexcept;
    void stop_read_stream() noexcept;
    void clear_transfer_fifos() noexcept;

    const Ps1DiscSession* disc_{};
    std::uint8_t index_{};
    std::uint8_t interrupt_enable_{};
    std::uint8_t interrupt_flags_{};
    std::uint8_t request_register_{};
    std::uint8_t status_byte_{};
    std::uint8_t mode_{};
    std::uint8_t filter_file_{};
    std::uint8_t filter_channel_{};
    bool muted_{};
    bool adpcm_muted_{};
    std::array<std::uint8_t, 4> pending_audio_matrix_{
        0x80u, 0x00u, 0x80u, 0x00u};
    std::array<std::uint8_t, 4> active_audio_matrix_{
        0x80u, 0x00u, 0x80u, 0x00u};
    std::uint64_t current_lba_{};
    bool read_stream_active_{};
    std::uint32_t read_cycles_remaining_{};
    std::deque<std::uint8_t> parameters_{};
    std::deque<std::uint8_t> responses_{};
    std::deque<std::vector<std::uint8_t>> drive_sector_queue_{};
    std::deque<std::uint8_t> sector_buffer_{};
    std::deque<std::uint8_t> data_{};
    std::deque<DeferredResponse> deferred_responses_{};
    std::uint64_t command_count_{};
    std::deque<Ps1CdromCommandEvent> recent_commands_{};
    std::optional<std::uint8_t> last_unsupported_command_{};
};

} // namespace jojo
