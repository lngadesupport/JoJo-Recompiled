#include "content/fighter_render_context.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "CHECK failed at line "
              << line << ": " << expression << "\n";
    std::exit(1);
}
#define CHECK(expr) check((expr), #expr, __LINE__)

void put16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

} // namespace

int main() {
    std::vector<std::uint8_t> overlay(0x100u, 0u);

    constexpr std::size_t good = 0x20u;
    overlay[good + 0u] = 1u;
    overlay[good + 1u] = 2u;
    overlay[good + 0x06u] = 0xFFu; // signed clut mode -1
    overlay[good + 0x07u] = 5u;
    put16(overlay, good + 0x0cu, 0x01e8u);
    put16(overlay, good + 0x0eu, 0x1003u); // frame 3 after 0x0fff mask
    overlay[good + 0x1du] = 0u; // side 0
    overlay[good + 0x1eu] = 1u;
    overlay[good + 0x1fu] = 2u;

    constexpr std::size_t wrong_side = 0x60u;
    overlay[wrong_side + 0u] = 1u;
    overlay[wrong_side + 1u] = 2u;
    put16(overlay, wrong_side + 0x0cu, 0x01e8u);
    put16(overlay, wrong_side + 0x0eu, 2u);
    overlay[wrong_side + 0x1du] = 7u;

    const auto side0 =
        jojo::content::scan_compact_render_context_candidates(
            overlay, 10u, 0u);
    CHECK(side0.size() == 1u);
    CHECK(side0[0].source_offset == good);
    CHECK(side0[0].frame_index == 3u);
    CHECK(side0[0].clut_mode == -1);
    CHECK(side0[0].clut_base == 5u);
    CHECK(side0[0].clut_row_base == 0x01e8u);
    CHECK(side0[0].asset_slot == 0u);
    CHECK(side0[0].orientation == 3u);
    CHECK(side0[0].confidence_score >= 30u);

    // side 1 accepts asset slot 1 or 3, not side-0 records.
    const auto side1 =
        jojo::content::scan_compact_render_context_candidates(
            overlay, 10u, 1u);
    CHECK(side1.empty());

    // Frame bounds are enforced.
    const auto too_small =
        jojo::content::scan_compact_render_context_candidates(
            overlay, 3u, 0u);
    CHECK(too_small.empty());

    std::vector<std::uint8_t> script_overlay(0x200u, 0u);
    const auto script_address =
        jojo::content::fighter_overlay_primary_base + 0x80u;
    // Two different pointer fields resolve to the same script; output must
    // deduplicate the target while retaining one source pointer location.
    put16(script_overlay, 0x00u,
        static_cast<std::uint16_t>(script_address));
    put16(script_overlay, 0x02u,
        static_cast<std::uint16_t>(script_address >> 16u));
    put16(script_overlay, 0x10u,
        static_cast<std::uint16_t>(script_address));
    put16(script_overlay, 0x12u,
        static_cast<std::uint16_t>(script_address >> 16u));

    // Three valid records: length 4, 4, 6.
    script_overlay[0x80u] = 0x04u;
    put16(script_overlay, 0x82u, 0x1002u);
    script_overlay[0x84u] = 0x84u;
    put16(script_overlay, 0x86u, 0x0005u);
    script_overlay[0x88u] = 0x06u;
    put16(script_overlay, 0x8au, 0x2007u);
    script_overlay[0x8eu] = 0x00u;

    const auto scripts =
        jojo::content::scan_animation_script_candidates(
            script_overlay, 10u);
    CHECK(scripts.size() == 1u);
    CHECK(scripts[0].target_offset == 0x80u);
    CHECK(scripts[0].records.size() == 3u);
    CHECK(scripts[0].records[0].frame_index == 2u);
    CHECK(scripts[0].records[1].frame_index == 5u);
    CHECK(scripts[0].records[2].frame_index == 7u);
    CHECK(scripts[0].records[2].record_length == 6u);
    CHECK(scripts[0].confidence_score >= 23u);

    std::cout << "fighter render context/script candidate tests passed\n";
    return 0;
}
