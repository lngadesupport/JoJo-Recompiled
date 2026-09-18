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

    [[nodiscard]] R3000aBusResult read_gp0() noexcept;
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
        polygon_payload,
        fill_rectangle_position,
        fill_rectangle_size,
        monochrome_rectangle_position,
        monochrome_rectangle_size,
        textured_rectangle_position,
        textured_rectangle_uv,
        textured_rectangle_size,
        vram_copy_source,
        vram_copy_destination,
        vram_copy_size,
        vram_to_cpu_source,
        vram_to_cpu_size,
        cpu_to_vram_destination,
        cpu_to_vram_size,
        cpu_to_vram_payload,
    };

    struct PolygonVertex {
        std::int32_t x{};
        std::int32_t y{};
        std::uint8_t r{};
        std::uint8_t g{};
        std::uint8_t b{};
        std::uint8_t u{};
        std::uint8_t v{};
    };

    void reset_command_buffer() noexcept;
    void reset_display_state() noexcept;
    void write_transfer_pixel(std::uint16_t pixel) noexcept;
    void begin_polygon(std::uint32_t command_word) noexcept;
    [[nodiscard]] bool execute_polygon_packet() noexcept;
    void rasterize_triangle(
        const PolygonVertex& a,
        const PolygonVertex& b,
        const PolygonVertex& c,
        bool textured,
        bool raw_texture,
        bool gouraud) noexcept;
    void fill_rectangle(std::uint32_t width, std::uint32_t height) noexcept;
    void draw_monochrome_rectangle(std::uint32_t width, std::uint32_t height) noexcept;
    void draw_textured_rectangle(std::uint32_t width, std::uint32_t height) noexcept;
    [[nodiscard]] std::uint16_t sample_raw_texture(
        std::uint32_t u,
        std::uint32_t v) const noexcept;
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
    std::uint16_t draw_color_{};
    std::int32_t draw_x_{};
    std::int32_t draw_y_{};
    std::uint32_t draw_area_left_{};
    std::uint32_t draw_area_top_{};
    std::uint32_t draw_area_right_{vram_width - 1u};
    std::uint32_t draw_area_bottom_{vram_height - 1u};
    std::int32_t draw_offset_x_{};
    std::int32_t draw_offset_y_{};
    std::uint32_t texture_page_x_{};
    std::uint32_t texture_page_y_{};
    std::uint8_t texture_depth_{};
    std::uint8_t texture_u_{};
    std::uint8_t texture_v_{};
    std::uint32_t texture_modulation_color_{0x00808080u};
    bool texture_raw_{true};
    std::uint8_t texture_window_mask_x_{};
    std::uint8_t texture_window_mask_y_{};
    std::uint8_t texture_window_offset_x_{};
    std::uint8_t texture_window_offset_y_{};
    bool texture_x_flip_{};
    bool texture_y_flip_{};
    std::uint32_t texture_fixed_width_{};
    std::uint32_t texture_fixed_height_{};
    std::uint32_t texture_clut_x_{};
    std::uint32_t texture_clut_y_{};
    std::vector<std::uint32_t> polygon_words_{};
    std::size_t polygon_words_expected_{};
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
    std::uint32_t readback_x_{};
    std::uint32_t readback_y_{};
    std::uint32_t readback_width_{};
    std::uint32_t readback_height_{};
    std::uint32_t readback_pixel_index_{};
    std::uint32_t readback_pixels_remaining_{};

    std::optional<std::uint8_t> last_unsupported_gp0_command_{};
    std::optional<std::uint8_t> last_unsupported_gp1_command_{};
};

} // namespace jojo
