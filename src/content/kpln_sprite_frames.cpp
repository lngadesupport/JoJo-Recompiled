#include "content/kpln_sprite_frames.h"

#include <algorithm>
#include <limits>
#include <set>

namespace jojo::content {
namespace {

std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8u));
}

std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8u) |
        (static_cast<std::uint32_t>(p[2]) << 16u) |
        (static_cast<std::uint32_t>(p[3]) << 24u);
}

Result<KplnFrameHeader> read_header(
    std::span<const std::uint8_t> bytes,
    std::size_t record_index) {
    const auto offset = record_index * 12u;
    if (offset > bytes.size() ||
        bytes.size() - offset < 12u) {
        return Result<KplnFrameHeader>::failure(
            ErrorCode::invalid_installation,
            "KPLN frame record is truncated");
    }

    const auto* p = bytes.data() + offset;
    const auto dimensions = le16(p + 2u);

    KplnFrameHeader header{};
    header.data_offset_units = le16(p + 0u);
    header.columns =
        static_cast<std::uint8_t>(dimensions & 0x00FFu);
    header.rows =
        static_cast<std::uint8_t>(dimensions >> 8u);
    header.x_offset =
        static_cast<std::int16_t>(le16(p + 4u));
    header.y_offset =
        static_cast<std::int16_t>(le16(p + 6u));
    header.marker = le16(p + 8u);
    header.continues = le16(p + 10u) != 0u;
    return Result<KplnFrameHeader>::success(header);
}

std::size_t find_record_table_end(
    std::span<const std::uint8_t> bytes) {
    const auto record_count = bytes.size() / 12u;
    for (std::size_t i = 0u; i < record_count; ++i) {
        const auto marker =
            le16(bytes.data() + i * 12u + 8u);
        if (marker == 0u) {
            return i;
        }
    }
    return record_count;
}

std::vector<std::uint8_t> expand_4bpp(
    std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> indices;
    indices.reserve(bytes.size() * 2u);
    for (const auto byte : bytes) {
        indices.push_back(
            static_cast<std::uint8_t>(byte & 0x0Fu));
        indices.push_back(
            static_cast<std::uint8_t>(byte >> 4u));
    }
    return indices;
}

} // namespace

Result<std::vector<KplnDirectFrame>>
parse_kpln_direct_frames_0800(
    std::span<const std::uint8_t> frame_bytes) {
    if (frame_bytes.size() < 12u) {
        return Result<std::vector<KplnDirectFrame>>::failure(
            ErrorCode::unsupported_format,
            "KPLN 0x0800 frame data is too small");
    }

    const auto table_records =
        find_record_table_end(frame_bytes);
    if (table_records == 0u) {
        return Result<std::vector<KplnDirectFrame>>::success({});
    }

    std::vector<KplnDirectFrame> frames;
    std::size_t record_index = 0u;

    while (record_index < table_records) {
        KplnDirectFrame frame{};
        frame.source_record_index =
            static_cast<std::uint32_t>(record_index);

        bool more = true;
        while (more) {
            if (record_index >= table_records) {
                return Result<std::vector<KplnDirectFrame>>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0800 continuation crosses the record table");
            }

            const auto header =
                read_header(frame_bytes, record_index);
            if (!header) {
                return Result<std::vector<KplnDirectFrame>>::failure(
                    header.error, header.detail);
            }
            if (header.value.columns == 0u ||
                header.value.rows == 0u) {
                return Result<std::vector<KplnDirectFrame>>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0800 frame has an empty tile matrix");
            }

            const auto cell_count =
                static_cast<std::size_t>(header.value.columns) *
                header.value.rows;
            const auto matrix_offset =
                static_cast<std::size_t>(
                    header.value.data_offset_units) * 2u;
            if (matrix_offset > frame_bytes.size() ||
                cell_count >
                    (frame_bytes.size() - matrix_offset) / 2u) {
                return Result<std::vector<KplnDirectFrame>>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0800 tile matrix is outside the chunk");
            }

            KplnDirectPart part{};
            part.header = header.value;
            part.cells.reserve(cell_count);

            for (std::uint32_t column = 0u;
                 column < header.value.columns;
                 ++column) {
                for (std::uint32_t row = 0u;
                     row < header.value.rows;
                     ++row) {
                    const auto linear =
                        static_cast<std::size_t>(column) *
                            header.value.rows +
                        row;
                    const auto tile_word =
                        le16(
                            frame_bytes.data() +
                            matrix_offset +
                            linear * 2u);
                    part.cells.push_back({
                        column,
                        row,
                        tile_word,
                        tile_word == 0xFFFFu,
                    });
                }
            }

            more = header.value.continues;
            frame.parts.push_back(std::move(part));
            ++record_index;
        }

        frames.push_back(std::move(frame));
    }

    return Result<std::vector<KplnDirectFrame>>::success(
        std::move(frames));
}

