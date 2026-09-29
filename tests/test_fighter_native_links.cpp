#include "content/fighter_native_links.h"

#include <cstdlib>
#include <iostream>

namespace {

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "CHECK failed at line "
              << line << ": " << expression << "\n";
    std::exit(1);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

} // namespace

int main() {
    jojo::content::HitTable hit{};
    hit.records[3] = {-12, 20, -8, 32};
    hit.nonzero_records = 1u;

    jojo::content::FighterTkRoots tk{};
    jojo::content::FighterTkcRecord hit_link{};
    hit_link.reference_index = 3u;
    tk.slots[0].tkc_records.push_back(hit_link);

    jojo::content::FighterTkcRecord empty_hit_link{};
    empty_hit_link.reference_index = 4u;
    tk.slots[0].tkc_records.push_back(empty_hit_link);

    jojo::content::FighterTkdRecord visual_link{};
    visual_link.element_index = 1u;
    tk.slots[0].tkd_records.push_back(visual_link);

    jojo::content::FighterTkdRecord missing_visual{};
    missing_visual.element_index = 9u;
    tk.slots[0].tkd_records.push_back(missing_visual);

    const auto analysis =
        jojo::content::analyze_fighter_native_links(
            hit, tk, 2u, 4u);

    CHECK(analysis.tkc_record_count == 2u);
    CHECK(analysis.tkc_hit_index_in_range_count == 2u);
    CHECK(analysis.tkc_nonempty_hit_candidate_count == 1u);
    CHECK(analysis.tkc_hit_candidates.size() == 2u);
    CHECK(analysis.tkc_hit_candidates[0].slot_index == 0u);
    CHECK(analysis.tkc_hit_candidates[0].record_index == 0u);
    CHECK(analysis.tkc_hit_candidates[0].hit_index == 3u);
    CHECK(analysis.tkc_hit_candidates[0].target_in_range);
    CHECK(analysis.tkc_hit_candidates[0].target_nonempty);
    CHECK(!analysis.tkc_hit_candidates[1].target_nonempty);

    CHECK(analysis.tkd_record_count == 2u);
    CHECK(analysis.tkd_direct_frame_index_in_range_count == 1u);
    CHECK(analysis.tkd_cached_frame_index_in_range_count == 1u);
    CHECK(analysis.tkd_graphics_candidates.size() == 2u);
    CHECK(analysis.tkd_graphics_candidates[0].element_index == 1u);
    CHECK(analysis.tkd_graphics_candidates[0].direct_target_in_range);
    CHECK(analysis.tkd_graphics_candidates[0].cached_target_in_range);
    CHECK(analysis.tkd_graphics_candidates[1].element_index == 9u);
    CHECK(!analysis.tkd_graphics_candidates[1].direct_target_in_range);
    CHECK(!analysis.tkd_graphics_candidates[1].cached_target_in_range);

    std::cout << "fighter native link analysis tests passed\n";
    return 0;
}
