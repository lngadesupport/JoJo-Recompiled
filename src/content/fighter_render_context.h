#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct FighterRenderContextCandidate {
    std::uint32_t source_offset{};
    std::uint16_t frame_index{};
    std::int8_t clut_mode{};
    std::uint8_t clut_base{};
    std::uint16_t clut_row_base{};
    std::uint8_t asset_slot{};
    std::uint8_t flip_a{};
    std::uint8_t flip_b{};
    std::uint8_t orientation{};
    std::uint32_t confidence_score{};
};

[[nodiscard]] std::vector<FighterRenderContextCandidate>
scan_compact_render_context_candidates(
    std::span<const std::uint8_t> overlay,
    std::uint32_t frame_count,
    std::uint32_t side);

} // namespace jojo::content
