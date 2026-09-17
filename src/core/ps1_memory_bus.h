#pragma once

#include "core/r3000a_bus.h"
#include "core/result.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace jojo {

struct Ps1UnsupportedAccess {
    std::uint32_t guest_address{};
    std::uint32_t physical_address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
};

class Ps1MemoryBus final : public R3000aBus {
public:
    static constexpr std::uint32_t main_ram_size = 2u * 1024u * 1024u;
    static constexpr std::uint32_t scratchpad_base = 0x1F800000u;
    static constexpr std::uint32_t scratchpad_size = 1024u;

    Ps1MemoryBus();

    static std::optional<std::uint32_t> guest_to_physical(std::uint32_t guest) noexcept;

    R3000aBusResult read8(std::uint32_t address) noexcept override;
    R3000aBusResult read16(std::uint32_t address) noexcept override;
    R3000aBusResult read32(std::uint32_t address) noexcept override;
    R3000aBusResult write8(std::uint32_t address, std::uint8_t value) noexcept override;
    R3000aBusResult write16(std::uint32_t address, std::uint16_t value) noexcept override;
    R3000aBusResult write32(std::uint32_t address, std::uint32_t value) noexcept override;

    Result<void> load_main_ram(std::uint32_t guest_address,
                               std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::uint16_t interrupt_mask() const noexcept;
    [[nodiscard]] std::uint32_t dma_interrupt() const noexcept;
    [[nodiscard]] std::uint16_t timer1_counter() const noexcept;
    [[nodiscard]] std::uint16_t timer1_mode() const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    void set_diagnostic_mmio_probe_enabled(bool enabled) noexcept;
    [[nodiscard]] bool diagnostic_mmio_probe_enabled() const noexcept;
    const std::optional<Ps1UnsupportedAccess>& last_diagnostic_mmio_probe() const noexcept;
    void clear_last_diagnostic_mmio_probe() noexcept;

    const std::optional<Ps1UnsupportedAccess>& last_unsupported_access() const noexcept;
    void clear_last_unsupported_access() noexcept;

private:
    static constexpr std::size_t diagnostic_mmio_shadow_size = 0x2000u;

    std::vector<std::uint8_t> main_ram_;
    std::array<std::uint8_t, scratchpad_size> scratchpad_{};
    std::uint16_t interrupt_status_{};
    std::uint16_t interrupt_mask_{};
    std::uint32_t dma_control_{0x07654321u};
    std::uint32_t dma_interrupt_{};
    std::uint16_t timer1_counter_{};
    std::uint16_t timer1_mode_{};
    bool diagnostic_mmio_probe_enabled_{};
    std::array<std::uint8_t, diagnostic_mmio_shadow_size> diagnostic_mmio_shadow_{};
    std::optional<Ps1UnsupportedAccess> last_diagnostic_mmio_probe_{};
    std::optional<Ps1UnsupportedAccess> last_unsupported_{};
};

} // namespace jojo
