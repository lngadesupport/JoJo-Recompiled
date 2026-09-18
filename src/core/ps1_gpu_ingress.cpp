#include "core/ps1_gpu_ingress.h"

#include <algorithm>

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_bool(std::uint64_t& hash, bool value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value ? 1u : 0u));
}

void hash_u16(std::uint64_t& hash, std::uint16_t value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value));
    hash_byte(hash, static_cast<std::uint8_t>(value >> 8u));
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0u; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint32_t normalize_transfer_width(std::uint32_t raw) noexcept {
    return ((raw - 1u) & 0x3FFu) + 1u;
}

std::uint32_t normalize_transfer_height(std::uint32_t raw) noexcept {
    return ((raw - 1u) & 0x1FFu) + 1u;
}

std::int32_t sign_extend11(std::uint32_t value) noexcept {
    value &= 0x7FFu;
    return (value & 0x400u) != 0u
        ? static_cast<std::int32_t>(value | 0xFFFFF800u)
        : static_cast<std::int32_t>(value);
}

std::int32_t sign_extend16_coord(std::uint32_t value) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

std::uint32_t apply_texture_window_axis(
    std::uint32_t coordinate,
    std::uint8_t mask,
    std::uint8_t offset) noexcept {
    const auto expanded_mask = static_cast<std::uint32_t>(mask) << 3u;
    const auto expanded_offset =
        static_cast<std::uint32_t>(offset & mask) << 3u;
    return ((coordinate & ~expanded_mask) | expanded_offset) & 0xFFu;
}

std::uint16_t color24_to_bgr555(std::uint32_t value) noexcept {
    const auto red = static_cast<std::uint16_t>((value >> 3u) & 0x1Fu);
    const auto green = static_cast<std::uint16_t>((value >> 11u) & 0x1Fu);
    const auto blue = static_cast<std::uint16_t>((value >> 19u) & 0x1Fu);
    return static_cast<std::uint16_t>(red | (green << 5u) | (blue << 10u));
}

std::uint16_t modulate_bgr555(std::uint16_t texel, std::uint32_t color24) noexcept {
    const auto modulate = [](std::uint32_t component5, std::uint32_t color8) {
        return std::min<std::uint32_t>(
            31u,
            (component5 * color8 + 64u) / 128u);
    };
    const auto red = modulate(texel & 0x1Fu, color24 & 0xFFu);
    const auto green = modulate((texel >> 5u) & 0x1Fu, (color24 >> 8u) & 0xFFu);
    const auto blue = modulate((texel >> 10u) & 0x1Fu, (color24 >> 16u) & 0xFFu);
    return static_cast<std::uint16_t>(
        red | (green << 5u) | (blue << 10u) | (texel & 0x8000u));
}

std::uint32_t display_width_from_mode(std::uint32_t parameter) noexcept {
    if ((parameter & (1u << 6u)) != 0u) return 368u;
    switch (parameter & 3u) {
        case 0u: return 256u;
        case 1u: return 320u;
        case 2u: return 512u;
        default: return 640u;
    }
}

} // namespace

void Ps1GpuIngress::reset_command_buffer() noexcept {
    gp0_mode_ = Gp0Mode::command;
    polygon_words_.clear();
    polygon_words_expected_ = 0u;
    fill_color_ = 0u;
    fill_x_ = 0u;
    fill_y_ = 0u;
    draw_color_ = 0u;
    draw_x_ = 0;
    draw_y_ = 0;
    texture_fixed_width_ = 0u;
    texture_fixed_height_ = 0u;
    texture_modulation_color_ = 0x00808080u;
    texture_raw_ = true;
    copy_source_x_ = 0u;
    copy_source_y_ = 0u;
    copy_destination_x_ = 0u;
    copy_destination_y_ = 0u;
    transfer_x_ = 0u;
    transfer_y_ = 0u;
    transfer_width_ = 0u;
    transfer_height_ = 0u;
    transfer_pixel_index_ = 0u;
    transfer_pixels_remaining_ = 0u;
}

void Ps1GpuIngress::reset_display_state() noexcept {
    display_ = {};
}

void Ps1GpuIngress::apply_display_mode(std::uint32_t parameter) noexcept {
    display_.width = display_width_from_mode(parameter);
    const bool vertical_480 = (parameter & (1u << 2u)) != 0u;
    display_.pal = (parameter & (1u << 3u)) != 0u;
    display_.interlaced = (parameter & (1u << 5u)) != 0u;
    display_.height = vertical_480 && display_.interlaced ? 480u : 240u;
    display_.rgb24 = (parameter & (1u << 4u)) != 0u;

    constexpr std::uint32_t display_mode_status_mask =
        (1u << 14u) |
        (1u << 16u) |
        (3u << 17u) |
        (1u << 19u) |
        (1u << 20u) |
        (1u << 21u) |
        (1u << 22u);
    status_ &= ~display_mode_status_mask;
    status_ |= ((parameter >> 7u) & 1u) << 14u;
    status_ |= ((parameter >> 6u) & 1u) << 16u;
    status_ |= (parameter & 3u) << 17u;
    status_ |= ((parameter >> 2u) & 1u) << 19u;
    status_ |= ((parameter >> 3u) & 1u) << 20u;
    status_ |= ((parameter >> 4u) & 1u) << 21u;
    status_ |= ((parameter >> 5u) & 1u) << 22u;
}

