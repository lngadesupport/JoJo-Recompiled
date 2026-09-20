#include "content/tim_image.h"

#include <algorithm>
#include <array>
#include <limits>

namespace jojo::content {
namespace {

constexpr std::uint32_t kTimMagic = 0x00000010u;
constexpr std::size_t kSectorSize = 2048u;

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

bool valid_tim_flags(std::uint32_t flags) noexcept {
    const auto mode = flags & 0x7u;
    if (mode > 3u) return false;
    const auto unknown = flags & ~0xBu;
    if (unknown != 0u) return false;
    if ((flags & 0x8u) != 0u && mode > 1u) {
        return false;
    }
    return true;
}

std::uint8_t expand5(std::uint16_t value) noexcept {
    value &= 0x1Fu;
    return static_cast<std::uint8_t>(
        (value << 3u) | (value >> 2u));
}

std::uint32_t bgr555_to_rgba(
    std::uint16_t value,
    bool& has_stp) noexcept {
    const auto rgb = static_cast<std::uint16_t>(
        value & 0x7FFFu);
    if ((value & 0x8000u) != 0u) {
        has_stp = true;
    }

    // Preserve color-key transparency while keeping PS1 STP as metadata.
    // STP is not equivalent to ordinary alpha; the original primitive state
    // decides whether semitransparency is used.
    const std::uint8_t alpha =
        rgb == 0u ? 0u : 255u;
    const auto red = expand5(value);
    const auto green = expand5(value >> 5u);
    const auto blue = expand5(value >> 10u);
    return static_cast<std::uint32_t>(red) |
        (static_cast<std::uint32_t>(green) << 8u) |
        (static_cast<std::uint32_t>(blue) << 16u) |
        (static_cast<std::uint32_t>(alpha) << 24u);
}

struct TimBlockHeader {
    std::uint32_t length{};
    std::uint16_t x{};
    std::uint16_t y{};
    std::uint16_t width_words{};
    std::uint16_t height{};
};

Result<TimBlockHeader> read_block_header(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) {
    if (offset > bytes.size() ||
        bytes.size() - offset < 12u) {
        return Result<TimBlockHeader>::failure(
            ErrorCode::invalid_installation,
            "TIM block header is truncated");
    }

    TimBlockHeader header{};
    header.length = le32(bytes.data() + offset);
    header.x = le16(bytes.data() + offset + 4u);
    header.y = le16(bytes.data() + offset + 6u);
    header.width_words =
        le16(bytes.data() + offset + 8u);
    header.height =
        le16(bytes.data() + offset + 10u);

    if (header.length < 12u ||
        header.length > bytes.size() - offset) {
        return Result<TimBlockHeader>::failure(
            ErrorCode::invalid_installation,
            "TIM block length is outside the source buffer");
    }
    return Result<TimBlockHeader>::success(header);
}

Result<std::vector<TimDecodedImage>> decode_tim_at(
    std::span<const std::uint8_t> bytes,
    std::size_t source_offset) {
    if (source_offset > bytes.size() ||
        bytes.size() - source_offset < 8u ||
        le32(bytes.data() + source_offset) != kTimMagic) {
        return Result<std::vector<TimDecodedImage>>::failure(
            ErrorCode::unsupported_format,
            "TIM magic is missing");
    }

    const auto flags =
        le32(bytes.data() + source_offset + 4u);
    if (!valid_tim_flags(flags)) {
        return Result<std::vector<TimDecodedImage>>::failure(
            ErrorCode::unsupported_format,
            "unsupported TIM flags");
    }

    const auto mode = flags & 0x7u;
    const bool has_clut = (flags & 0x8u) != 0u;
    std::size_t cursor = source_offset + 8u;
    std::vector<std::vector<std::uint16_t>> palettes;

    if (has_clut) {
        const auto clut = read_block_header(bytes, cursor);
        if (!clut) {
            return Result<std::vector<TimDecodedImage>>::failure(
                clut.error, clut.detail);
        }

        const auto color_count =
            static_cast<std::size_t>(clut.value.width_words) *
            clut.value.height;
        const auto expected_bytes =
            color_count * sizeof(std::uint16_t);
        if (clut.value.length != 12u + expected_bytes) {
            return Result<std::vector<TimDecodedImage>>::failure(
                ErrorCode::invalid_installation,
                "TIM CLUT block size does not match its dimensions");
        }

        const std::size_t colors_per_palette =
            mode == 0u ? 16u : 256u;
        if (color_count == 0u ||
            color_count % colors_per_palette != 0u) {
            return Result<std::vector<TimDecodedImage>>::failure(
                ErrorCode::invalid_installation,
                "TIM CLUT does not contain whole palettes");
        }

        const auto* color_bytes =
            bytes.data() + cursor + 12u;
        const auto palette_count =
            color_count / colors_per_palette;
        palettes.resize(palette_count);
        for (std::size_t palette = 0u;
             palette < palette_count;
             ++palette) {
            auto& colors = palettes[palette];
            colors.resize(colors_per_palette);
            for (std::size_t color = 0u;
                 color < colors_per_palette;
                 ++color) {
                const auto index =
                    palette * colors_per_palette + color;
                colors[color] =
                    le16(color_bytes + index * 2u);
            }
        }
        cursor += clut.value.length;
    }

    const auto image = read_block_header(bytes, cursor);
    if (!image) {
        return Result<std::vector<TimDecodedImage>>::failure(
            image.error, image.detail);
    }
    const auto raw_offset = cursor + 12u;
    const auto raw_size =
        static_cast<std::size_t>(image.value.length - 12u);
    const auto expected_raw =
        static_cast<std::size_t>(image.value.width_words) *
        2u *
        image.value.height;
    if (raw_size != expected_raw) {
        return Result<std::vector<TimDecodedImage>>::failure(
            ErrorCode::invalid_installation,
            "TIM image block size does not match its dimensions");
    }

    std::uint32_t width{};
    switch (mode) {
        case 0u:
            width =
                static_cast<std::uint32_t>(
                    image.value.width_words) * 4u;
            break;
        case 1u:
            width =
                static_cast<std::uint32_t>(
                    image.value.width_words) * 2u;
            break;
        case 2u:
            width = image.value.width_words;
            break;
        case 3u:
            width =
                static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(
                        image.value.width_words) * 2u) /
                    3u);
            break;
        default:
            break;
    }
    if (width == 0u || image.value.height == 0u) {
        return Result<std::vector<TimDecodedImage>>::failure(
            ErrorCode::invalid_installation,
            "TIM image dimensions are empty");
    }

