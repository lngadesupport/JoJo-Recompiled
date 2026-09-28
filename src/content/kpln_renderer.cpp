#include "content/kpln_renderer.h"

#include <algorithm>
#include <limits>

namespace jojo::content {
namespace {

std::uint8_t expand5(std::uint16_t value) noexcept {
    value &= 0x1Fu;
    return static_cast<std::uint8_t>(
        (value << 3u) | (value >> 2u));
}

std::uint32_t bgr555_to_rgba(
    std::uint16_t value) noexcept {
    const auto rgb =
        static_cast<std::uint16_t>(value & 0x7FFFu);
    const auto red = expand5(value);
    const auto green = expand5(value >> 5u);
    const auto blue = expand5(value >> 10u);
    const auto alpha =
        static_cast<std::uint8_t>(rgb == 0u ? 0u : 255u);
    return static_cast<std::uint32_t>(red) |
        (static_cast<std::uint32_t>(green) << 8u) |
        (static_cast<std::uint32_t>(blue) << 16u) |
        (static_cast<std::uint32_t>(alpha) << 24u);
}

std::uint32_t descriptor_clut_selector(
    const KplnCachedDescriptor& descriptor,
    std::uint32_t clut_base,
    std::uint32_t clut_mode,
    std::uint32_t render_mode) noexcept {
    if (clut_mode > 0u) {
        return clut_base;
    }
    const auto relative =
        render_mode < 4u
        ? static_cast<std::uint32_t>(
            descriptor.clut_selector)
        : (descriptor.raw >> 24u);
    return clut_base + relative;
}

std::uint32_t descriptor_transform(
    const KplnCachedDescriptor& descriptor,
    std::uint32_t render_mode,
    std::uint32_t orientation) noexcept {
    const auto native =
        static_cast<std::uint32_t>(descriptor.raw >> 30u);
    if (render_mode == 4u ||
        render_mode == 5u ||
        render_mode == 0x87u) {
        return orientation & 3u;
    }
    return (native ^ orientation) & 3u;
}

} // namespace

Result<KplnRenderedFrame> render_kpln_cached_frame(
    const KplnCachedFrame& frame,
    const KplnClutWindow& clut,
    std::uint32_t side,
    std::uint32_t clut_base,
    std::uint32_t clut_mode,
    std::uint32_t render_mode,
    std::uint32_t orientation) {
    if (side > 1u || orientation > 3u) {
        return Result<KplnRenderedFrame>::failure(
            ErrorCode::invalid_argument,
            "KPLN render side/orientation is outside the retail range");
    }
    if (frame.parts.empty()) {
        return Result<KplnRenderedFrame>::failure(
            ErrorCode::invalid_argument,
            "KPLN cached frame has no parts");
    }

    std::int32_t min_x =
        std::numeric_limits<std::int32_t>::max();
    std::int32_t min_y =
        std::numeric_limits<std::int32_t>::max();
    std::int32_t max_x =
        std::numeric_limits<std::int32_t>::min();
    std::int32_t max_y =
        std::numeric_limits<std::int32_t>::min();

    for (const auto& part : frame.parts) {
        const auto x =
            -static_cast<std::int32_t>(
                part.header.x_offset);
        const auto y =
            -static_cast<std::int32_t>(
                part.header.y_offset);
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(
            max_x,
            x + static_cast<std::int32_t>(
                part.header.columns) * 16);
        max_y = std::max(
            max_y,
            y + static_cast<std::int32_t>(
                part.header.rows) * 16);
    }

    if (max_x <= min_x || max_y <= min_y) {
        return Result<KplnRenderedFrame>::failure(
            ErrorCode::invalid_installation,
            "KPLN cached frame has invalid bounds");
    }

    const auto width =
        static_cast<std::uint32_t>(max_x - min_x);
    const auto height =
        static_cast<std::uint32_t>(max_y - min_y);
    const auto pixel_count =
        static_cast<std::uint64_t>(width) * height;
    if (pixel_count >
        std::numeric_limits<std::size_t>::max()) {
        return Result<KplnRenderedFrame>::failure(
            ErrorCode::unsupported_format,
            "KPLN rendered frame is too large");
    }

    KplnRenderedFrame output{};
    output.width = width;
    output.height = height;
    output.origin_x = min_x;
    output.origin_y = min_y;
    output.rgba8.assign(
        static_cast<std::size_t>(pixel_count),
        0u);

    const auto clut_row_base =
        0x1e8u + side;

    for (const auto& part : frame.parts) {
        const auto part_x =
            -static_cast<std::int32_t>(
                part.header.x_offset) -
            min_x;
        const auto part_y =
            -static_cast<std::int32_t>(
                part.header.y_offset) -
            min_y;

        for (const auto& cell : part.cells) {
            if (!cell.visible ||
                !cell.has_descriptor ||
                cell.descriptor.tile_indices.size() != 256u) {
                continue;
            }

            const auto selector =
                descriptor_clut_selector(
                    cell.descriptor,
                    clut_base,
                    clut_mode,
                    render_mode);
            const auto transform =
                descriptor_transform(
                    cell.descriptor,
                    render_mode,
                    orientation);
            const bool flip_y =
                (transform & 1u) != 0u;
            const bool flip_x =
                (transform & 2u) != 0u;

            const auto dst_x =
                part_x +
                static_cast<std::int32_t>(
                    cell.column) * 16;
            const auto dst_y =
                part_y +
                static_cast<std::int32_t>(
                    cell.row) * 16;

            for (std::uint32_t y = 0u; y < 16u; ++y) {
                const auto source_y =
                    flip_y ? 15u - y : y;
                const auto target_y =
                    dst_y + static_cast<std::int32_t>(y);
                if (target_y < 0 ||
                    target_y >=
                        static_cast<std::int32_t>(height)) {
                    continue;
                }

                for (std::uint32_t x = 0u;
                     x < 16u;
                     ++x) {
                    const auto source_x =
                        flip_x ? 15u - x : x;
                    const auto target_x =
                        dst_x +
                        static_cast<std::int32_t>(x);
                    if (target_x < 0 ||
                        target_x >=
                            static_cast<std::int32_t>(width)) {
                        continue;
                    }

                    const auto pixel_index =
                        cell.descriptor.tile_indices[
                            static_cast<std::size_t>(
                                source_y) * 16u +
                            source_x];
                    if (pixel_index == 0u) {
                        continue;
                    }

                    const auto raw_color =
                        sample_kpln_clut(
                            clut,
                            clut_row_base,
                            selector,
                            pixel_index);
                    const auto rgba =
                        bgr555_to_rgba(raw_color);
                    if ((rgba >> 24u) == 0u) {
                        continue;
                    }

                    output.rgba8[
                        static_cast<std::size_t>(
                            target_y) * width +
                        static_cast<std::size_t>(
                            target_x)] = rgba;
                }
            }
        }
    }

    return Result<KplnRenderedFrame>::success(
        std::move(output));
}

} // namespace jojo::content
