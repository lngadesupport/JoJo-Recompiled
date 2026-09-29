#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct TimDecodedImage {
    std::size_t source_offset{};
    std::uint32_t flags{};
    std::uint32_t palette_index{};
    std::uint32_t palette_count{1u};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint16_t vram_x{};
    std::uint16_t vram_y{};
    bool has_semitransparent_pixels{};
    std::vector<std::uint32_t> rgba8;
};

[[nodiscard]] Result<std::vector<TimDecodedImage>>
decode_sector_aligned_tim_images(
    std::span<const std::uint8_t> bytes);

} // namespace jojo::content
