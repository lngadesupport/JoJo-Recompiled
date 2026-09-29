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

struct KplnCellReference {
    std::uint32_t source_offset{};
    std::uint8_t flags{};
};

struct KplnElementRecord {
    std::uint32_t block_dword_offset{};
    std::uint8_t layout_width{};
    std::uint8_t layout_height{};
    std::uint16_t raw_field2{};
    std::uint16_t raw_field3{};
    std::int16_t signed_field2{};
    std::int16_t signed_field3{};
    std::uint16_t payload_count{};
    std::vector<std::uint32_t> mask_dwords;
    std::vector<KplnCellReference> cells;
};

struct KplnElementTable {
    std::uint32_t descriptor_count{};
    std::uint32_t first_block_dword_offset{};
    std::vector<KplnElementRecord> records;
};

[[nodiscard]] Result<KplnIndexedPage4bpp>
parse_kpln_indexed_page_0202(
    std::span<const std::uint8_t> bytes);

} // namespace jojo::content
