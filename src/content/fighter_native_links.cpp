#include "content/fighter_native_links.h"

namespace jojo::content {

FighterNativeLinkAnalysis analyze_fighter_native_links(
    const HitTable& hit,
    const FighterTkRoots& tk,
    const KplnGroupTable& graphics) {
    FighterNativeLinkAnalysis analysis{};

    for (std::size_t slot_index = 0u;
         slot_index < tk.slots.size();
         ++slot_index) {
        const auto& slot = tk.slots[slot_index];

        for (std::size_t record_index = 0u;
             record_index < slot.tkc_records.size();
             ++record_index) {
            const auto& record =
                slot.tkc_records[record_index];
            FighterTkcHitCandidate candidate{};
            candidate.slot_index =
                static_cast<std::uint32_t>(slot_index);
            candidate.record_index =
                static_cast<std::uint32_t>(record_index);
            candidate.hit_index = record.reference_index;
            candidate.target_in_range =
                record.reference_index < hit.records.size();
            if (candidate.target_in_range) {
                ++analysis.tkc_hit_index_in_range_count;
                candidate.target_nonempty =
                    !hit.records[record.reference_index].empty();
                if (candidate.target_nonempty) {
                    ++analysis.tkc_nonempty_hit_candidate_count;
                }
            }
            ++analysis.tkc_record_count;
            analysis.tkc_hit_candidates.push_back(candidate);
        }

        for (std::size_t record_index = 0u;
             record_index < slot.tkd_records.size();
             ++record_index) {
            const auto& record =
                slot.tkd_records[record_index];
            FighterTkdGraphicsCandidate candidate{};
            candidate.slot_index =
                static_cast<std::uint32_t>(slot_index);
            candidate.record_index =
                static_cast<std::uint32_t>(record_index);
            candidate.group_index = record.element_index;
            candidate.target_in_range =
                record.element_index < graphics.records.size();
            if (candidate.target_in_range) {
                ++analysis.tkd_graphics_index_in_range_count;
            }
            ++analysis.tkd_record_count;
            analysis.tkd_graphics_candidates.push_back(candidate);
        }
    }

    return analysis;
}

} // namespace jojo::content