void Ps1GpuIngress::write_transfer_pixel(std::uint16_t pixel) noexcept {
    if (transfer_pixels_remaining_ == 0u || transfer_width_ == 0u) return;

    const auto local_x = transfer_pixel_index_ % transfer_width_;
    const auto local_y = transfer_pixel_index_ / transfer_width_;
    const auto x = (transfer_x_ + local_x) & (vram_width - 1u);
    const auto y = (transfer_y_ + local_y) & (vram_height - 1u);
    vram_[static_cast<std::size_t>(y) * vram_width + x] = pixel;
    ++vram_write_count_;
    ++transfer_pixel_index_;
    --transfer_pixels_remaining_;

    if (transfer_pixels_remaining_ == 0u) reset_command_buffer();
}

void Ps1GpuIngress::fill_rectangle(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0u || height == 0u) return;
    for (std::uint32_t local_y = 0u; local_y < height; ++local_y) {
        const auto y = (fill_y_ + local_y) & (vram_height - 1u);
        for (std::uint32_t local_x = 0u; local_x < width; ++local_x) {
            const auto x = (fill_x_ + local_x) & (vram_width - 1u);
            vram_[static_cast<std::size_t>(y) * vram_width + x] = fill_color_;
            ++vram_write_count_;
        }
    }
}

void Ps1GpuIngress::draw_monochrome_rectangle(
    std::uint32_t width,
    std::uint32_t height) noexcept {
    for (std::uint32_t local_y = 0u; local_y < height; ++local_y) {
        const auto y = draw_y_ + draw_offset_y_ + static_cast<std::int32_t>(local_y);
        if (y < 0 || y >= static_cast<std::int32_t>(vram_height) ||
            y < static_cast<std::int32_t>(draw_area_top_) ||
            y > static_cast<std::int32_t>(draw_area_bottom_)) {
            continue;
        }
        for (std::uint32_t local_x = 0u; local_x < width; ++local_x) {
            const auto x = draw_x_ + draw_offset_x_ + static_cast<std::int32_t>(local_x);
            if (x < 0 || x >= static_cast<std::int32_t>(vram_width) ||
                x < static_cast<std::int32_t>(draw_area_left_) ||
                x > static_cast<std::int32_t>(draw_area_right_)) {
                continue;
            }
            vram_[static_cast<std::size_t>(y) * vram_width +
                  static_cast<std::uint32_t>(x)] = draw_color_;
            ++vram_write_count_;
        }
    }
}

std::uint16_t Ps1GpuIngress::sample_raw_texture(
    std::uint32_t u,
    std::uint32_t v) const noexcept {
    u = apply_texture_window_axis(
        u,
        texture_window_mask_x_,
        texture_window_offset_x_);
    v = apply_texture_window_axis(
        v,
        texture_window_mask_y_,
        texture_window_offset_y_);
    const auto source_y = (texture_page_y_ + v) & (vram_height - 1u);

    switch (texture_depth_) {
        case 0u: { // 4bpp indexed
            const auto packed_x =
                (texture_page_x_ + (u >> 2u)) & (vram_width - 1u);
            const auto packed =
                vram_[static_cast<std::size_t>(source_y) * vram_width + packed_x];
            const auto index =
                static_cast<std::uint8_t>((packed >> ((u & 3u) * 4u)) & 0xFu);
            if (index == 0u) return 0u;
            const auto clut_x =
                (texture_clut_x_ + index) & (vram_width - 1u);
            const auto clut_y = texture_clut_y_ & (vram_height - 1u);
            return vram_[static_cast<std::size_t>(clut_y) * vram_width + clut_x];
        }
        case 1u: { // 8bpp indexed
            const auto packed_x =
                (texture_page_x_ + (u >> 1u)) & (vram_width - 1u);
            const auto packed =
                vram_[static_cast<std::size_t>(source_y) * vram_width + packed_x];
            const auto index =
                static_cast<std::uint8_t>((packed >> ((u & 1u) * 8u)) & 0xFFu);
            if (index == 0u) return 0u;
            const auto clut_x =
                (texture_clut_x_ + index) & (vram_width - 1u);
            const auto clut_y = texture_clut_y_ & (vram_height - 1u);
            return vram_[static_cast<std::size_t>(clut_y) * vram_width + clut_x];
        }
        case 2u: { // 15bpp direct
            const auto source_x =
                (texture_page_x_ + u) & (vram_width - 1u);
            return vram_[static_cast<std::size_t>(source_y) * vram_width + source_x];
        }
        default:
            return 0u;
    }
}

