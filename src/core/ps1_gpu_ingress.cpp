#include "core/ps1_gpu_ingress.h"

namespace jojo {

R3000aBusResult Ps1GpuIngress::write_gp0(std::uint32_t value) noexcept {
    const auto command = static_cast<std::uint8_t>(value >> 24u);
    last_unsupported_gp0_command_.reset();

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
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x01u: // Reset command buffer
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x02u: // Ack GPU IRQ
            status_ &= ~(1u << 24u);
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x03u: // Display enable (1 = disabled)
            if ((parameter & 1u) != 0u) status_ |= 1u << 23u;
            else status_ &= ~(1u << 23u);
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x04u: // DMA direction
            status_ = (status_ & ~(3u << 29u)) | ((parameter & 3u) << 29u);
            ++gp1_command_count_;
            return {R3000aBusStatus::ok, 0u};
        case 0x05u: // Display VRAM start
        case 0x06u: // Horizontal display range
        case 0x07u: // Vertical display range
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

const std::optional<std::uint8_t>& Ps1GpuIngress::last_unsupported_gp0_command() const noexcept {
    return last_unsupported_gp0_command_;
}

const std::optional<std::uint8_t>& Ps1GpuIngress::last_unsupported_gp1_command() const noexcept {
    return last_unsupported_gp1_command_;
}

} // namespace jojo
