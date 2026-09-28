#include "content/kpln_renderer.h"

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

} // namespace

int main() {
    jojo::content::KplnCachedFrame frame{};
    frame.source_record_index = 0u;

    jojo::content::KplnCachedPart part{};
    part.header.columns = 2u;
    part.header.rows = 1u;
    part.header.x_offset = 4;
    part.header.y_offset = 8;

    jojo::content::KplnCachedCell visible{};
    visible.column = 0u;
    visible.row = 0u;
    visible.visible = true;
    visible.has_descriptor = true;
    visible.descriptor.clut_selector = 5u;
    visible.descriptor.raw = 5u << 24u;
    visible.descriptor.tile_indices.assign(256u, 1u);

    jojo::content::KplnCachedCell empty{};
    empty.column = 1u;
    empty.row = 0u;
    empty.visible = false;

    part.cells.push_back(visible);
    part.cells.push_back(empty);
    frame.parts.push_back(part);

    jojo::content::KplnClutWindow clut{};
    clut.bgr555.assign(
        static_cast<std::size_t>(clut.width) * clut.height,
        0u);
    const auto selector_x = 5u * 16u + 1u;
    const auto selector_y = 0x1e8u - jojo::content::kpln_clut_base_y;
    clut.bgr555[
        static_cast<std::size_t>(selector_y) * clut.width +
        selector_x] = 0x001Fu;

    const auto rendered =
        jojo::content::render_kpln_cached_frame(
            frame, clut, 0u, 0u, 0u, 0u, 0u);
    CHECK(static_cast<bool>(rendered));
    CHECK(rendered.value.width == 32u);
    CHECK(rendered.value.height == 16u);
    CHECK(rendered.value.origin_x == -4);
    CHECK(rendered.value.origin_y == -8);
    CHECK(rendered.value.rgba8.size() == 32u * 16u);

    const auto first = rendered.value.rgba8[0];
    CHECK((first & 0xFFu) == 255u);
    CHECK(((first >> 8u) & 0xFFu) == 0u);
    CHECK(((first >> 16u) & 0xFFu) == 0u);
    CHECK(((first >> 24u) & 0xFFu) == 255u);

    const auto transparent =
        rendered.value.rgba8[20u];
    CHECK((transparent >> 24u) == 0u);

    // A recovered PL context can override the default side CLUT row.
    jojo::content::KplnClutWindow custom_row_clut = clut;
    custom_row_clut.bgr555[
        static_cast<std::size_t>(
            0x1e9u - jojo::content::kpln_clut_base_y) *
            custom_row_clut.width +
        selector_x] = 0x7C00u;
    const auto custom_clut_row =
        jojo::content::render_kpln_cached_frame(
            frame,
            custom_row_clut,
            0u,
            0u,
            0u,
            0u,
            0u,
            0x1e9u);
    CHECK(static_cast<bool>(custom_clut_row));
    const auto custom_pixel =
        custom_clut_row.value.rgba8[0];
    CHECK((custom_pixel & 0xFFu) == 0u);
    CHECK(((custom_pixel >> 8u) & 0xFFu) == 0u);
    CHECK(((custom_pixel >> 16u) & 0xFFu) == 255u);

    jojo::content::KplnDirectFrame direct_frame{};
    jojo::content::KplnDirectPart direct_part{};
    direct_part.header.columns = 1u;
    direct_part.header.rows = 1u;
    direct_part.header.x_offset = 0;
    direct_part.header.y_offset = 0;
    direct_part.cells.push_back({0u, 0u, 0u, false});
    direct_frame.parts.push_back(direct_part);

    jojo::content::KplnIndexedPage4bpp atlas{};
    atlas.indices.assign(1024u * 256u, 0u);
    for (std::uint32_t y = 0u; y < 16u; ++y) {
        for (std::uint32_t x = 0u; x < 16u; ++x) {
            atlas.indices[
                static_cast<std::size_t>(y) * 1024u + x] = 1u;
        }
    }

    jojo::content::KplnClutWindow direct_clut{};
    direct_clut.bgr555.assign(
        static_cast<std::size_t>(direct_clut.width) *
            direct_clut.height,
        0u);
    direct_clut.bgr555[
        static_cast<std::size_t>(
            0x1e8u - jojo::content::kpln_clut_base_y) *
            direct_clut.width +
        1u] = 0x03E0u;

    const auto direct_render =
        jojo::content::render_kpln_direct_frame(
            direct_frame,
            atlas,
            direct_clut,
            0u,
            0u);
    CHECK(static_cast<bool>(direct_render));
    CHECK(direct_render.value.width == 16u);
    CHECK(direct_render.value.height == 16u);
    const auto direct_pixel = direct_render.value.rgba8[0];
    CHECK((direct_pixel & 0xFFu) == 0u);
    CHECK(((direct_pixel >> 8u) & 0xFFu) == 255u);
    CHECK(((direct_pixel >> 16u) & 0xFFu) == 0u);
    CHECK(((direct_pixel >> 24u) & 0xFFu) == 255u);

    std::cout << "KPLN cached/direct renderer tests passed\n";
    return 0;
}