void Ps1GpuIngress::draw_textured_rectangle(
    std::uint32_t width,
    std::uint32_t height) noexcept {
    for (std::uint32_t local_y = 0u; local_y < height; ++local_y) {
        const auto y = draw_y_ + draw_offset_y_ + static_cast<std::int32_t>(local_y);
        if (y < 0 || y >= static_cast<std::int32_t>(vram_height) ||
            y < static_cast<std::int32_t>(draw_area_top_) ||
            y > static_cast<std::int32_t>(draw_area_bottom_)) {
            continue;
        }

        for (std::uint32_t local_x = 0u; local_x < width; ++local_x) {
            const auto x = draw_x_ + draw_offset_x_ + static_cast<std::int32_t>(local_x);
            if (x < 0 || x >= static_cast<std::int32_t>(vram_width) ||
                x < static_cast<std::int32_t>(draw_area_left_) ||
                x > static_cast<std::int32_t>(draw_area_right_)) {
                continue;
            }

            const auto texture_x = texture_x_flip_
                ? static_cast<std::uint32_t>(texture_u_) + (width - 1u - local_x)
                : static_cast<std::uint32_t>(texture_u_) + local_x;
            const auto texture_y = texture_y_flip_
                ? static_cast<std::uint32_t>(texture_v_) + (height - 1u - local_y)
                : static_cast<std::uint32_t>(texture_v_) + local_y;
            const auto texel = sample_raw_texture(texture_x, texture_y);
            if (texel == 0u) continue;

            const auto pixel = texture_raw_
                ? texel
                : modulate_bgr555(texel, texture_modulation_color_);
            vram_[static_cast<std::size_t>(y) * vram_width +
                  static_cast<std::uint32_t>(x)] = pixel;
            ++vram_write_count_;
        }
    }
}


void Ps1GpuIngress::begin_polygon(std::uint32_t command_word) noexcept {
    const auto command = static_cast<std::uint8_t>(command_word >> 24u);
    const bool gouraud = (command & 0x10u) != 0u;
    const bool quad = (command & 0x08u) != 0u;
    const bool textured = (command & 0x04u) != 0u;
    const std::size_t vertices = quad ? 4u : 3u;

    polygon_words_.clear();
    polygon_words_.reserve(
        1u + vertices * (1u + (textured ? 1u : 0u)) +
        (gouraud ? vertices - 1u : 0u));
    polygon_words_.push_back(command_word);
    polygon_words_expected_ =
        1u + vertices * (1u + (textured ? 1u : 0u)) +
        (gouraud ? vertices - 1u : 0u);
    gp0_mode_ = Gp0Mode::polygon_payload;
}

void Ps1GpuIngress::rasterize_triangle(
    const PolygonVertex& a,
    const PolygonVertex& b,
    const PolygonVertex& c,
    bool textured,
    bool raw_texture,
    bool gouraud) noexcept {
    const auto ax = a.x + draw_offset_x_;
    const auto ay = a.y + draw_offset_y_;
    const auto bx = b.x + draw_offset_x_;
    const auto by = b.y + draw_offset_y_;
    const auto cx = c.x + draw_offset_x_;
    const auto cy = c.y + draw_offset_y_;

    const auto edge = [](std::int32_t x0, std::int32_t y0,
                         std::int32_t x1, std::int32_t y1,
                         std::int32_t px, std::int32_t py) noexcept {
        return static_cast<std::int64_t>(px - x0) * (y1 - y0) -
               static_cast<std::int64_t>(py - y0) * (x1 - x0);
    };

    const auto area = edge(ax, ay, bx, by, cx, cy);
    if (area == 0) return;

    const auto min_x = std::max<std::int32_t>(
        static_cast<std::int32_t>(draw_area_left_),
        std::max<std::int32_t>(0, std::min({ax, bx, cx})));
    const auto max_x = std::min<std::int32_t>(
        static_cast<std::int32_t>(draw_area_right_),
        std::min<std::int32_t>(
            static_cast<std::int32_t>(vram_width) - 1,
            std::max({ax, bx, cx})));
    const auto min_y = std::max<std::int32_t>(
        static_cast<std::int32_t>(draw_area_top_),
        std::max<std::int32_t>(0, std::min({ay, by, cy})));
    const auto max_y = std::min<std::int32_t>(
        static_cast<std::int32_t>(draw_area_bottom_),
        std::min<std::int32_t>(
            static_cast<std::int32_t>(vram_height) - 1,
            std::max({ay, by, cy})));
    if (min_x > max_x || min_y > max_y) return;

    const auto inside = [area](std::int64_t w0, std::int64_t w1, std::int64_t w2) noexcept {
        return area > 0
            ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
            : (w0 <= 0 && w1 <= 0 && w2 <= 0);
    };

    const auto interpolate = [area](
        std::int64_t w0,
        std::int64_t w1,
        std::int64_t w2,
        std::int32_t v0,
        std::int32_t v1,
        std::int32_t v2) noexcept {
        return static_cast<std::int32_t>(
            (w0 * v0 + w1 * v1 + w2 * v2) / area);
    };

    for (std::int32_t y = min_y; y <= max_y; ++y) {
        for (std::int32_t x = min_x; x <= max_x; ++x) {
            const auto w0 = edge(bx, by, cx, cy, x, y);
            const auto w1 = edge(cx, cy, ax, ay, x, y);
            const auto w2 = edge(ax, ay, bx, by, x, y);
            if (!inside(w0, w1, w2)) continue;

            const auto red = gouraud
                ? std::clamp(interpolate(w0, w1, w2, a.r, b.r, c.r), 0, 255)
                : static_cast<std::int32_t>(a.r);
            const auto green = gouraud
                ? std::clamp(interpolate(w0, w1, w2, a.g, b.g, c.g), 0, 255)
                : static_cast<std::int32_t>(a.g);
            const auto blue = gouraud
                ? std::clamp(interpolate(w0, w1, w2, a.b, b.b, c.b), 0, 255)
                : static_cast<std::int32_t>(a.b);

            std::uint16_t pixel = 0u;
            if (textured) {
                const auto u = static_cast<std::uint32_t>(
                    std::clamp(interpolate(w0, w1, w2, a.u, b.u, c.u), 0, 255));
                const auto v = static_cast<std::uint32_t>(
                    std::clamp(interpolate(w0, w1, w2, a.v, b.v, c.v), 0, 255));
                const auto texel = sample_raw_texture(u, v);
                if (texel == 0u) continue;

                if (raw_texture) {
                    pixel = texel;
                } else {
                    const auto modulate = [](std::uint32_t component5, std::int32_t color8) {
                        return std::min<std::uint32_t>(
                            31u,
                            (component5 * static_cast<std::uint32_t>(color8) + 64u) / 128u);
                    };
                    const auto tr = modulate(texel & 0x1Fu, red);
                    const auto tg = modulate((texel >> 5u) & 0x1Fu, green);
                    const auto tb = modulate((texel >> 10u) & 0x1Fu, blue);
                    pixel = static_cast<std::uint16_t>(
                        tr | (tg << 5u) | (tb << 10u) | (texel & 0x8000u));
                }
            } else {
                const auto packed =
                    static_cast<std::uint32_t>(red) |
                    (static_cast<std::uint32_t>(green) << 8u) |
                    (static_cast<std::uint32_t>(blue) << 16u);
                pixel = color24_to_bgr555(packed);
            }

            vram_[static_cast<std::size_t>(y) * vram_width +
                  static_cast<std::uint32_t>(x)] = pixel;
            ++vram_write_count_;
        }
    }
}