    const auto variant_count =
        has_clut ? palettes.size() : 1u;
    std::vector<TimDecodedImage> decoded;
    decoded.reserve(variant_count);

    for (std::size_t variant = 0u;
         variant < variant_count;
         ++variant) {
        TimDecodedImage output{};
        output.source_offset = source_offset;
        output.flags = flags;
        output.palette_index =
            static_cast<std::uint32_t>(variant);
        output.palette_count =
            static_cast<std::uint32_t>(variant_count);
        output.width = width;
        output.height = image.value.height;
        output.vram_x = image.value.x;
        output.vram_y = image.value.y;

        const auto pixel_count =
            static_cast<std::size_t>(width) *
            output.height;
        output.rgba8.resize(pixel_count);
        const auto* raw = bytes.data() + raw_offset;

        if (mode == 0u) {
            const auto& palette = palettes[variant];
            std::size_t destination = 0u;
            for (std::size_t i = 0u;
                 i < raw_size &&
                 destination < pixel_count;
                 ++i) {
                const std::array<std::uint8_t, 2> indices{
                    static_cast<std::uint8_t>(
                        raw[i] & 0x0Fu),
                    static_cast<std::uint8_t>(
                        raw[i] >> 4u),
                };
                for (const auto index : indices) {
                    if (destination >= pixel_count) break;
                    output.rgba8[destination++] =
                        bgr555_to_rgba(
                            palette[index],
                            output.has_semitransparent_pixels);
                }
            }
        } else if (mode == 1u) {
            const auto& palette = palettes[variant];
            if (raw_size < pixel_count) {
                return Result<std::vector<TimDecodedImage>>::failure(
                    ErrorCode::invalid_installation,
                    "TIM 8bpp image data is truncated");
            }
            for (std::size_t i = 0u;
                 i < pixel_count;
                 ++i) {
                output.rgba8[i] =
                    bgr555_to_rgba(
                        palette[raw[i]],
                        output.has_semitransparent_pixels);
            }
        } else if (mode == 2u) {
            if (raw_size < pixel_count * 2u) {
                return Result<std::vector<TimDecodedImage>>::failure(
                    ErrorCode::invalid_installation,
                    "TIM 16bpp image data is truncated");
            }
            for (std::size_t i = 0u;
                 i < pixel_count;
                 ++i) {
                output.rgba8[i] =
                    bgr555_to_rgba(
                        le16(raw + i * 2u),
                        output.has_semitransparent_pixels);
            }
        } else {
            const auto row_bytes =
                static_cast<std::size_t>(
                    image.value.width_words) * 2u;
            for (std::uint32_t y = 0u;
                 y < output.height;
                 ++y) {
                const auto* row =
                    raw + static_cast<std::size_t>(y) *
                        row_bytes;
                for (std::uint32_t x = 0u;
                     x < width;
                     ++x) {
                    const auto source =
                        static_cast<std::size_t>(x) * 3u;
                    if (source + 2u >= row_bytes) break;
                    const auto destination =
                        static_cast<std::size_t>(y) *
                            width + x;
                    output.rgba8[destination] =
                        static_cast<std::uint32_t>(
                            row[source + 0u]) |
                        (static_cast<std::uint32_t>(
                            row[source + 1u]) << 8u) |
                        (static_cast<std::uint32_t>(
                            row[source + 2u]) << 16u) |
                        0xFF000000u;
                }
            }
        }

        decoded.push_back(std::move(output));
    }

    return Result<std::vector<TimDecodedImage>>::success(
        std::move(decoded));
}

} // namespace

Result<std::vector<TimDecodedImage>>
decode_sector_aligned_tim_images(
    std::span<const std::uint8_t> bytes) {
    std::vector<TimDecodedImage> result;
    if (bytes.size() < 8u) {
        return Result<std::vector<TimDecodedImage>>::success(
            std::move(result));
    }

    for (std::size_t offset = 0u;
         offset + 8u <= bytes.size();
         offset += kSectorSize) {
        if (le32(bytes.data() + offset) != kTimMagic) {
            continue;
        }

        const auto flags =
            le32(bytes.data() + offset + 4u);
        if (!valid_tim_flags(flags)) {
            continue;
        }

        const auto decoded = decode_tim_at(bytes, offset);
        if (!decoded) {
            return Result<std::vector<TimDecodedImage>>::failure(
                decoded.error,
                "TIM at sector-aligned offset " +
                    std::to_string(offset) + ": " +
                    decoded.detail);
        }

        result.insert(
            result.end(),
            decoded.value.begin(),
            decoded.value.end());
    }

    return Result<std::vector<TimDecodedImage>>::success(
        std::move(result));
}

} // namespace jojo::content
