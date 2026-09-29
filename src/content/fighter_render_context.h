#pragma once

#include "content/fighter_overlay.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct FighterAnimationScriptRecord {
    std::uint32_t source_offset{};
    std::uint8_t command{};
    std::uint8_t record_length{};
    std::uint16_t frame_index{};
};

enum class FighterAnimationCandidateClass : std::uint8_t {
    generic,
    tkc_like,
    frame_sequence_like,
};

struct FighterAnimationScriptCandidate {
    std::uint32_t source_pointer_offset{};
    std::uint32_t target_offset{};
    std::uint32_t confidence_score{};
    FighterAnimationCandidateClass classification{
        FighterAnimationCandidateClass::generic};
    std::uint32_t unique_frame_count{};
    std::uint32_t command_46_count{};
    std::uint32_t command_8a_count{};
    bool canonical_sequence_root{};
    std::vector<FighterAnimationScriptRecord> records;
};

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

[[nodiscard]] std::vector<FighterAnimationScriptCandidate>
scan_animation_script_candidates(
    std::span<const std::uint8_t> overlay,
    std::uint32_t frame_count);

[[nodiscard]] std::vector<FighterRenderContextCandidate>
scan_compact_render_context_candidates(
    std::span<const std::uint8_t> overlay,
    std::uint32_t frame_count,
    std::uint32_t side);

} // namespace jojo::content