bool Ps1GpuIngress::execute_polygon_packet() noexcept {
    if (polygon_words_.empty() || polygon_words_.size() != polygon_words_expected_) {
        return false;
    }

    const auto command_word = polygon_words_[0];
    const auto command = static_cast<std::uint8_t>(command_word >> 24u);
    const bool gouraud = (command & 0x10u) != 0u;
    const bool quad = (command & 0x08u) != 0u;
    const bool textured = (command & 0x04u) != 0u;
    const bool raw_texture = (command & 0x01u) != 0u;
    const std::size_t vertex_count = quad ? 4u : 3u;

    PolygonVertex vertices[4]{};
    std::size_t word = 1u;
    std::uint32_t color = command_word & 0x00FFFFFFu;

    for (std::size_t i = 0u; i < vertex_count; ++i) {
        if (i != 0u && gouraud) {
            color = polygon_words_[word++] & 0x00FFFFFFu;
        }
        const auto xy = polygon_words_[word++];
        vertices[i].x = sign_extend16_coord(xy);
        vertices[i].y = sign_extend16_coord(xy >> 16u);
        vertices[i].r = static_cast<std::uint8_t>(color & 0xFFu);
        vertices[i].g = static_cast<std::uint8_t>((color >> 8u) & 0xFFu);
        vertices[i].b = static_cast<std::uint8_t>((color >> 16u) & 0xFFu);

        if (textured) {
            const auto uv = polygon_words_[word++];
            vertices[i].u = static_cast<std::uint8_t>(uv & 0xFFu);
            vertices[i].v = static_cast<std::uint8_t>((uv >> 8u) & 0xFFu);
            const auto attribute = static_cast<std::uint16_t>(uv >> 16u);
            if (i == 0u) {
                texture_clut_x_ = static_cast<std::uint32_t>(attribute & 0x3Fu) << 4u;
                texture_clut_y_ = static_cast<std::uint32_t>((attribute >> 6u) & 0x1FFu);
            } else if (i == 1u) {
                texture_page_x_ = static_cast<std::uint32_t>(attribute & 0x0Fu) * 64u;
                texture_page_y_ = static_cast<std::uint32_t>((attribute >> 4u) & 1u) * 256u;
                texture_depth_ = static_cast<std::uint8_t>((attribute >> 7u) & 3u);
            }
        }
    }

    if (textured && texture_depth_ > 2u) {
        last_unsupported_gp0_command_ = command;
        return false;
    }

    rasterize_triangle(
        vertices[0], vertices[1], vertices[2],
        textured, raw_texture, gouraud);
    if (quad) {
        rasterize_triangle(
            vertices[1], vertices[2], vertices[3],
            textured, raw_texture, gouraud);
    }
    return true;
}

void Ps1GpuIngress::copy_vram_rectangle(
    std::uint32_t width,
    std::uint32_t height) noexcept {
    if (width == 0u || height == 0u) return;

    std::vector<std::uint16_t> source;
    source.reserve(static_cast<std::size_t>(width) * height);
    for (std::uint32_t local_y = 0u; local_y < height; ++local_y) {
        const auto y = (copy_source_y_ + local_y) & (vram_height - 1u);
        for (std::uint32_t local_x = 0u; local_x < width; ++local_x) {
            const auto x = (copy_source_x_ + local_x) & (vram_width - 1u);
            source.push_back(vram_[static_cast<std::size_t>(y) * vram_width + x]);
        }
    }

    std::size_t index = 0u;
    for (std::uint32_t local_y = 0u; local_y < height; ++local_y) {
        const auto y = (copy_destination_y_ + local_y) & (vram_height - 1u);
        for (std::uint32_t local_x = 0u; local_x < width; ++local_x) {
            const auto x = (copy_destination_x_ + local_x) & (vram_width - 1u);
            vram_[static_cast<std::size_t>(y) * vram_width + x] = source[index++];
            ++vram_write_count_;
        }
    }
}