Result<KplnTileDecompression>
decompress_kpln_tile_0801(
    std::span<const std::uint8_t> stream) {
    constexpr std::size_t kOutputBytes = 0x80u;

    KplnTileDecompression output{};
    output.bytes.resize(kOutputBytes);

    std::size_t source = 0u;
    std::size_t target = 0u;
    std::uint8_t control = 0u;
    std::uint32_t control_bits_left = 0u;

    while (target < kOutputBytes) {
        if (control_bits_left == 0u) {
            if (source >= stream.size()) {
                return Result<KplnTileDecompression>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0801 tile ended before a control byte");
            }
            control = stream[source++];
            control_bits_left = 8u;
        }

        const bool compressed = (control & 1u) != 0u;
        control >>= 1u;
        --control_bits_left;

        if (!compressed) {
            if (source >= stream.size()) {
                return Result<KplnTileDecompression>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0801 literal exceeds the stream");
            }
            output.bytes[target++] = stream[source++];
            continue;
        }

        if (source >= stream.size()) {
            return Result<KplnTileDecompression>::failure(
                ErrorCode::invalid_installation,
                "KPLN 0x0801 token exceeds the stream");
        }

        const auto token = stream[source++];
        const auto length =
            static_cast<std::uint32_t>(token & 0x0Fu);
        const auto distance =
            static_cast<std::uint32_t>(token >> 4u);

        if (distance == 0u) {
            if (source >= stream.size()) {
                return Result<KplnTileDecompression>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0801 run is missing its value");
            }
            const auto value = stream[source++];
            const auto count = length + 1u;
            for (std::uint32_t i = 0u;
                 i < count && target < kOutputBytes;
                 ++i) {
                output.bytes[target++] = value;
            }
        } else {
            for (std::uint32_t i = 0u;
                 i < length && target < kOutputBytes;
                 ++i) {
                if (distance > target) {
                    output.bytes[target++] = 0u;
                } else {
                    output.bytes[target] =
                        output.bytes[target - distance];
                    ++target;
                }
            }
        }
    }

    output.bytes_consumed =
        static_cast<std::uint32_t>(source);
    return Result<KplnTileDecompression>::success(
        std::move(output));
}

Result<KplnCachedFrameSet>
parse_kpln_cached_frames_0802(
    std::span<const std::uint8_t> frame_bytes,
    std::span<const std::uint8_t> tile_pool) {
    if (frame_bytes.size() < 12u) {
        return Result<KplnCachedFrameSet>::failure(
            ErrorCode::unsupported_format,
            "KPLN 0x0802 frame data is too small");
    }

    const auto table_records =
        find_record_table_end(frame_bytes);
    KplnCachedFrameSet result{};
    std::set<std::uint32_t> unique_offsets;
    std::size_t record_index = 0u;

    while (record_index < table_records) {
        KplnCachedFrame frame{};
        frame.source_record_index =
            static_cast<std::uint32_t>(record_index);

        bool more = true;
        while (more) {
            if (record_index >= table_records) {
                return Result<KplnCachedFrameSet>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0802 continuation crosses the record table");
            }

            const auto header =
                read_header(frame_bytes, record_index);
            if (!header) {
                return Result<KplnCachedFrameSet>::failure(
                    header.error, header.detail);
            }
            if (header.value.columns == 0u ||
                header.value.rows == 0u) {
                return Result<KplnCachedFrameSet>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0802 frame has an empty cell matrix");
            }

            const auto cell_count =
                static_cast<std::size_t>(header.value.columns) *
                header.value.rows;
            const auto mask_offset =
                static_cast<std::size_t>(
                    header.value.data_offset_units) * 4u;
            const auto mask_bytes =
                ((cell_count + 31u) / 32u) * 4u;
            if (mask_offset > frame_bytes.size() ||
                mask_bytes > frame_bytes.size() - mask_offset) {
                return Result<KplnCachedFrameSet>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0802 visibility mask is outside the chunk");
            }

            const auto descriptors_offset =
                mask_offset + mask_bytes;
            const auto descriptor_capacity =
                (frame_bytes.size() - descriptors_offset) / 4u;
            if (header.value.marker > descriptor_capacity) {
                return Result<KplnCachedFrameSet>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0802 descriptor list exceeds the chunk");
            }

            KplnCachedPart part{};
            part.header = header.value;
            part.cells.reserve(cell_count);

            std::size_t descriptor_index = 0u;
            std::uint8_t control = 0u;
            std::uint32_t bits_left = 0u;
            std::size_t bit_index = 0u;

            for (std::uint32_t column = 0u;
                 column < header.value.columns;
                 ++column) {
                for (std::uint32_t row = 0u;
                     row < header.value.rows;
                     ++row) {
                    if (bits_left == 0u) {
                        const auto byte_offset =
                            mask_offset + bit_index / 8u;
                        if (byte_offset >=
                            mask_offset + mask_bytes) {
                            return Result<KplnCachedFrameSet>::failure(
                                ErrorCode::invalid_installation,
                                "KPLN 0x0802 visibility mask ended early");
                        }
                        control = frame_bytes[byte_offset];
                        bits_left = 8u;
                    }

                    const bool visible =
                        (control & 0x80u) != 0u;
                    control =
                        static_cast<std::uint8_t>(control << 1u);
                    --bits_left;
                    ++bit_index;

                    KplnCachedCell cell{};
                    cell.column = column;
                    cell.row = row;
                    cell.visible = visible;

                    if (visible) {
                        ++part.visible_bit_count;
                    }

                    if (visible &&
                        descriptor_index < header.value.marker) {
                        const auto descriptor_offset =
                            descriptors_offset +
                            descriptor_index * 4u;
                        const auto raw =
                            le32(
                                frame_bytes.data() +
                                descriptor_offset);
                        ++descriptor_index;

                        KplnCachedDescriptor descriptor{};
                        descriptor.raw = raw;
                        descriptor.stream_offset =
                            raw & 0x00FFFFFFu;
                        descriptor.clut_selector =
                            static_cast<std::uint8_t>(
                                (raw >> 24u) & 0x3Fu);
                        descriptor.transform =
                            static_cast<std::uint8_t>(
                                raw >> 30u);

                        if (descriptor.stream_offset >=
                            tile_pool.size()) {
                            return Result<KplnCachedFrameSet>::failure(
                                ErrorCode::invalid_installation,
                                "KPLN 0x0801 stream offset is outside the tile pool");
                        }

                        const auto decoded =
                            decompress_kpln_tile_0801(
                                tile_pool.subspan(
                                    descriptor.stream_offset));
                        if (!decoded) {
                            return Result<KplnCachedFrameSet>::failure(
                                decoded.error,
                                "KPLN 0x0801 stream at offset " +
                                    std::to_string(
                                        descriptor.stream_offset) +
                                    ": " + decoded.detail);
                        }

                        descriptor.compressed_bytes_consumed =
                            decoded.value.bytes_consumed;
                        descriptor.tile_indices =
                            expand_4bpp(decoded.value.bytes);
                        cell.has_descriptor = true;
                        cell.descriptor =
                            std::move(descriptor);
                        unique_offsets.insert(
                            cell.descriptor.stream_offset);
                    }

                    part.cells.push_back(std::move(cell));
                }
            }

            // Across all 10,154 retail 0x0802 records the marker is exactly
            // the number of MSB-first visibility bits inside columns*rows.
            // Enforce that invariant so a corrupt/false table cannot silently
            // desynchronize descriptors from cells.
            if (part.visible_bit_count != header.value.marker ||
                descriptor_index != header.value.marker) {
                return Result<KplnCachedFrameSet>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0802 visibility count does not match descriptor count");
            }

            more = header.value.continues;
            frame.parts.push_back(std::move(part));
            ++record_index;
        }

        result.frames.push_back(std::move(frame));
    }

    result.unique_tile_offsets.assign(
        unique_offsets.begin(),
        unique_offsets.end());
    return Result<KplnCachedFrameSet>::success(
        std::move(result));
}

