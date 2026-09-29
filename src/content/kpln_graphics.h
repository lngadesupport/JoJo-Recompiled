#pragma once

#include "core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

// Native representation of KPLN opcode 0x0202 after removing the original
// VRAM placement. The retail payload is exactly one 1024x256 4bpp indexed
// surface. Frame parsing lives in kpln_sprite_frames; CLUT reconstruction
// lives in kpln_clut.
struct KplnIndexedPage4bpp {
    std::uint32_t width{1024u};
    std::uint32_t height{256u};
    std::vector<std::uint8_t> indices;
};

[[nodiscard]] Result<KplnIndexedPage4bpp>
parse_kpln_indexed_page_0202(
    std::span<const std::uint8_t> bytes);

} // namespace jojo::content
