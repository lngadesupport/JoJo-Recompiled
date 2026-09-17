#include "core/ps1_hle_bios.h"

#include <cstddef>

namespace jojo {
namespace {

constexpr std::uint32_t kBiosA0 = 0x000000A0u;
constexpr std::uint32_t kBiosB0 = 0x000000B0u;
constexpr std::uint32_t kBiosC0 = 0x000000C0u;
constexpr std::uint32_t kA0InitHeap = 0x00000039u;
constexpr std::uint32_t kA0RemoveIso9660 = 0x00000056u;
constexpr std::uint32_t kA0RemoveIso9660Alias = 0x00000072u;
constexpr std::uint32_t kB0HookEntryInt = 0x00000019u;
constexpr std::uint32_t kB0ChangeClearPad = 0x0000005Bu;
constexpr std::uint32_t kC0ChangeClearRCnt = 0x0000000Au;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void return_from_bios_call(R3000aState& cpu) noexcept {
    cpu.pc = cpu.gpr[31];
    cpu.next_pc = cpu.pc + 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_bool(std::uint64_t& hash, bool value) noexcept {
    hash_byte(hash, static_cast<std::uint8_t>(value ? 1u : 0u));
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void hash_optional_u32(std::uint64_t& hash,
                       const std::optional<std::uint32_t>& value) noexcept {
    hash_bool(hash, value.has_value());
    if (value) hash_u32(hash, *value);
}

void hash_optional_bool(std::uint64_t& hash,
                        const std::optional<bool>& value) noexcept {
    hash_bool(hash, value.has_value());
    if (value) hash_bool(hash, *value);
}

} // namespace

Ps1HleBiosDispatchStatus Ps1HleBios::dispatch(
    R3000aState& cpu,
    std::uint32_t table_physical,
    std::uint32_t selector) noexcept {
    if (table_physical == kBiosA0 && selector == kA0InitHeap) {
        heap_state_ = Ps1BiosHeapState{cpu.gpr[4], cpu.gpr[5]};
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosA0 &&
        (selector == kA0RemoveIso9660 || selector == kA0RemoveIso9660Alias)) {
        iso9660_removed_ = true;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0HookEntryInt) {
        interrupt_hook_address_ = cpu.gpr[4];
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0ChangeClearPad) {
        pad_card_auto_ack_enabled_ = cpu.gpr[4] != 0u;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosC0 && selector == kC0ChangeClearRCnt) {
        if (cpu.gpr[4] >= root_counter_auto_ack_enabled_.size()) {
            return Ps1HleBiosDispatchStatus::unimplemented;
        }
        const auto index = static_cast<std::size_t>(cpu.gpr[4]);
        const bool previous = root_counter_auto_ack_enabled_[index].value_or(false);
        root_counter_auto_ack_enabled_[index] = cpu.gpr[5] != 0u;
        cpu.gpr[2] = previous ? 1u : 0u;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    return Ps1HleBiosDispatchStatus::unimplemented;
}

std::uint64_t Ps1HleBios::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_bool(hash, heap_state_.has_value());
    if (heap_state_) {
        hash_u32(hash, heap_state_->base);
        hash_u32(hash, heap_state_->size);
    }
    hash_optional_u32(hash, interrupt_hook_address_);
    hash_optional_bool(hash, pad_card_auto_ack_enabled_);
    for (const auto& state : root_counter_auto_ack_enabled_) {
        hash_optional_bool(hash, state);
    }
    hash_bool(hash, iso9660_removed_);
    return hash;
}

const std::optional<Ps1BiosHeapState>& Ps1HleBios::heap_state() const noexcept {
    return heap_state_;
}

const std::optional<std::uint32_t>& Ps1HleBios::interrupt_hook_address() const noexcept {
    return interrupt_hook_address_;
}

const std::optional<bool>& Ps1HleBios::pad_card_auto_ack_enabled() const noexcept {
    return pad_card_auto_ack_enabled_;
}

std::optional<bool> Ps1HleBios::root_counter_auto_ack_enabled(
    std::uint32_t counter) const noexcept {
    if (counter >= root_counter_auto_ack_enabled_.size()) {
        return std::nullopt;
    }
    return root_counter_auto_ack_enabled_[static_cast<std::size_t>(counter)];
}

bool Ps1HleBios::iso9660_removed() const noexcept {
    return iso9660_removed_;
}

} // namespace jojo
