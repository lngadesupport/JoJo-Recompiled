#include "core/ps1_gpu_ingress.h"

namespace jojo {
namespace {

std::uint32_t normalize_transfer_width(std::uint32_t raw) noexcept {
    return ((raw - 1u) & 0x3FFu) + 1u;
}

std::uint32_t normalize_transfer_height(std::uint32_t raw) noexcept {
    return ((raw - 1u) & 0x1FFu) + 1u;
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
        case 0xE1u:
        case 0xE2u:
        case 0xE3u:
        case 0xE4u:
        case 0xE5u:
        case 0xE6u:
            ++gp0_word_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x02u: // Fill rectangle in VRAM
            ++gp0_word_count_;
            fill_color_ = color24_to_bgr555(value & 0x00FFFFFFu);
            gp0_mode_ = Gp0Mode::fill_rectangle_position;
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
