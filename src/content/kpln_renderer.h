#pragma once

#include "content/kpln_clut.h"
#include "content/kpln_graphics.h"
#include "content/kpln_sprite_frames.h"
#include "core/result.h"

#include <cstdint>
#include <vector>

namespace jojo::content {

struct KplnRenderedFrame {
    std::uint32_t width{};
    std::uint32_t height{};
    std::int32_t origin_x{};
    std::int32_t origin_y{};
    std::vector<std::uint32_t> rgba8;
};

[[nodiscard]] Result<KplnRenderedFrame> render_kpln_direct_frame(
    const KplnDirectFrame& frame,
    const KplnIndexedPage4bpp& atlas,
    const KplnClutWindow& clut,
    std::uint32_t side,
    std::uint32_t clut_base);

[[nodiscard]] Result<KplnRenderedFrame> render_kpln_cached_frame(
    const KplnCachedFrame& frame,
    const KplnClutWindow& clut,
    std::uint32_t side,
    std::uint32_t clut_base,
    std::uint32_t clut_mode,
    std::uint32_t render_mode,
    std::uint32_t orientation,
    std::uint32_t clut_row_base = 0u);

} // namespace jojo::content
