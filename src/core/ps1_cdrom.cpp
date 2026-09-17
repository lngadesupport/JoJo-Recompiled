#include "core/ps1_cdrom.h"

#include <array>
#include <vector>

namespace jojo {
namespace {

constexpr std::uint32_t kCdBase = 0x1F801800u;
constexpr std::uint32_t kCdStatus = kCdBase + 0u;
constexpr std::uint32_t kCdCommandResponse = kCdBase + 1u;
constexpr std::uint32_t kCdParameterData = kCdBase + 2u;
constexpr std::uint32_t kCdInterrupt = kCdBase + 3u;

bool bcd_to_binary(std::uint8_t bcd, std::uint32_t& out) noexcept {
    const auto hi = static_cast<std::uint32_t>(bcd >> 4u);
    const auto lo = static_cast<std::uint32_t>(bcd & 0x0Fu);
    if (hi > 9u || lo > 9u) return false;
    out = hi * 10u + lo;
    return true;
}

} // namespace

void Ps1CdromController::attach_disc(const Ps1DiscSession* disc) noexcept {
    disc_ = disc;
}

R3000aBusResult Ps1CdromController::read8(std::uint32_t physical) noexcept {
    if (physical == kCdStatus) {
        std::uint32_t status = index_ & 0x03u;
        if (parameters_.empty()) status |= 1u << 3u;
        if (!responses_.empty()) status |= 1u << 5u;
        if (!data_.empty()) status |= 1u << 6u;
        return {R3000aBusStatus::ok, status};
    }
    if (physical == kCdCommandResponse) {
        if (responses_.empty()) return {R3000aBusStatus::unsupported, 0u};
        const auto value = responses_.front();
        responses_.pop_front();
        return {R3000aBusStatus::ok, value};
    }
    if (physical == kCdParameterData) {
        if (data_.empty()) return {R3000aBusStatus::unsupported, 0u};
        const auto value = data_.front();
        data_.pop_front();
        return {R3000aBusStatus::ok, value};
    }
    if (physical == kCdInterrupt) {
        return {R3000aBusStatus::ok,
                index_ == 0u ? interrupt_enable_ : interrupt_flags_};
    }
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1CdromController::write8(std::uint32_t physical,
                                           std::uint8_t value) noexcept {
    if (physical == kCdStatus) {
        index_ = static_cast<std::uint8_t>(value & 0x03u);
        return {R3000aBusStatus::ok, 0u};
    }
    if (physical == kCdCommandResponse) {
        if (index_ != 0u) return {R3000aBusStatus::unsupported, 0u};
        return execute_command(value);
    }
    if (physical == kCdParameterData) {
        if (index_ == 0u) {
            if (parameters_.size() >= parameter_capacity) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            parameters_.push_back(value);
            return {R3000aBusStatus::ok, 0u};
        }
        if (index_ == 1u) {
            interrupt_enable_ = static_cast<std::uint8_t>(value & 0x1Fu);
            return {R3000aBusStatus::ok, 0u};
        }
        return {R3000aBusStatus::unsupported, 0u};
    }
    if (physical == kCdInterrupt) {
        if (index_ == 1u) {
            interrupt_flags_ = static_cast<std::uint8_t>(interrupt_flags_ & ~(value & 0x1Fu));
            if ((value & 0x40u) != 0u) parameters_.clear();
            return {R3000aBusStatus::ok, 0u};
        }
        return {R3000aBusStatus::unsupported, 0u};
    }
    return {R3000aBusStatus::unsupported, 0u};
}

std::size_t Ps1CdromController::response_bytes_available() const noexcept {
    return responses_.size();
}

std::size_t Ps1CdromController::data_bytes_available() const noexcept {
    return data_.size();
}

std::size_t Ps1CdromController::read_data_words(std::span<std::uint32_t> out) noexcept {
    std::size_t written = 0u;
    while (written < out.size() && data_.size() >= 4u) {
        const auto b0 = static_cast<std::uint32_t>(data_.front()); data_.pop_front();
        const auto b1 = static_cast<std::uint32_t>(data_.front()); data_.pop_front();
        const auto b2 = static_cast<std::uint32_t>(data_.front()); data_.pop_front();
        const auto b3 = static_cast<std::uint32_t>(data_.front()); data_.pop_front();
        out[written++] = b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u);
    }
    return written;
}

std::uint64_t Ps1CdromController::current_lba() const noexcept {
    return current_lba_;
}

const std::optional<std::uint8_t>&
Ps1CdromController::last_unsupported_command() const noexcept {
    return last_unsupported_command_;
}

bool Ps1CdromController::push_response(std::uint8_t value) noexcept {
    if (responses_.size() >= response_capacity) return false;
    responses_.push_back(value);
    return true;
}

void Ps1CdromController::clear_transfer_fifos() noexcept {
    parameters_.clear();
    responses_.clear();
    data_.clear();
}

R3000aBusResult Ps1CdromController::execute_command(std::uint8_t command) noexcept {
    last_unsupported_command_.reset();

    switch (command) {
        case 0x01u: // Getstat
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};

        case 0x02u: { // Setloc
            if (parameters_.size() < 3u) return {R3000aBusStatus::unsupported, 0u};
            const std::array<std::uint8_t, 3> msf{
                parameters_[0], parameters_[1], parameters_[2]};
            std::uint32_t minute = 0u;
            std::uint32_t second = 0u;
            std::uint32_t frame = 0u;
            if (!bcd_to_binary(msf[0], minute) ||
                !bcd_to_binary(msf[1], second) ||
                !bcd_to_binary(msf[2], frame) ||
                second >= 60u || frame >= 75u) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            const std::uint64_t absolute_frame =
                static_cast<std::uint64_t>(minute) * 60u * 75u +
                static_cast<std::uint64_t>(second) * 75u + frame;
            if (absolute_frame < 150u) return {R3000aBusStatus::unsupported, 0u};
            current_lba_ = absolute_frame - 150u;
            parameters_.clear();
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        case 0x06u: { // ReadN
            if (disc_ == nullptr) return {R3000aBusStatus::unsupported, 0u};
            auto sector = disc_->read_sectors(current_lba_, 1u);
            if (!sector || sector.value.size() != data_capacity) {
                return {R3000aBusStatus::unsupported, 0u};
            }
            data_.assign(sector.value.begin(), sector.value.end());
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x01u;
            return {R3000aBusStatus::ok, 0u};
        }

        case 0x08u: // Stop
        case 0x09u: // Pause
            data_.clear();
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};

        case 0x0Au: { // Init
            const auto* attached = disc_;
            clear_transfer_fifos();
            disc_ = attached;
            index_ = 0u;
            interrupt_enable_ = 0u;
            interrupt_flags_ = 0u;
            status_byte_ = 0u;
            current_lba_ = 0u;
            last_unsupported_command_.reset();
            if (!push_response(status_byte_)) return {R3000aBusStatus::unsupported, 0u};
            interrupt_flags_ = 0x03u;
            return {R3000aBusStatus::ok, 0u};
        }

        default:
            last_unsupported_command_ = command;
            return {R3000aBusStatus::unsupported, 0u};
    }
}

} // namespace jojo
