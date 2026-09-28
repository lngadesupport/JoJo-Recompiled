#pragma once

#include "core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

constexpr std::uint32_t kpln_clut_width_words = 0x180u;
constexpr std::uint32_t kpln_clut_base_y = 0x1e0u;
constexpr std::uint32_t kpln_clut_height = 0x18u;

struct KplnClutWindow {
    std::uint32_t width{kpln_clut_width_words};
    std::uint32_t height{kpln_clut_height};
    std::vector<std::uint16_t> bgr555;

    [[nodiscard]] std::uint16_t at(
        std::uint32_t x_word,
        std::uint32_t vram_y) const noexcept;
};

struct KplnClutWindows {
    std::uint32_t palette_count{};
    std::vector<KplnClutWindow> windows;
};

[[nodiscard]] Result<KplnClutWindows> build_kpln_clut_windows(
    std::span<const std::uint8_t> pool_0803,
    std::span<const std::uint8_t> pool_0804,
    std::span<const std::uint8_t> pool_0805,
    std::span<const std::uint8_t> pool_0806,
    std::span<const std::uint8_t> pool_0807);

[[nodiscard]] std::uint16_t sample_kpln_clut(
    const KplnClutWindow& window,
    std::uint32_t clut_row_base,
    std::uint32_t selector,
    std::uint32_t pixel_index) noexcept;

} // namespace jojo::content
