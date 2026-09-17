#include "core/ps1_hardware_services.h"

#include <cstddef>

namespace jojo {
namespace {

constexpr std::uint32_t kInterruptStatusAddress = 0x1F801070u;
constexpr std::uint32_t kInterruptMaskAddress = 0x1F801074u;
constexpr std::uint32_t kTimerBase = 0x1F801100u;
constexpr std::uint32_t kTimerStride = 0x10u;
constexpr std::uint32_t kTimerCounterOffset = 0x0u;
constexpr std::uint32_t kTimerModeOffset = 0x4u;
constexpr std::uint32_t kTimerTargetOffset = 0x8u;
constexpr std::uint16_t kTimerResetAtTarget = 0x0008u;
constexpr std::uint16_t kTimerIrqAtTarget = 0x0010u;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

bool decode_timer_register(std::uint32_t physical,
                           std::uint32_t& channel,
                           std::uint32_t& offset) noexcept {
    if (physical < kTimerBase || physical >= kTimerBase + 3u * kTimerStride) {
        return false;
    }
    const auto relative = physical - kTimerBase;
    channel = relative / kTimerStride;
    offset = relative % kTimerStride;
    return offset == kTimerCounterOffset ||
           offset == kTimerModeOffset ||
           offset == kTimerTargetOffset;
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u16(std::uint64_t& hash, std::uint16_t value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value));
    hash_byte(hash, static_cast<std::uint8_t>(value >> 8u));
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

R3000aBusResult Ps1HardwareServices::read8(std::uint32_t) noexcept {
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1HardwareServices::read16(std::uint32_t physical) noexcept {
    if (physical == kInterruptStatusAddress) {
        return {R3000aBusStatus::ok, interrupt_status_};
    }
    if (physical == kInterruptMaskAddress) {
        return {R3000aBusStatus::ok, interrupt_mask_};
    }

    std::uint32_t channel = 0u;
    std::uint32_t offset = 0u;
    if (decode_timer_register(physical, channel, offset)) {
        const auto& timer = timers_[channel];
        if (offset == kTimerCounterOffset) return {R3000aBusStatus::ok, timer.counter};
        if (offset == kTimerModeOffset) return {R3000aBusStatus::ok, timer.mode};
        if (offset == kTimerTargetOffset) return {R3000aBusStatus::ok, timer.target};
    }
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1HardwareServices::read32(std::uint32_t) noexcept {
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1HardwareServices::write8(std::uint32_t, std::uint8_t) noexcept {
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1HardwareServices::write16(std::uint32_t physical,
                                             std::uint16_t value) noexcept {
    if (physical == kInterruptStatusAddress) {
        interrupt_status_ = static_cast<std::uint16_t>(
            interrupt_status_ & value & interrupt_valid_bits);
        return {R3000aBusStatus::ok, 0u};
    }
    if (physical == kInterruptMaskAddress) {
        interrupt_mask_ = static_cast<std::uint16_t>(value & interrupt_valid_bits);
        return {R3000aBusStatus::ok, 0u};
    }

    std::uint32_t channel = 0u;
    std::uint32_t offset = 0u;
    if (!decode_timer_register(physical, channel, offset)) {
        return {R3000aBusStatus::unsupported, 0u};
    }

    auto& timer = timers_[channel];
    if (offset == kTimerCounterOffset) {
        timer.counter = value;
        timer.cycle_accumulator = 0u;
        return {R3000aBusStatus::ok, 0u};
    }
    if (offset == kTimerModeOffset) {
        if ((value & static_cast<std::uint16_t>(~timer_supported_mode_mask)) != 0u) {
            return {R3000aBusStatus::unsupported, 0u};
        }
        timer.mode = value;
        timer.counter = 0u;
        timer.cycle_accumulator = 0u;
        return {R3000aBusStatus::ok, 0u};
    }
    timer.target = value;
    return {R3000aBusStatus::ok, 0u};
}

R3000aBusResult Ps1HardwareServices::write32(std::uint32_t physical,
                                             std::uint32_t value) noexcept {
    std::uint32_t channel = 0u;
    std::uint32_t offset = 0u;
    if (decode_timer_register(physical, channel, offset) && offset == kTimerModeOffset) {
        return write16(physical, static_cast<std::uint16_t>(value & 0xFFFFu));
    }
    return {R3000aBusStatus::unsupported, 0u};
}

void Ps1HardwareServices::step(std::uint32_t cpu_cycles) noexcept {
    for (std::uint32_t channel = 0u; channel < timers_.size(); ++channel) {
        auto& timer = timers_[channel];
        if (cpu_cycles == 0u) continue;

        timer.cycle_accumulator += cpu_cycles;
        while (timer.cycle_accumulator != 0u) {
            --timer.cycle_accumulator;
            timer.counter = static_cast<std::uint16_t>(timer.counter + 1u);

            if (timer.target != 0u && timer.counter == timer.target) {
                if ((timer.mode & kTimerIrqAtTarget) != 0u) {
                    interrupt_status_ = static_cast<std::uint16_t>(
                        interrupt_status_ | static_cast<std::uint16_t>(1u << (4u + channel)));
                }
                if ((timer.mode & kTimerResetAtTarget) != 0u) {
                    timer.counter = 0u;
                }
            }
        }
    }
}

std::uint16_t Ps1HardwareServices::interrupt_status() const noexcept {
    return interrupt_status_;
}

std::uint16_t Ps1HardwareServices::interrupt_mask() const noexcept {
    return interrupt_mask_;
}

std::uint16_t Ps1HardwareServices::timer_counter(std::uint32_t channel) const noexcept {
    return channel < timers_.size() ? timers_[channel].counter : 0u;
}

std::uint16_t Ps1HardwareServices::timer_mode(std::uint32_t channel) const noexcept {
    return channel < timers_.size() ? timers_[channel].mode : 0u;
}

std::uint16_t Ps1HardwareServices::timer_target(std::uint32_t channel) const noexcept {
    return channel < timers_.size() ? timers_[channel].target : 0u;
}

bool Ps1HardwareServices::interrupt_pending() const noexcept {
    return (interrupt_status_ & interrupt_mask_) != 0u;
}

std::uint64_t Ps1HardwareServices::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_u16(hash, interrupt_status_);
    hash_u16(hash, interrupt_mask_);
    for (const auto& timer : timers_) {
        hash_u16(hash, timer.counter);
        hash_u16(hash, timer.mode);
        hash_u16(hash, timer.target);
        hash_u32(hash, timer.cycle_accumulator);
    }
    return hash;
}

} // namespace jojo
