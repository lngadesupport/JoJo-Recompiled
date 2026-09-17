#include "core/ps1_memory_bus.h"

#include <algorithm>
#include <cstddef>

namespace jojo {
namespace {

constexpr std::uint32_t kDiagnosticMmioBase = 0x1F801000u;
constexpr std::uint32_t kInterruptStatusAddress = 0x1F801070u;
constexpr std::uint32_t kInterruptMaskAddress = 0x1F801074u;
constexpr std::uint32_t kDmaControlAddress = 0x1F8010F0u;
constexpr std::uint32_t kDmaInterruptAddress = 0x1F8010F4u;
constexpr std::uint32_t kTimer1ModeAddress = 0x1F801114u;
constexpr std::uint16_t kInterruptValidBits = 0x07FFu;
constexpr std::uint32_t kDmaInterruptControlMask = 0x00FF807Fu;
constexpr std::uint32_t kDmaInterruptFlagMask = 0x7F000000u;
constexpr std::uint32_t kDmaInterruptMasterFlag = 0x80000000u;
constexpr std::uint32_t kDmaInterruptMasterEnable = 0x00800000u;
constexpr std::uint32_t kDmaInterruptBusError = 0x00008000u;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint8_t* mapped_bytes(std::uint32_t physical,
                           std::size_t width,
                           std::span<std::uint8_t> main_ram,
                           std::span<std::uint8_t> scratchpad) noexcept {
    if (physical < main_ram.size() &&
        width <= main_ram.size() - static_cast<std::size_t>(physical)) {
        return main_ram.data() + physical;
    }

    if (physical >= Ps1MemoryBus::scratchpad_base) {
        const auto offset = static_cast<std::size_t>(physical - Ps1MemoryBus::scratchpad_base);
        if (offset < scratchpad.size() && width <= scratchpad.size() - offset) {
            return scratchpad.data() + offset;
        }
    }
    return nullptr;
}

std::uint8_t* diagnostic_mmio_bytes(std::uint32_t physical,
                                    std::size_t width,
                                    std::span<std::uint8_t> shadow) noexcept {
    if (physical < kDiagnosticMmioBase) return nullptr;
    const auto offset = static_cast<std::size_t>(physical - kDiagnosticMmioBase);
    if (offset < shadow.size() && width <= shadow.size() - offset) {
        return shadow.data() + offset;
    }
    return nullptr;
}

std::uint32_t read_little_endian(const std::uint8_t* bytes, std::uint8_t width) noexcept {
    std::uint32_t value = std::uint32_t(bytes[0]);
    if (width >= 2u) value |= std::uint32_t(bytes[1]) << 8;
    if (width == 4u) {
        value |= std::uint32_t(bytes[2]) << 16;
        value |= std::uint32_t(bytes[3]) << 24;
    }
    return value;
}

void write_little_endian(std::uint8_t* bytes,
                         std::uint8_t width,
                         std::uint32_t value) noexcept {
    bytes[0] = static_cast<std::uint8_t>(value);
    if (width >= 2u) bytes[1] = static_cast<std::uint8_t>(value >> 8);
    if (width == 4u) {
        bytes[2] = static_cast<std::uint8_t>(value >> 16);
        bytes[3] = static_cast<std::uint8_t>(value >> 24);
    }
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

void hash_bytes(std::uint64_t& hash, std::span<const std::uint8_t> bytes) noexcept {
    for (const auto value : bytes) {
        hash_byte(hash, value);
    }
}

} // namespace

Ps1MemoryBus::Ps1MemoryBus() : main_ram_(main_ram_size, 0u) {}

std::optional<std::uint32_t> Ps1MemoryBus::guest_to_physical(std::uint32_t guest) noexcept {
    if (guest < 0x80000000u) return guest;
    if (guest < 0xC0000000u) return guest & 0x1FFFFFFFu;
    return std::nullopt;
}

R3000aBusResult Ps1MemoryBus::read8(std::uint32_t address) noexcept {
    const auto physical = guest_to_physical(address);
    if (physical) {
        if (auto* p = mapped_bytes(*physical, 1u, main_ram_, scratchpad_)) {
            return {R3000aBusStatus::ok, read_little_endian(p, 1u)};
        }
        if (diagnostic_mmio_probe_enabled_) {
            if (auto* p = diagnostic_mmio_bytes(*physical, 1u, diagnostic_mmio_shadow_)) {
                const auto value = read_little_endian(p, 1u);
                last_diagnostic_mmio_probe_ = Ps1UnsupportedAccess{
                    address, *physical, 1u, false, value};
                return {R3000aBusStatus::ok, value};
            }
        }
    }
    last_unsupported_ = Ps1UnsupportedAccess{
        address, physical.value_or(address), 1u, false, 0u};
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1MemoryBus::read16(std::uint32_t address) noexcept {
    const auto physical = guest_to_physical(address);
    if (physical) {
        if (*physical == kInterruptMaskAddress) {
            return {R3000aBusStatus::ok, interrupt_mask_};
        }
        if (auto* p = mapped_bytes(*physical, 2u, main_ram_, scratchpad_)) {
            return {R3000aBusStatus::ok, read_little_endian(p, 2u)};
        }
        if (diagnostic_mmio_probe_enabled_) {
            if (auto* p = diagnostic_mmio_bytes(*physical, 2u, diagnostic_mmio_shadow_)) {
                const auto value = read_little_endian(p, 2u);
                last_diagnostic_mmio_probe_ = Ps1UnsupportedAccess{
                    address, *physical, 2u, false, value};
                return {R3000aBusStatus::ok, value};
            }
        }
    }
    last_unsupported_ = Ps1UnsupportedAccess{
        address, physical.value_or(address), 2u, false, 0u};
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1MemoryBus::read32(std::uint32_t address) noexcept {
    const auto physical = guest_to_physical(address);
    if (physical) {
        if (*physical == kDmaControlAddress) {
            return {R3000aBusStatus::ok, dma_control_};
        }
        if (*physical == kDmaInterruptAddress) {
            return {R3000aBusStatus::ok, visible_dma_interrupt(dma_interrupt_)};
        }
        if (auto* p = mapped_bytes(*physical, 4u, main_ram_, scratchpad_)) {
            return {R3000aBusStatus::ok, read_little_endian(p, 4u)};
        }
        if (diagnostic_mmio_probe_enabled_) {
            if (auto* p = diagnostic_mmio_bytes(*physical, 4u, diagnostic_mmio_shadow_)) {
                const auto value = read_little_endian(p, 4u);
                last_diagnostic_mmio_probe_ = Ps1UnsupportedAccess{
                    address, *physical, 4u, false, value};
                return {R3000aBusStatus::ok, value};
            }
        }
    }
    last_unsupported_ = Ps1UnsupportedAccess{
        address, physical.value_or(address), 4u, false, 0u};
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1MemoryBus::write8(std::uint32_t address, std::uint8_t value) noexcept {
    const auto physical = guest_to_physical(address);
    if (physical) {
        if (auto* p = mapped_bytes(*physical, 1u, main_ram_, scratchpad_)) {
            write_little_endian(p, 1u, value);
            return {R3000aBusStatus::ok, 0u};
        }
        if (diagnostic_mmio_probe_enabled_) {
            if (auto* p = diagnostic_mmio_bytes(*physical, 1u, diagnostic_mmio_shadow_)) {
                write_little_endian(p, 1u, value);
                last_diagnostic_mmio_probe_ = Ps1UnsupportedAccess{
                    address, *physical, 1u, true, value};
                return {R3000aBusStatus::ok, 0u};
            }
        }
    }
    last_unsupported_ = Ps1UnsupportedAccess{
        address, physical.value_or(address), 1u, true, value};
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1MemoryBus::write16(std::uint32_t address, std::uint16_t value) noexcept {
    const auto physical = guest_to_physical(address);
    if (physical) {
        if (*physical == kInterruptStatusAddress) {
            interrupt_status_ = static_cast<std::uint16_t>(interrupt_status_ & value & kInterruptValidBits);
            return {R3000aBusStatus::ok, 0u};
        }
        if (*physical == kInterruptMaskAddress) {
            interrupt_mask_ = static_cast<std::uint16_t>(value & kInterruptValidBits);
            return {R3000aBusStatus::ok, 0u};
        }
        if (auto* p = mapped_bytes(*physical, 2u, main_ram_, scratchpad_)) {
            write_little_endian(p, 2u, value);
            return {R3000aBusStatus::ok, 0u};
        }
        if (diagnostic_mmio_probe_enabled_) {
            if (auto* p = diagnostic_mmio_bytes(*physical, 2u, diagnostic_mmio_shadow_)) {
                write_little_endian(p, 2u, value);
                last_diagnostic_mmio_probe_ = Ps1UnsupportedAccess{
                    address, *physical, 2u, true, value};
                return {R3000aBusStatus::ok, 0u};
            }
        }
    }
    last_unsupported_ = Ps1UnsupportedAccess{
        address, physical.value_or(address), 2u, true, value};
    return {R3000aBusStatus::unsupported, 0u};
}

R3000aBusResult Ps1MemoryBus::write32(std::uint32_t address, std::uint32_t value) noexcept {
    const auto physical = guest_to_physical(address);
    if (physical) {
        if (*physical == kDmaControlAddress) {
            dma_control_ = value;
            return {R3000aBusStatus::ok, 0u};
        }
        if (*physical == kDmaInterruptAddress) {
            const auto flags = (dma_interrupt_ & kDmaInterruptFlagMask) &
                               ~(value & kDmaInterruptFlagMask);
            dma_interrupt_ = (value & kDmaInterruptControlMask) | flags;
            return {R3000aBusStatus::ok, 0u};
        }
        if (*physical == kTimer1ModeAddress) {
            timer1_mode_ = static_cast<std::uint16_t>(value & 0xFFFFu);
            timer1_counter_ = 0u;
            return {R3000aBusStatus::ok, 0u};
        }
        if (auto* p = mapped_bytes(*physical, 4u, main_ram_, scratchpad_)) {
            write_little_endian(p, 4u, value);
            return {R3000aBusStatus::ok, 0u};
        }
        if (diagnostic_mmio_probe_enabled_) {
            if (auto* p = diagnostic_mmio_bytes(*physical, 4u, diagnostic_mmio_shadow_)) {
                write_little_endian(p, 4u, value);
                last_diagnostic_mmio_probe_ = Ps1UnsupportedAccess{
                    address, *physical, 4u, true, value};
                return {R3000aBusStatus::ok, 0u};
            }
        }
    }
    last_unsupported_ = Ps1UnsupportedAccess{
        address, physical.value_or(address), 4u, true, value};
    return {R3000aBusStatus::unsupported, 0u};
}

Result<void> Ps1MemoryBus::load_main_ram(
    std::uint32_t guest_address,
    std::span<const std::uint8_t> bytes) {
    const auto physical = guest_to_physical(guest_address);
    if (!physical || *physical >= main_ram_size ||
        bytes.size() > static_cast<std::size_t>(main_ram_size - *physical)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "PS-X EXE payload destination is outside JoJo main RAM");
    }
    std::copy(bytes.begin(), bytes.end(), main_ram_.begin() + *physical);
    return Result<void>::success();
}

std::uint16_t Ps1MemoryBus::interrupt_mask() const noexcept {
    return interrupt_mask_;
}

std::uint32_t Ps1MemoryBus::dma_interrupt() const noexcept {
    return visible_dma_interrupt(dma_interrupt_);
}

std::uint16_t Ps1MemoryBus::timer1_counter() const noexcept {
    return timer1_counter_;
}

std::uint16_t Ps1MemoryBus::timer1_mode() const noexcept {
    return timer1_mode_;
}

std::uint64_t Ps1MemoryBus::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_bytes(hash, std::span<const std::uint8_t>{main_ram_.data(), main_ram_.size()});
    hash_bytes(hash, std::span<const std::uint8_t>{scratchpad_.data(), scratchpad_.size()});
    hash_u16(hash, interrupt_status_);
    hash_u16(hash, interrupt_mask_);
    hash_u32(hash, dma_control_);
    hash_u32(hash, dma_interrupt_);
    hash_u16(hash, timer1_counter_);
    hash_u16(hash, timer1_mode_);
    hash_bytes(hash, std::span<const std::uint8_t>{
        diagnostic_mmio_shadow_.data(), diagnostic_mmio_shadow_.size()});
    return hash;
}

void Ps1MemoryBus::set_diagnostic_mmio_probe_enabled(bool enabled) noexcept {
    if (enabled && !diagnostic_mmio_probe_enabled_) {
        diagnostic_mmio_shadow_.fill(0u);
    }
    diagnostic_mmio_probe_enabled_ = enabled;
    last_diagnostic_mmio_probe_.reset();
}

bool Ps1MemoryBus::diagnostic_mmio_probe_enabled() const noexcept {
    return diagnostic_mmio_probe_enabled_;
}

const std::optional<Ps1UnsupportedAccess>&
Ps1MemoryBus::last_diagnostic_mmio_probe() const noexcept {
    return last_diagnostic_mmio_probe_;
}

void Ps1MemoryBus::clear_last_diagnostic_mmio_probe() noexcept {
    last_diagnostic_mmio_probe_.reset();
}

const std::optional<Ps1UnsupportedAccess>&
Ps1MemoryBus::last_unsupported_access() const noexcept {
    return last_unsupported_;
}

void Ps1MemoryBus::clear_last_unsupported_access() noexcept {
    last_unsupported_.reset();
}

} // namespace jojo