R3000aBusResult Ps1GpuIngress::read_gp0() noexcept {
    if (readback_pixels_remaining_ == 0u || readback_width_ == 0u) {
        return {R3000aBusStatus::ok, gpuread_latch_};
    }

    const auto read_pixel = [&]() noexcept -> std::uint16_t {
        if (readback_pixels_remaining_ == 0u) return 0u;
        const auto local_x = readback_pixel_index_ % readback_width_;
        const auto local_y = readback_pixel_index_ / readback_width_;
        const auto x = (readback_x_ + local_x) & (vram_width - 1u);
        const auto y = (readback_y_ + local_y) & (vram_height - 1u);
        const auto pixel = vram_[static_cast<std::size_t>(y) * vram_width + x];
        ++readback_pixel_index_;
        --readback_pixels_remaining_;
        return pixel;
    };

    const auto low = static_cast<std::uint32_t>(read_pixel());
    const auto high = readback_pixels_remaining_ != 0u
        ? static_cast<std::uint32_t>(read_pixel())
        : 0u;
    return {R3000aBusStatus::ok, low | (high << 16u)};
}

R3000aBusResult Ps1GpuIngress::write_gp0(std::uint32_t value) noexcept {
    last_unsupported_gp0_command_.reset();

    switch (gp0_mode_) {
        case Gp0Mode::polygon_payload: {
            ++gp0_word_count_;
            polygon_words_.push_back(value);
            if (polygon_words_.size() < polygon_words_expected_) {
                return {R3000aBusStatus::ok, 0u};
            }
            const auto command = polygon_words_.empty()
                ? std::uint8_t{0}
                : static_cast<std::uint8_t>(polygon_words_[0] >> 24u);
            const bool ok = execute_polygon_packet();
            reset_command_buffer();
            if (!ok) {
                last_unsupported_gp0_command_ = command;
                return {R3000aBusStatus::unsupported, 0u};
            }
            return {R3000aBusStatus::ok, 0u};
        }
        case Gp0Mode::fill_rectangle_position:
            ++gp0_word_count_;
            fill_x_ = value & 0x3FFu;
            fill_y_ = (value >> 16u) & 0x1FFu;
            gp0_mode_ = Gp0Mode::fill_rectangle_size;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::fill_rectangle_size: {
            ++gp0_word_count_;
            const auto width = value & 0x3FFu;
            const auto height = (value >> 16u) & 0x1FFu;
            fill_rectangle(width, height);
            reset_command_buffer();
            return {R3000aBusStatus::ok, 0u};
        }
        case Gp0Mode::monochrome_rectangle_position:
            ++gp0_word_count_;
            draw_x_ = sign_extend16_coord(value);
            draw_y_ = sign_extend16_coord(value >> 16u);
            gp0_mode_ = Gp0Mode::monochrome_rectangle_size;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::monochrome_rectangle_size: {
            ++gp0_word_count_;
            const auto width = value & 0xFFFFu;
            const auto height = (value >> 16u) & 0xFFFFu;
            draw_monochrome_rectangle(width, height);
            reset_command_buffer();
            return {R3000aBusStatus::ok, 0u};
        }
        case Gp0Mode::textured_rectangle_position:
            ++gp0_word_count_;
            draw_x_ = sign_extend16_coord(value);
            draw_y_ = sign_extend16_coord(value >> 16u);
            gp0_mode_ = Gp0Mode::textured_rectangle_uv;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::textured_rectangle_uv: {
            ++gp0_word_count_;
            texture_u_ = static_cast<std::uint8_t>(value & 0xFFu);
            texture_v_ = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
            const auto clut = static_cast<std::uint16_t>(value >> 16u);
            texture_clut_x_ = static_cast<std::uint32_t>(clut & 0x3Fu) << 4u;
            texture_clut_y_ = static_cast<std::uint32_t>((clut >> 6u) & 0x1FFu);
            if (texture_fixed_width_ != 0u && texture_fixed_height_ != 0u) {
                draw_textured_rectangle(texture_fixed_width_, texture_fixed_height_);
                reset_command_buffer();
            } else {
                gp0_mode_ = Gp0Mode::textured_rectangle_size;
            }
            return {R3000aBusStatus::ok, 0u};
        }
        case Gp0Mode::textured_rectangle_size: {
            ++gp0_word_count_;
            const auto width = value & 0xFFFFu;
            const auto height = (value >> 16u) & 0xFFFFu;
            draw_textured_rectangle(width, height);
            reset_command_buffer();
            return {R3000aBusStatus::ok, 0u};
        }
        case Gp0Mode::vram_copy_source:
            ++gp0_word_count_;
            copy_source_x_ = value & 0x3FFu;
            copy_source_y_ = (value >> 16u) & 0x1FFu;
            gp0_mode_ = Gp0Mode::vram_copy_destination;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::vram_copy_destination:
            ++gp0_word_count_;
            copy_destination_x_ = value & 0x3FFu;
            copy_destination_y_ = (value >> 16u) & 0x1FFu;
            gp0_mode_ = Gp0Mode::vram_copy_size;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::vram_copy_size: {
            ++gp0_word_count_;
            const auto width = normalize_transfer_width(value & 0xFFFFu);
            const auto height = normalize_transfer_height((value >> 16u) & 0xFFFFu);
            copy_vram_rectangle(width, height);
            reset_command_buffer();
            return {R3000aBusStatus::ok, 0u};
        }
        case Gp0Mode::vram_to_cpu_source:
            ++gp0_word_count_;
            readback_x_ = value & 0x3FFu;
            readback_y_ = (value >> 16u) & 0x1FFu;
            gp0_mode_ = Gp0Mode::vram_to_cpu_size;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::vram_to_cpu_size:
            ++gp0_word_count_;
            readback_width_ = normalize_transfer_width(value & 0xFFFFu);
            readback_height_ = normalize_transfer_height((value >> 16u) & 0xFFFFu);
            readback_pixel_index_ = 0u;
            readback_pixels_remaining_ = readback_width_ * readback_height_;
            gp0_mode_ = Gp0Mode::command;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::cpu_to_vram_destination:
            ++gp0_word_count_;
            transfer_x_ = value & 0x3FFu;
            transfer_y_ = (value >> 16u) & 0x1FFu;
            gp0_mode_ = Gp0Mode::cpu_to_vram_size;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::cpu_to_vram_size:
            ++gp0_word_count_;
            transfer_width_ = normalize_transfer_width(value & 0xFFFFu);
            transfer_height_ = normalize_transfer_height((value >> 16u) & 0xFFFFu);
            transfer_pixel_index_ = 0u;
            transfer_pixels_remaining_ = transfer_width_ * transfer_height_;
            gp0_mode_ = Gp0Mode::cpu_to_vram_payload;
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::cpu_to_vram_payload:
            ++gp0_word_count_;
            write_transfer_pixel(static_cast<std::uint16_t>(value & 0xFFFFu));
            if (transfer_pixels_remaining_ != 0u) {
                write_transfer_pixel(static_cast<std::uint16_t>(value >> 16u));
            }
            return {R3000aBusStatus::ok, 0u};
        case Gp0Mode::command:
            break;
    }

    const auto command = static_cast<std::uint8_t>(value >> 24u);
    if ((command & 0xE0u) == 0x20u) {
        ++gp0_word_count_;
        begin_polygon(value);
        return {R3000aBusStatus::ok, 0u};
    }

    switch (command) {
        case 0x00u: // NOP
        case 0x01u: // Clear cache
        case 0x1Fu: // IRQ request/control-side event
        case 0xE1u: // Draw mode / texture page
            ++gp0_word_count_;
            texture_page_x_ = (value & 0x0Fu) * 64u;
            texture_page_y_ = ((value >> 4u) & 1u) * 256u;
            texture_depth_ = static_cast<std::uint8_t>((value >> 7u) & 3u);
            texture_x_flip_ = (value & (1u << 12u)) != 0u;
            texture_y_flip_ = (value & (1u << 13u)) != 0u;
            return {R3000aBusStatus::ok, 0u};
        case 0xE2u: // Texture window
            texture_window_mask_x_ = static_cast<std::uint8_t>(value & 0x1Fu);
            texture_window_mask_y_ = static_cast<std::uint8_t>((value >> 5u) & 0x1Fu);
            texture_window_offset_x_ = static_cast<std::uint8_t>((value >> 10u) & 0x1Fu);
            texture_window_offset_y_ = static_cast<std::uint8_t>((value >> 15u) & 0x1Fu);
            ++gp0_word_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0xE6u:
            ++gp0_word_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0xE3u:
            draw_area_left_ = value & 0x3FFu;
            draw_area_top_ = (value >> 10u) & 0x1FFu;
            ++gp0_word_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0xE4u:
            draw_area_right_ = value & 0x3FFu;
            draw_area_bottom_ = (value >> 10u) & 0x1FFu;
            ++gp0_word_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0xE5u:
            draw_offset_x_ = sign_extend11(value);
            draw_offset_y_ = sign_extend11(value >> 11u);
            ++gp0_word_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x02u: // Fill rectangle in VRAM
            ++gp0_word_count_;
            fill_color_ = color24_to_bgr555(value & 0x00FFFFFFu);
            gp0_mode_ = Gp0Mode::fill_rectangle_position;
            return {R3000aBusStatus::ok, 0u};
        case 0x60u:
        case 0x61u:
        case 0x62u:
        case 0x63u: // Monochrome variable-size rectangle
            ++gp0_word_count_;
            draw_color_ = color24_to_bgr555(value & 0x00FFFFFFu);
            gp0_mode_ = Gp0Mode::monochrome_rectangle_position;
            return {R3000aBusStatus::ok, 0u};
        case 0x64u: // Modulated textured variable rectangle
        case 0x65u: // Raw-textured variable rectangle
            if (texture_depth_ > 2u) {
                last_unsupported_gp0_command_ = command;
                return {R3000aBusStatus::unsupported, 0u};
            }
            texture_raw_ = (command & 1u) != 0u;
            texture_modulation_color_ = value & 0x00FFFFFFu;
            texture_fixed_width_ = 0u;
            texture_fixed_height_ = 0u;
            ++gp0_word_count_;
            gp0_mode_ = Gp0Mode::textured_rectangle_position;
            return {R3000aBusStatus::ok, 0u};
        case 0x6Cu: // Modulated textured 1x1 rectangle
        case 0x6Du: // Raw-textured 1x1 rectangle
        case 0x74u: // Modulated textured 8x8 rectangle
        case 0x75u: // Raw-textured 8x8 rectangle
        case 0x7Cu: // Modulated textured 16x16 rectangle
        case 0x7Du: // Raw-textured 16x16 rectangle
            if (texture_depth_ > 2u) {
                last_unsupported_gp0_command_ = command;
                return {R3000aBusStatus::unsupported, 0u};
            }
            texture_raw_ = (command & 1u) != 0u;
            texture_modulation_color_ = value & 0x00FFFFFFu;
            if ((command & 0xF8u) == 0x68u) {
                texture_fixed_width_ = 1u;
                texture_fixed_height_ = 1u;
            } else if ((command & 0xF8u) == 0x70u) {
                texture_fixed_width_ = 8u;
                texture_fixed_height_ = 8u;
            } else {
                texture_fixed_width_ = 16u;
                texture_fixed_height_ = 16u;
            }
            ++gp0_word_count_;
            gp0_mode_ = Gp0Mode::textured_rectangle_position;
            return {R3000aBusStatus::ok, 0u};
        case 0x80u: // VRAM -> VRAM rectangle copy
            ++gp0_word_count_;
            gp0_mode_ = Gp0Mode::vram_copy_source;
            return {R3000aBusStatus::ok, 0u};
        case 0xA0u: // CPU -> VRAM image load
            ++gp0_word_count_;
            gp0_mode_ = Gp0Mode::cpu_to_vram_destination;
            return {R3000aBusStatus::ok, 0u};
        case 0xC0u: // VRAM -> CPU image store
            ++gp0_word_count_;
            readback_pixel_index_ = 0u;
            readback_pixels_remaining_ = 0u;
            gp0_mode_ = Gp0Mode::vram_to_cpu_source;
            return {R3000aBusStatus::ok, 0u};
        default:
            last_unsupported_gp0_command_ = command;
            return {R3000aBusStatus::unsupported, 0u};
    }
}

