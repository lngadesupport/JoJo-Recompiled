#include "core/ps1_gpu_ingress.h"

namespace jojo {
namespace {

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
    fill_color_ = 0u;
    fill_x_ = 0u;
    fill_y_ = 0u;
    draw_color_ = 0u;
    draw_x_ = 0;
    draw_y_ = 0;
    texture_fixed_width_ = 0u;
    texture_fixed_height_ = 0u;
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
    const bool interlaced = (parameter & (1u << 5u)) != 0u;
    display_.height = vertical_480 && interlaced ? 480u : 240u;
    display_.rgb24 = (parameter & (1u << 4u)) != 0u;
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

void Ps1GpuIngress::draw_raw_textured_rectangle(
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

            vram_[static_cast<std::size_t>(y) * vram_width +
                  static_cast<std::uint32_t>(x)] = texel;
            ++vram_write_count_;
        }
    }
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

R3000aBusResult Ps1GpuIngress::write_gp0(std::uint32_t value) noexcept {
    last_unsupported_gp0_command_.reset();

    switch (gp0_mode_) {
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
                draw_raw_textured_rectangle(texture_fixed_width_, texture_fixed_height_);
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
            draw_raw_textured_rectangle(width, height);
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
        case 0x65u: // Raw-textured variable rectangle
            if (texture_depth_ > 2u) {
                last_unsupported_gp0_command_ = command;
                return {R3000aBusStatus::unsupported, 0u};
            }
            texture_fixed_width_ = 0u;
            texture_fixed_height_ = 0u;
            ++gp0_word_count_;
            gp0_mode_ = Gp0Mode::textured_rectangle_position;
            return {R3000aBusStatus::ok, 0u};
        case 0x6Du: // Raw-textured 1x1 rectangle
        case 0x75u: // Raw-textured 8x8 rectangle
        case 0x7Du: // Raw-textured 16x16 rectangle
            if (texture_depth_ > 2u) {
                last_unsupported_gp0_command_ = command;
                return {R3000aBusStatus::unsupported, 0u};
            }
            if (command == 0x6Du) {
                texture_fixed_width_ = 1u;
                texture_fixed_height_ = 1u;
            } else if (command == 0x75u) {
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
        default:
            last_unsupported_gp0_command_ = command;
            return {R3000aBusStatus::unsupported, 0u};
    }
}

R3000aBusResult Ps1GpuIngress::write_gp1(std::uint32_t value) noexcept {
    const auto command = static_cast<std::uint8_t>(value >> 24u);
    const auto parameter = value & 0x00FFFFFFu;
    last_unsupported_gp1_command_.reset();

    switch (command) {
        case 0x00u: // Reset GPU
            status_ = reset_status;
            reset_command_buffer();
            reset_display_state();
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

const std::optional<std::uint8_t>& Ps1GpuIngress::last_unsupported_gp0_command() const noexcept {
    return last_unsupported_gp0_command_;
}

const std::optional<std::uint8_t>& Ps1GpuIngress::last_unsupported_gp1_command() const noexcept {
    return last_unsupported_gp1_command_;
}

} // namespace jojo
