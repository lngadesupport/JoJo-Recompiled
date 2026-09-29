#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct KplnDirectCell {
    std::uint32_t column{};
    std::uint32_t row{};
    std::uint16_t tile_word{};
    bool empty{};
};

struct KplnFrameHeader {
    std::uint32_t data_offset_units{};
    std::uint8_t columns{};
    std::uint8_t rows{};
    std::int16_t x_offset{};
    std::int16_t y_offset{};
    std::uint16_t marker{};
    bool continues{};
};

struct KplnDirectPart {
    KplnFrameHeader header;
    std::vector<KplnDirectCell> cells;
};

struct KplnDirectFrame {
    std::uint32_t source_record_index{};
    std::vector<KplnDirectPart> parts;
};

struct KplnTileDecompression {
    std::vector<std::uint8_t> bytes;
    std::uint32_t bytes_consumed{};
};

struct KplnCachedDescriptor {
    std::uint32_t raw{};
    std::uint32_t stream_offset{};
    std::uint8_t clut_selector{};
    std::uint8_t transform{};
    std::uint32_t compressed_bytes_consumed{};
    std::vector<std::uint8_t> tile_indices;
};

struct KplnCachedCell {
    std::uint32_t column{};
    std::uint32_t row{};
    bool visible{};
    bool has_descriptor{};
    KplnCachedDescriptor descriptor;
};

struct KplnCachedPart {
    KplnFrameHeader header;
    std::uint32_t visible_bit_count{};
    std::vector<KplnCachedCell> cells;
};

struct KplnCachedFrame {
    std::uint32_t source_record_index{};
    std::vector<KplnCachedPart> parts;
};

struct KplnCachedFrameSet {
    std::vector<KplnCachedFrame> frames;
    std::vector<std::uint32_t> unique_tile_offsets;
};

struct KplnIndexedSurface4bpp {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> indices;
};

[[nodiscard]] Result<std::vector<KplnDirectFrame>>
parse_kpln_direct_frames_0800(
    std::span<const std::uint8_t> frame_bytes);

[[nodiscard]] Result<KplnTileDecompression>
decompress_kpln_tile_0801(
    std::span<const std::uint8_t> stream);

[[nodiscard]] Result<KplnCachedFrameSet>
parse_kpln_cached_frames_0802(
    std::span<const std::uint8_t> frame_bytes,
    std::span<const std::uint8_t> tile_pool);

[[nodiscard]] Result<KplnIndexedSurface4bpp>
parse_kpln_indexed_surface_0204(
    std::span<const std::uint8_t> bytes);

} // namespace jojo::content
