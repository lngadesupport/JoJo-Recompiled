#include "core/ps1_hardware_services.h"

#include <cstddef>
#include <vector>

namespace jojo {
namespace {

constexpr std::uint32_t kInterruptStatusAddress = 0x1F801070u;
constexpr std::uint32_t kInterruptMaskAddress = 0x1F801074u;
constexpr std::uint32_t kDmaBase = 0x1F801080u;
constexpr std::uint32_t kDmaStride = 0x10u;
constexpr std::uint32_t kDmaControlAddress = 0x1F8010F0u;
constexpr std::uint32_t kDmaInterruptAddress = 0x1F8010F4u;
constexpr std::uint32_t kDmaInterruptControlMask = 0x00FF807Fu;
constexpr std::uint32_t kDmaInterruptFlagMask = 0x7F000000u;
constexpr std::uint32_t kDmaInterruptMasterFlag = 0x80000000u;
constexpr std::uint32_t kDmaInterruptMasterEnable = 0x00800000u;
constexpr std::uint32_t kDmaInterruptBusError = 0x00008000u;
constexpr std::uint32_t kDmaBusy = 1u << 24u;
constexpr std::uint32_t kDmaTrigger = 1u << 28u;
constexpr std::uint32_t kDmaDirectionFromRam = 1u;
constexpr std::uint32_t kDmaSyncMask = 3u << 9u;
constexpr std::uint32_t kDmaStepDecrement = 1u << 1u;
constexpr std::uint32_t kDmaChopping = 1u << 8u;
constexpr std::uint32_t kTimerBase = 0x1F801100u;
constexpr std::uint32_t kSpuBase = Ps1Spu::mmio_base;
constexpr std::uint32_t kSpuEnd = Ps1Spu::mmio_end;
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

bool decode_dma_register(std::uint32_t physical,
                         std::uint32_t& channel,
                         std::uint32_t& offset) noexcept {
    if (physical < kDmaBase || physical >= kDmaBase + 7u * kDmaStride) {
        return false;
    }
    const auto relative = physical - kDmaBase;
    channel = relative / kDmaStride;
    offset = relative % kDmaStride;
    return offset == 0u || offset == 4u || offset == 8u;
}

std::uint32_t visible_dma_interrupt(std::uint32_t state) noexcept {
    std::uint32_t value = state & (kDmaInterruptControlMask | kDmaInterruptFlagMask);
    if ((value & kDmaInterruptBusError) != 0u ||
        ((value & kDmaInterruptMasterEnable) != 0u &&
         (value & kDmaInterruptFlagMask) != 0u)) {
        value |= kDmaInterruptMasterFlag;
    }
    return value;
}

bool dma_channel_enabled(std::uint32_t dpcr, std::uint32_t channel) noexcept {
    return (dpcr & (1u << (channel * 4u + 3u))) != 0u;
}

bool supported_dma_device_direction(std::uint32_t channel, bool from_ram) noexcept {
    if (channel == 2u) return from_ram;
    if (channel == 3u) return !from_ram;
    if (channel == 4u) return true;
    return false;
}

bool is_spu_halfword(std::uint32_t physical) noexcept {
    return physical >= kSpuBase && physical <= kSpuEnd &&
           (physical & 1u) == 0u;
}

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
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

void Ps1HardwareServices::sync_sio0_irq_edge() noexcept {
    const bool current = sio0_.irq_pending();
    if (current && !sio0_irq_line_) {
        interrupt_status_ = static_cast<std::uint16_t>(
            interrupt_status_ | 0x0080u);
    }
    sio0_irq_line_ = current;
}

void Ps1HardwareServices::attach_disc(const Ps1DiscSession* disc) noexcept {
    cdrom_.attach_disc(disc);
}

R3000aBusResult Ps1HardwareServices::read8(std::uint32_t physical) noexcept {
    if (physical == Ps1Sio0::data_address) {
        return sio0_.read8(physical);
    }
    if (physical >= 0x1F801800u && physical <= 0x1F801803u) {
        return cdrom_.read8(physical);
    }
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1HardwareServices::read16(std::uint32_t physical) noexcept {
    if (physical == Ps1Sio0::mode_address ||
        physical == Ps1Sio0::control_address ||
        physical == Ps1Sio0::baud_address) {
        return sio0_.read16(physical);
    }
    if (is_spu_halfword(physical)) {
        return spu_.read16(physical);
    }
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

R3000aBusResult Ps1HardwareServices::read32(std::uint32_t physical) noexcept {
    if (physical == Ps1Sio0::data_address ||
        physical == Ps1Sio0::status_address) {
        return sio0_.read32(physical);
    }
    if (is_spu_halfword(physical) && physical + 2u <= kSpuEnd) {
        const auto low = spu_.read16(physical);
        if (low.status != R3000aBusStatus::ok) return low;
        const auto high = spu_.read16(physical + 2u);
        if (high.status != R3000aBusStatus::ok) return high;
        return {R3000aBusStatus::ok, low.value | (high.value << 16u)};
    }
    if (physical == 0x1F801810u) {
        return gpu_.read_gp0();
    }
    if (physical == 0x1F801814u) {
        return {R3000aBusStatus::ok, gpu_.status()};
    }
    if (physical == kDmaControlAddress) {
        return {R3000aBusStatus::ok, dma_control_};
    }
    if (physical == kDmaInterruptAddress) {
        return {R3000aBusStatus::ok, visible_dma_interrupt(dma_interrupt_)};
    }

    std::uint32_t channel = 0u;
    std::uint32_t offset = 0u;
    if (decode_dma_register(physical, channel, offset)) {
        const auto& dma = dma_channels_[channel];
        if (offset == 0u) return {R3000aBusStatus::ok, dma.madr};
        if (offset == 4u) return {R3000aBusStatus::ok, dma.bcr};
        return {R3000aBusStatus::ok, dma.chcr};
    }
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1HardwareServices::write8(std::uint32_t physical,
                                          std::uint8_t value) noexcept {
    if (physical == Ps1Sio0::data_address) {
        const auto result = sio0_.write8(physical, value);
        sync_sio0_irq_edge();
        return result;
    }
    if (physical >= 0x1F801800u && physical <= 0x1F801803u) {
        return cdrom_.write8(physical, value);
    }
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1HardwareServices::write16(std::uint32_t physical,
                                             std::uint16_t value) noexcept {
    if (physical == Ps1Sio0::mode_address ||
        physical == Ps1Sio0::control_address ||
        physical == Ps1Sio0::baud_address) {
        const auto result = sio0_.write16(physical, value);
        sync_sio0_irq_edge();
        return result;
    }
    if (is_spu_halfword(physical)) {
        return spu_.write16(physical, value);
    }
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
    if (physical == Ps1Sio0::data_address) {
        const auto result = sio0_.write32(physical, value);
        sync_sio0_irq_edge();
        return result;
    }
    if (is_spu_halfword(physical) && physical + 2u <= kSpuEnd) {
        const auto low = spu_.write16(
            physical,
            static_cast<std::uint16_t>(value));
        if (low.status != R3000aBusStatus::ok) return low;
        return spu_.write16(
            physical + 2u,
            static_cast<std::uint16_t>(value >> 16u));
    }
    if (physical == 0x1F801810u) return gpu_.write_gp0(value);
    if (physical == 0x1F801814u) return gpu_.write_gp1(value);
    if (physical == kDmaControlAddress) {
        dma_control_ = value;
        return {R3000aBusStatus::ok, 0u};
    }
    if (physical == kDmaInterruptAddress) {
        const auto flags = (dma_interrupt_ & kDmaInterruptFlagMask) &
                           ~(value & kDmaInterruptFlagMask);
        dma_interrupt_ = (value & kDmaInterruptControlMask) | flags;
        return {R3000aBusStatus::ok, 0u};
    }

    std::uint32_t dma_channel_index = 0u;
    std::uint32_t dma_offset = 0u;
    if (decode_dma_register(physical, dma_channel_index, dma_offset)) {
        auto& dma = dma_channels_[dma_channel_index];
        if (dma_offset == 0u) {
            dma.madr = value & 0x00FFFFFFu;
            return {R3000aBusStatus::ok, 0u};
        }
        if (dma_offset == 4u) {
            dma.bcr = value;
            return {R3000aBusStatus::ok, 0u};
        }

        if ((value & kDmaBusy) == 0u) {
            dma.chcr = value;
            return {R3000aBusStatus::ok, 0u};
        }
        if (!dma_channel_enabled(dma_control_, dma_channel_index) ||
            pending_dma_transfer_.has_value()) {
            return {R3000aBusStatus::unsupported, 0u};
        }
        if ((value & kDmaSyncMask) != 0u ||
            (value & kDmaTrigger) == 0u ||
            (value & kDmaStepDecrement) != 0u ||
            (value & kDmaChopping) != 0u) {
            return {R3000aBusStatus::unsupported, 0u};
        }

        const bool from_ram = (value & kDmaDirectionFromRam) != 0u;
        if (!supported_dma_device_direction(dma_channel_index, from_ram)) {
            return {R3000aBusStatus::unsupported, 0u};
        }
        const auto words = dma.bcr & 0xFFFFu;
        if (words == 0u || words > 0x10000u) {
            return {R3000aBusStatus::unsupported, 0u};
        }

        dma.chcr = value;
        pending_dma_transfer_ = Ps1DmaTransferRequest{
            static_cast<std::uint8_t>(dma_channel_index),
            dma.madr,
            words,
            from_ram,
        };
        return {R3000aBusStatus::ok, 0u};
    }

    std::uint32_t timer_channel = 0u;
    std::uint32_t timer_offset = 0u;
    if (decode_timer_register(physical, timer_channel, timer_offset) &&
        timer_offset == kTimerModeOffset) {
        return write16(physical, static_cast<std::uint16_t>(value & 0xFFFFu));
    }
    return {R3000aBusStatus::unsupported, 0u};
}

void Ps1HardwareServices::step(std::uint32_t cpu_cycles) noexcept {
    spu_.step(cpu_cycles);
    sync_sio0_irq_edge();
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

void Ps1HardwareServices::signal_vblank() noexcept {
    interrupt_status_ = static_cast<std::uint16_t>(
        interrupt_status_ | 0x0001u);
    ++vblank_count_;
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

std::uint32_t Ps1HardwareServices::dma_control() const noexcept {
    return dma_control_;
}

std::uint32_t Ps1HardwareServices::dma_interrupt() const noexcept {
    return visible_dma_interrupt(dma_interrupt_);
}

const Ps1DmaChannelState& Ps1HardwareServices::dma_channel(std::uint32_t channel) const noexcept {
    static const Ps1DmaChannelState empty{};
    return channel < dma_channels_.size() ? dma_channels_[channel] : empty;
}

const std::optional<Ps1DmaTransferRequest>&
Ps1HardwareServices::pending_dma_transfer() const noexcept {
    return pending_dma_transfer_;
}

bool Ps1HardwareServices::execute_pending_dma(
    std::span<std::uint8_t> main_ram) noexcept {
    if (!pending_dma_transfer_) return false;
    const auto request = *pending_dma_transfer_;
    const auto start = static_cast<std::size_t>(request.madr);
    const auto byte_count = static_cast<std::size_t>(request.words) * 4u;
    if (start >= main_ram.size() || byte_count > main_ram.size() - start) {
        return false;
    }

    if (request.channel == 4u) {
        std::vector<std::uint32_t> words(request.words, 0u);
        if (request.from_ram) {
            for (std::size_t i = 0; i < request.words; ++i) {
                const auto offset = start + i * 4u;
                words[i] =
                    static_cast<std::uint32_t>(main_ram[offset + 0u]) |
                    (static_cast<std::uint32_t>(main_ram[offset + 1u]) << 8u) |
                    (static_cast<std::uint32_t>(main_ram[offset + 2u]) << 16u) |
                    (static_cast<std::uint32_t>(main_ram[offset + 3u]) << 24u);
            }
            if (!spu_.dma_write_words(words)) return false;
        } else {
            if (!spu_.dma_read_words(words)) return false;
            for (std::size_t i = 0; i < request.words; ++i) {
                const auto value = words[i];
                const auto offset = start + i * 4u;
                main_ram[offset + 0u] = static_cast<std::uint8_t>(value);
                main_ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
                main_ram[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
                main_ram[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
            }
        }
        return complete_dma_transfer(4u);
    }

    if (request.channel == 2u && request.from_ram) {
        auto candidate = gpu_;
        for (std::size_t i = 0; i < request.words; ++i) {
            const auto offset = start + i * 4u;
            const std::uint32_t value =
                static_cast<std::uint32_t>(main_ram[offset + 0u]) |
                (static_cast<std::uint32_t>(main_ram[offset + 1u]) << 8u) |
                (static_cast<std::uint32_t>(main_ram[offset + 2u]) << 16u) |
                (static_cast<std::uint32_t>(main_ram[offset + 3u]) << 24u);
            if (candidate.write_gp0(value).status != R3000aBusStatus::ok) {
                return false;
            }
        }
        gpu_ = candidate;
        return complete_dma_transfer(2u);
    }

    if (request.channel != 3u || request.from_ram) return false;
    if (cdrom_.data_bytes_available() < byte_count) return false;

    std::vector<std::uint32_t> words(request.words, 0u);
    if (cdrom_.read_data_words(words) != words.size()) return false;

    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto value = words[i];
        const auto offset = start + i * 4u;
        main_ram[offset + 0u] = static_cast<std::uint8_t>(value);
        main_ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
        main_ram[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
        main_ram[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
    }
    return complete_dma_transfer(3u);
}

bool Ps1HardwareServices::complete_dma_transfer(std::uint32_t channel) noexcept {
    if (!pending_dma_transfer_ || pending_dma_transfer_->channel != channel ||
        channel >= dma_channels_.size()) {
        return false;
    }

    auto& dma = dma_channels_[channel];
    dma.chcr &= ~(kDmaBusy | kDmaTrigger);
    dma_interrupt_ |= 1u << (24u + channel);
    ++completed_dma_transfer_count_;
    pending_dma_transfer_.reset();

    if ((visible_dma_interrupt(dma_interrupt_) & kDmaInterruptMasterFlag) != 0u) {
        interrupt_status_ = static_cast<std::uint16_t>(interrupt_status_ | 0x0008u);
    }
    return true;
}

void Ps1HardwareServices::cancel_pending_dma_transfer() noexcept {
    pending_dma_transfer_.reset();
}

std::uint64_t Ps1HardwareServices::completed_dma_transfer_count() const noexcept {
    return completed_dma_transfer_count_;
}

std::uint64_t Ps1HardwareServices::vblank_count() const noexcept {
    return vblank_count_;
}

const Ps1CdromController& Ps1HardwareServices::cdrom() const noexcept {
    return cdrom_;
}

std::uint32_t Ps1HardwareServices::gpu_status() const noexcept {
    return gpu_.status();
}

std::uint64_t Ps1HardwareServices::gpu_gp0_word_count() const noexcept {
    return gpu_.gp0_word_count();
}

std::uint64_t Ps1HardwareServices::gpu_gp1_command_count() const noexcept {
    return gpu_.gp1_command_count();
}

std::uint64_t Ps1HardwareServices::gpu_vram_write_count() const noexcept {
    return gpu_.vram_write_count();
}

const Ps1GpuIngress& Ps1HardwareServices::gpu() const noexcept {
    return gpu_;
}

Ps1Spu& Ps1HardwareServices::spu() noexcept {
    return spu_;
}

const Ps1Spu& Ps1HardwareServices::spu() const noexcept {
    return spu_;
}

Ps1Sio0& Ps1HardwareServices::sio0() noexcept {
    return sio0_;
}

const Ps1Sio0& Ps1HardwareServices::sio0() const noexcept {
    return sio0_;
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
    for (const auto& dma : dma_channels_) {
        hash_u32(hash, dma.madr);
        hash_u32(hash, dma.bcr);
        hash_u32(hash, dma.chcr);
    }
    hash_u32(hash, dma_control_);
    hash_u32(hash, dma_interrupt_);
    hash_bool(hash, pending_dma_transfer_.has_value());
    if (pending_dma_transfer_) {
        hash_byte(hash, pending_dma_transfer_->channel);
        hash_u32(hash, pending_dma_transfer_->madr);
        hash_u32(hash, pending_dma_transfer_->words);
        hash_bool(hash, pending_dma_transfer_->from_ram);
    }
    hash_u64(hash, completed_dma_transfer_count_);
    hash_u64(hash, vblank_count_);
    hash_u64(hash, spu_.diagnostic_state_hash());
    hash_u64(hash, sio0_.diagnostic_state_hash());
    hash_bool(hash, sio0_irq_line_);
    return hash;
}

} // namespace jojo
