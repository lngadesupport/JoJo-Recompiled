#pragma once

#include "core/r3000a_bus.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace jojo {

struct Ps1GpuDisplayState {
    bool enabled{};
    bool rgb24{};
    std::uint32_t start_x{};
    std::uint32_t start_y{};
    std::uint32_t width{256u};
    std::uint32_t height{240u};
};

class Ps1GpuIngress {
public:
    static constexpr std::uint32_t vram_width = 1024u;
    static constexpr std::uint32_t vram_height = 512u;

    [[nodiscard]] R3000aBusResult write_gp0(std::uint32_t value) noexcept;
    [[nodiscard]] R3000aBusResult write_gp1(std::uint32_t value) noexcept;

    [[nodiscard]] std::uint32_t status() const noexcept;
    [[nodiscard]] std::uint64_t gp0_word_count() const noexcept;
    [[nodiscard]] std::uint64_t gp1_command_count() const noexcept;
    [[nodiscard]] std::uint16_t vram_pixel(std::uint32_t x, std::uint32_t y) const noexcept;
    [[nodiscard]] std::uint64_t vram_write_count() const noexcept;
    [[nodiscard]] Ps1GpuDisplayState display_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& last_unsupported_gp0_command() const noexcept;
    [[nodiscard]] const std::optional<std::uint8_t>& last_unsupported_gp1_command() const noexcept;

private:
    static constexpr std::uint32_t reset_status = 0x14802000u;

    enum class Gp0Mode : std::uint8_t {
        command,
        fill_rectangle_position,
        fill_rectangle_size,
        vram_copy_source,
        vram_copy_destination,
        vram_copy_size,
        cpu_to_vram_destination,
        cpu_to_vram_size,
        cpu_to_vram_payload,
    };

    void reset_command_buffer() noexcept;
    void reset_display_state() noexcept;
    void write_transfer_pixel(std::uint16_t pixel) noexcept;
    void fill_rectangle(std::uint32_t width, std::uint32_t height) noexcept;
    void copy_vram_rectangle(std::uint32_t width, std::uint32_t height) noexcept;
    void apply_display_mode(std::uint32_t parameter) noexcept;

    std::uint32_t status_{reset_status};
    std::uint64_t gp0_word_count_{};
    std::uint64_t gp1_command_count_{};
    std::vector<std::uint16_t> vram_ = std::vector<std::uint16_t>(
        static_cast<std::size_t>(vram_width) * vram_height);
    std::uint64_t vram_write_count_{};
    Ps1GpuDisplayState display_{};

    Gp0Mode gp0_mode_{Gp0Mode::command};
    std::uint16_t fill_color_{};
    std::uint32_t fill_x_{};
    std::uint32_t fill_y_{};
    std::uint32_t copy_source_x_{};
    std::uint32_t copy_source_y_{};
    std::uint32_t copy_destination_x_{};
    std::uint32_t copy_destination_y_{};
    std::uint32_t transfer_x_{};
    std::uint32_t transfer_y_{};
    std::uint32_t transfer_width_{};
    std::uint32_t transfer_height_{};
    std::uint32_t transfer_pixel_index_{};
    std::uint32_t transfer_pixels_remaining_{};

    std::optional<std::uint8_t> last_unsupported_gp0_command_{};
    std::optional<std::uint8_t> last_unsupported_gp1_command_{};
};

} // namespace jojo
