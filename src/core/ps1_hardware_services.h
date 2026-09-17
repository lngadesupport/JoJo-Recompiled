#pragma once

#include "core/ps1_cdrom.h"
#include "core/r3000a_bus.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace jojo {

struct Ps1RootCounterState {
    std::uint16_t counter{};
    std::uint16_t mode{};
    std::uint16_t target{};
    std::uint32_t cycle_accumulator{};
};

struct Ps1DmaChannelState {
    std::uint32_t madr{};
    std::uint32_t bcr{};
    std::uint32_t chcr{};
};

struct Ps1DmaTransferRequest {
    std::uint8_t channel{};
    std::uint32_t madr{};
    std::uint32_t words{};
    bool from_ram{};
};

class Ps1HardwareServices {
public:
    Ps1HardwareServices() = default;

    void attach_disc(const Ps1DiscSession* disc) noexcept;

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

    [[nodiscard]] std::uint32_t dma_control() const noexcept;
    [[nodiscard]] std::uint32_t dma_interrupt() const noexcept;
    [[nodiscard]] const Ps1DmaChannelState& dma_channel(std::uint32_t channel) const noexcept;
    [[nodiscard]] const std::optional<Ps1DmaTransferRequest>& pending_dma_transfer() const noexcept;
    [[nodiscard]] bool execute_pending_dma(std::span<std::uint8_t> main_ram) noexcept;
    [[nodiscard]] bool complete_dma_transfer(std::uint32_t channel) noexcept;
    void cancel_pending_dma_transfer() noexcept;
    [[nodiscard]] std::uint64_t completed_dma_transfer_count() const noexcept;

    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

private:
    static constexpr std::uint16_t interrupt_valid_bits = 0x07FFu;
    static constexpr std::uint16_t timer_supported_mode_mask = 0x03FFu;

    std::uint16_t interrupt_status_{};
    std::uint16_t interrupt_mask_{};
    std::array<Ps1RootCounterState, 3> timers_{};
    std::array<Ps1DmaChannelState, 7> dma_channels_{};
    std::uint32_t dma_control_{0x07654321u};
    std::uint32_t dma_interrupt_{};
    std::optional<Ps1DmaTransferRequest> pending_dma_transfer_{};
    std::uint64_t completed_dma_transfer_count_{};
    Ps1CdromController cdrom_{};
};

} // namespace jojo
