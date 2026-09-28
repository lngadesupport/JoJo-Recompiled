#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct KplnGroupRecord {
    std::uint32_t list_word_offset{};
    std::uint16_t packed_layout{};
    std::uint8_t layout_low{};
    std::uint8_t layout_high{};
    std::uint16_t field2{};
    std::uint16_t field3{};
    std::uint16_t field4{};
    std::vector<std::uint16_t> indices;
};

struct KplnGroupTable {
    std::uint32_t table_word_count{};
    std::vector<KplnGroupRecord> records;
};

struct KplnPalette {
    std::array<std::uint16_t, 16> bgr555{};
};

struct KplnPaletteBank {
    std::vector<KplnPalette> palettes;
};

struct KplnIndexedPage4bpp {
    std::uint32_t width{1024u};
    std::uint32_t height{256u};
    std::vector<std::uint8_t> indices;
};

[[nodiscard]] Result<KplnGroupTable> parse_kpln_group_table_0800(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] Result<KplnPaletteBank> parse_kpln_palette_bank(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] Result<KplnIndexedPage4bpp> parse_kpln_indexed_page_0202(
    std::span<const std::uint8_t> bytes);

} // namespace jojo::content