Result<KplnIndexedSurface4bpp>
parse_kpln_indexed_surface_0204(
    std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kChunkBytes = 0x80u;
    constexpr std::size_t kStripBytes = 0x800u;
    constexpr std::uint32_t kStripWidth = 16u;
    constexpr std::uint32_t kHeight = 256u;
    constexpr std::size_t kRowBytes = kStripWidth / 2u;

    if (bytes.empty() ||
        bytes.size() % kChunkBytes != 0u) {
        return Result<KplnIndexedSurface4bpp>::failure(
            ErrorCode::unsupported_format,
            "KPLN 0x0204 payload is not a whole number of upload chunks");
    }

    const auto strip_count =
        (bytes.size() + kStripBytes - 1u) /
        kStripBytes;
    if (strip_count >
        std::numeric_limits<std::uint32_t>::max() /
            kStripWidth) {
        return Result<KplnIndexedSurface4bpp>::failure(
            ErrorCode::unsupported_format,
            "KPLN 0x0204 surface is too wide");
    }

    KplnIndexedSurface4bpp surface{};
    surface.width =
        static_cast<std::uint32_t>(strip_count) *
        kStripWidth;
    surface.height = kHeight;
    surface.indices.assign(
        static_cast<std::size_t>(surface.width) *
            surface.height,
        0u);

    for (std::size_t source = 0u;
         source < bytes.size();
         ++source) {
        const auto strip = source / kStripBytes;
        const auto within = source % kStripBytes;
        const auto row = within / kRowBytes;
        const auto byte_in_row = within % kRowBytes;
        if (row >= kHeight) {
            return Result<KplnIndexedSurface4bpp>::failure(
                ErrorCode::invalid_installation,
                "KPLN 0x0204 row exceeds the 256-row strip");
        }

        const auto x =
            strip * kStripWidth +
            byte_in_row * 2u;
        const auto base =
            row * surface.width + x;
        surface.indices[base + 0u] =
            static_cast<std::uint8_t>(
                bytes[source] & 0x0Fu);
        surface.indices[base + 1u] =
            static_cast<std::uint8_t>(
                bytes[source] >> 4u);
    }

    return Result<KplnIndexedSurface4bpp>::success(
        std::move(surface));
}

} // namespace jojo::content