R3000aBusResult Ps1GpuIngress::write_gp1(std::uint32_t value) noexcept {
    const auto command = static_cast<std::uint8_t>(value >> 24u);
    const auto parameter = value & 0x00FFFFFFu;
    last_unsupported_gp1_command_.reset();

    if (command >= 0x10u && command <= 0x1Fu) {
        switch (parameter & 0x00FFFFFFu) {
            case 0x02u:
                gpuread_latch_ =
                    static_cast<std::uint32_t>(texture_window_mask_x_) |
                    (static_cast<std::uint32_t>(texture_window_mask_y_) << 5u) |
                    (static_cast<std::uint32_t>(texture_window_offset_x_) << 10u) |
                    (static_cast<std::uint32_t>(texture_window_offset_y_) << 15u);
                break;
            case 0x03u:
                gpuread_latch_ =
                    (draw_area_left_ & 0x3FFu) |
                    ((draw_area_top_ & 0x3FFu) << 10u);
                break;
            case 0x04u:
                gpuread_latch_ =
                    (draw_area_right_ & 0x3FFu) |
                    ((draw_area_bottom_ & 0x3FFu) << 10u);
                break;
            case 0x05u:
                gpuread_latch_ =
                    (static_cast<std::uint32_t>(draw_offset_x_) & 0x7FFu) |
                    ((static_cast<std::uint32_t>(draw_offset_y_) & 0x7FFu) << 11u);
                break;
            case 0x07u:
                gpuread_latch_ = 0x00000002u; // retail/v2 GPU
                break;
            default:
                // Real hardware keeps the previous GPUREAD latch for
                // unsupported internal-register indices.
                break;
        }
        ++gp1_command_count_;
        return {R3000aBusStatus::ok, 0u};
    }

    switch (command) {
        case 0x00u: // Reset GPU
            status_ = reset_status;
            gpuread_latch_ = 0u;
            reset_command_buffer();
            reset_display_state();
            readback_x_ = 0u;
            readback_y_ = 0u;
            readback_width_ = 0u;
            readback_height_ = 0u;
            readback_pixel_index_ = 0u;
            readback_pixels_remaining_ = 0u;
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x01u: // Reset command buffer
            reset_command_buffer();
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x02u: // Ack GPU IRQ
            status_ &= ~(1u << 24u);
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x03u: // Display enable (1 = disabled)
            display_.enabled = (parameter & 1u) == 0u;
            if (!display_.enabled) status_ |= 1u << 23u;
            else status_ &= ~(1u << 23u);
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x04u: // DMA direction
            status_ = (status_ & ~(3u << 29u)) | ((parameter & 3u) << 29u);
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x05u: // Display VRAM start
            display_.start_x = parameter & 0x3FFu;
            display_.start_y = (parameter >> 10u) & 0x1FFu;
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x06u: // Horizontal display range
        case 0x07u: // Vertical display range
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x08u: // Display mode
            apply_display_mode(parameter);
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        default:
            last_unsupported_gp1_command_ = command;
            return {R3000aBusStatus::unsupported, 0u};
    }
}

