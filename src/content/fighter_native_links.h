#pragma once

#include "content/fighter_tk.h"
#include "content/hit_table.h"
#include "content/kpln_graphics.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jojo::content {

struct FighterTkcHitCandidate {
    std::uint32_t slot_index{};
    std::uint32_t record_index{};
    std::uint16_t hit_index{};
    bool target_in_range{};
    bool target_nonempty{};
};

struct FighterTkdGraphicsCandidate {
    std::uint32_t slot_index{};
    std::uint32_t record_index{};
    std::uint16_t group_index{};
    bool target_in_range{};
};

struct FighterNativeLinkAnalysis {
    std::uint32_t tkc_record_count{};
    std::uint32_t tkc_hit_index_in_range_count{};
    std::uint32_t tkc_nonempty_hit_candidate_count{};
    std::uint32_t tkd_record_count{};
    std::uint32_t tkd_graphics_index_in_range_count{};
    std::vector<FighterTkcHitCandidate> tkc_hit_candidates;
    std::vector<FighterTkdGraphicsCandidate> tkd_graphics_candidates;
};

[[nodiscard]] FighterNativeLinkAnalysis analyze_fighter_native_links(
    const HitTable& hit,
    const FighterTkRoots& tk,
    const KplnGroupTable& graphics);

} // namespace jojo::content