std::uint32_t Ps1GpuIngress::status() const noexcept {
    return status_;
}

std::uint64_t Ps1GpuIngress::gp0_word_count() const noexcept {
    return gp0_word_count_;
}

std::uint64_t Ps1GpuIngress::gp1_command_count() const noexcept {
    return gp1_command_count_;
}

std::uint16_t Ps1GpuIngress::vram_pixel(std::uint32_t x, std::uint32_t y) const noexcept {
    if (x >= vram_width || y >= vram_height) return 0u;
    return vram_[static_cast<std::size_t>(y) * vram_width + x];
}

std::uint64_t Ps1GpuIngress::vram_write_count() const noexcept {
    return vram_write_count_;
}

Ps1GpuDisplayState Ps1GpuIngress::display_state() const noexcept {
    return display_;
}

std::uint64_t Ps1GpuIngress::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_u32(hash, status_);
    hash_u32(hash, gpuread_latch_);
    hash_u64(hash, gp0_word_count_);
    hash_u64(hash, gp1_command_count_);
    hash_u64(hash, vram_write_count_);

    hash_u64(hash, vram_.size());
    for (const auto pixel : vram_) hash_u16(hash, pixel);

    hash_bool(hash, display_.enabled);
    hash_bool(hash, display_.rgb24);
    hash_bool(hash, display_.pal);
    hash_bool(hash, display_.interlaced);
    hash_u32(hash, display_.start_x);
    hash_u32(hash, display_.start_y);
    hash_u32(hash, display_.width);
    hash_u32(hash, display_.height);

    hash_byte(hash, static_cast<std::uint8_t>(gp0_mode_));
    hash_u16(hash, fill_color_);
    hash_u32(hash, fill_x_);
    hash_u32(hash, fill_y_);
    hash_u16(hash, draw_color_);
    hash_u32(hash, static_cast<std::uint32_t>(draw_x_));
    hash_u32(hash, static_cast<std::uint32_t>(draw_y_));
    hash_u32(hash, draw_area_left_);
    hash_u32(hash, draw_area_top_);
    hash_u32(hash, draw_area_right_);
    hash_u32(hash, draw_area_bottom_);
    hash_u32(hash, static_cast<std::uint32_t>(draw_offset_x_));
    hash_u32(hash, static_cast<std::uint32_t>(draw_offset_y_));
    hash_u32(hash, texture_page_x_);
    hash_u32(hash, texture_page_y_);
    hash_byte(hash, texture_depth_);
    hash_byte(hash, texture_u_);
    hash_byte(hash, texture_v_);
    hash_u32(hash, texture_modulation_color_);
    hash_bool(hash, texture_raw_);
    hash_byte(hash, texture_window_mask_x_);
    hash_byte(hash, texture_window_mask_y_);
    hash_byte(hash, texture_window_offset_x_);
    hash_byte(hash, texture_window_offset_y_);
    hash_bool(hash, texture_x_flip_);
    hash_bool(hash, texture_y_flip_);
    hash_u32(hash, texture_fixed_width_);
    hash_u32(hash, texture_fixed_height_);
    hash_u32(hash, texture_clut_x_);
    hash_u32(hash, texture_clut_y_);

    hash_u64(hash, polygon_words_.size());
    for (const auto word : polygon_words_) hash_u32(hash, word);
    hash_u64(hash, polygon_words_expected_);

    hash_u32(hash, copy_source_x_);
    hash_u32(hash, copy_source_y_);
    hash_u32(hash, copy_destination_x_);
    hash_u32(hash, copy_destination_y_);
    hash_u32(hash, transfer_x_);
    hash_u32(hash, transfer_y_);
    hash_u32(hash, transfer_width_);
    hash_u32(hash, transfer_height_);
    hash_u32(hash, transfer_pixel_index_);
    hash_u32(hash, transfer_pixels_remaining_);
    hash_u32(hash, readback_x_);
    hash_u32(hash, readback_y_);
    hash_u32(hash, readback_width_);
    hash_u32(hash, readback_height_);
    hash_u32(hash, readback_pixel_index_);
    hash_u32(hash, readback_pixels_remaining_);

    hash_bool(hash, last_unsupported_gp0_command_.has_value());
    if (last_unsupported_gp0_command_) {
        hash_byte(hash, *last_unsupported_gp0_command_);
    }
    hash_bool(hash, last_unsupported_gp1_command_.has_value());
    if (last_unsupported_gp1_command_) {
        hash_byte(hash, *last_unsupported_gp1_command_);
    }
    return hash;
}

const std::optional<std::uint8_t>& Ps1GpuIngress::last_unsupported_gp0_command() const noexcept {
    return last_unsupported_gp0_command_;
}

const std::optional<std::uint8_t>& Ps1GpuIngress::last_unsupported_gp1_command() const noexcept {
    return last_unsupported_gp1_command_;
}

} // namespace jojo
