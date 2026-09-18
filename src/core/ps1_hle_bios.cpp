#include "core/ps1_hle_bios.h"

#include <cstddef>

namespace jojo {
namespace {

constexpr std::uint32_t kBiosA0 = 0x000000A0u;
constexpr std::uint32_t kBiosB0 = 0x000000B0u;
constexpr std::uint32_t kBiosC0 = 0x000000C0u;
constexpr std::uint32_t kA0Write = 0x00000003u;
constexpr std::uint32_t kA0InitHeap = 0x00000039u;
constexpr std::uint32_t kA0FlushCache = 0x00000044u;
constexpr std::uint32_t kA0RemoveIso9660 = 0x00000056u;
constexpr std::uint32_t kA0RemoveIso9660Alias = 0x00000072u;
constexpr std::uint32_t kA0BuInit = 0x00000055u;
constexpr std::uint32_t kA0BuInitAlias = 0x00000070u;
constexpr std::uint32_t kB0DeliverEvent = 0x00000007u;
constexpr std::uint32_t kB0OpenEvent = 0x00000008u;
constexpr std::uint32_t kB0CloseEvent = 0x00000009u;
constexpr std::uint32_t kB0WaitEvent = 0x0000000Au;
constexpr std::uint32_t kB0TestEvent = 0x0000000Bu;
constexpr std::uint32_t kB0EnableEvent = 0x0000000Cu;
constexpr std::uint32_t kB0DisableEvent = 0x0000000Du;
constexpr std::uint32_t kB0ReturnFromException = 0x00000017u;
constexpr std::uint32_t kB0ResetEntryInt = 0x00000018u;
constexpr std::uint32_t kB0HookEntryInt = 0x00000019u;
constexpr std::uint32_t kB0Write = 0x00000035u;
constexpr std::uint32_t kB0InitCard2 = 0x0000004Au;
constexpr std::uint32_t kB0StartCard2 = 0x0000004Bu;
constexpr std::uint32_t kB0StopCard2 = 0x0000004Cu;
constexpr std::uint32_t kB0GetC0Table = 0x00000056u;
constexpr std::uint32_t kB0GetB0Table = 0x00000057u;
constexpr std::uint32_t kB0ChangeClearPad = 0x0000005Bu;
constexpr std::uint32_t kC0SysEnqIntRP = 0x00000002u;
constexpr std::uint32_t kC0SysDeqIntRP = 0x00000003u;
constexpr std::uint32_t kC0ChangeClearRCnt = 0x0000000Au;
constexpr std::uint32_t kSysEnterCriticalSection = 0x00000001u;
constexpr std::uint32_t kSysExitCriticalSection = 0x00000002u;
constexpr std::uint32_t kCriticalStatusMask = (1u << 0u) | (1u << 10u);
constexpr std::uint32_t kEventDescriptorBase = 0xF1000000u;
constexpr std::uint32_t kEventModeCallback = 0x00001000u;
constexpr std::uint32_t kEventModeReady = 0x00002000u;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void return_from_bios_call(R3000aState& cpu) noexcept {
    cpu.pc = cpu.gpr[31];
    cpu.next_pc = cpu.pc + 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}

void retire_pending_load(R3000aState& cpu) noexcept {
    if (!cpu.pending_load.valid) return;
    if (cpu.pending_load.reg != 0u) {
        cpu.gpr[cpu.pending_load.reg] = cpu.pending_load.value;
    }
    cpu.pending_load = {};
}

void return_from_syscall(R3000aState& cpu) noexcept {
    retire_pending_load(cpu);
    cpu.pc = cpu.next_pc;
    cpu.next_pc += 4u;
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

void hash_r3000a_state(
    std::uint64_t& hash,
    const R3000aState& state) noexcept {
    for (const auto value : state.gpr) hash_u32(hash, value);
    hash_u32(hash, state.hi);
    hash_u32(hash, state.lo);
    hash_u32(hash, state.pc);
    hash_u32(hash, state.next_pc);
    hash_bool(hash, state.pending_load.valid);
    hash_byte(hash, state.pending_load.reg);
    hash_u32(hash, state.pending_load.value);
    hash_bool(hash, state.delay_slot.active);
    hash_u32(hash, state.delay_slot.branch_pc);
    hash_bool(hash, state.delay_slot.taken);
    hash_u32(hash, state.delay_slot.target);
    hash_u32(hash, state.cop0.target_address);
    hash_u32(hash, state.cop0.bad_vaddr);
    hash_u32(hash, state.cop0.status);
    hash_u32(hash, state.cop0.cause);
    hash_u32(hash, state.cop0.epc);
    for (const auto value : state.gte.data) hash_u32(hash, value);
    for (const auto value : state.gte.control) hash_u32(hash, value);
    hash_byte(hash, state.external_interrupt_pending);
}

} // namespace

Ps1HleBiosDispatchStatus Ps1HleBios::dispatch(
    R3000aState& cpu,
    std::uint32_t table_physical,
    std::uint32_t selector,
    R3000aBus* bus) noexcept {
    if (((table_physical == kBiosA0 && selector == kA0Write) ||
         (table_physical == kBiosB0 && selector == kB0Write)) &&
        (cpu.gpr[4] == 1u || cpu.gpr[4] == 2u)) {
        // stdout/stderr are diagnostic sinks in the BIOS-less runtime.
        cpu.gpr[2] = cpu.gpr[6];
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosA0 && selector == kA0InitHeap) {
        heap_state_ = Ps1BiosHeapState{cpu.gpr[4], cpu.gpr[5]};
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosA0 && selector == kA0FlushCache) {
        // Guest writes are immediately coherent in the HLE bus and the x64
        // cache fingerprints guest opcodes, so no host cache flush is needed.
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosA0 &&
        (selector == kA0BuInit || selector == kA0BuInitAlias)) {
        backup_unit_initialized_ = true;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosA0 &&
        (selector == kA0RemoveIso9660 || selector == kA0RemoveIso9660Alias)) {
        iso9660_removed_ = true;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0OpenEvent) {
        for (std::size_t i = 0u; i < events_.size(); ++i) {
            auto& event = events_[i];
            if (event.allocated) continue;
            event = Ps1BiosEventState{
                true,
                false,
                false,
                cpu.gpr[4],
                cpu.gpr[5],
                cpu.gpr[6],
                cpu.gpr[7],
            };
            cpu.gpr[2] = kEventDescriptorBase + static_cast<std::uint32_t>(i);
            return_from_bios_call(cpu);
            return Ps1HleBiosDispatchStatus::handled;
        }
        cpu.gpr[2] = 0xFFFFFFFFu;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    const auto decode_event = [this](std::uint32_t descriptor)
        -> Ps1BiosEventState* {
        if (descriptor < kEventDescriptorBase) return nullptr;
        const auto index = descriptor - kEventDescriptorBase;
        if (index >= events_.size()) return nullptr;
        auto& event = events_[static_cast<std::size_t>(index)];
        return event.allocated ? &event : nullptr;
    };

    if (table_physical == kBiosB0 && selector == kB0CloseEvent) {
        auto* event = decode_event(cpu.gpr[4]);
        cpu.gpr[2] = event ? 1u : 0u;
        if (event) *event = {};
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0EnableEvent) {
        auto* event = decode_event(cpu.gpr[4]);
        cpu.gpr[2] = event ? 1u : 0u;
        if (event) {
            event->enabled = true;
            event->ready = false;
        }
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0DisableEvent) {
        auto* event = decode_event(cpu.gpr[4]);
        cpu.gpr[2] = event ? 1u : 0u;
        if (event) {
            event->enabled = false;
            event->ready = false;
        }
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0TestEvent) {
        auto* event = decode_event(cpu.gpr[4]);
        if (!event || !event->enabled || !event->ready) {
            cpu.gpr[2] = 0u;
        } else {
            cpu.gpr[2] = 1u;
            if (event->mode == kEventModeReady) {
                event->ready = false;
            }
        }
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0WaitEvent) {
        auto* event = decode_event(cpu.gpr[4]);
        if (!event || !event->enabled) {
            cpu.gpr[2] = 0u;
            return_from_bios_call(cpu);
            return Ps1HleBiosDispatchStatus::handled;
        }
        if (event->ready) {
            cpu.gpr[2] = 1u;
            if (event->mode == kEventModeReady) {
                event->ready = false;
            }
            return_from_bios_call(cpu);
            return Ps1HleBiosDispatchStatus::handled;
        }
        // Enabled/busy ready-mode events block in the real BIOS. Keep the
        // call as an explicit frontier unless hardware has delivered it.
        return Ps1HleBiosDispatchStatus::unimplemented;
    }

    if (table_physical == kBiosB0 && selector == kB0DeliverEvent) {
        deliver_event(cpu.gpr[4], cpu.gpr[5]);
        cpu.gpr[2] = 1u;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 &&
        selector == kB0ReturnFromException) {
        if (!interrupt_resume_state_) {
            return Ps1HleBiosDispatchStatus::unimplemented;
        }

        // ReturnFromException restores the interrupted thread registers,
        // SR and PC. Cause/EPC remain the exception context, while the
        // external IRQ line reflects any I_STAT acknowledgement performed
        // by the guest hook before returning.
        const auto current_cause = cpu.cop0.cause;
        const auto current_epc = cpu.cop0.epc;
        const auto current_target = cpu.cop0.target_address;
        const auto current_bad_vaddr = cpu.cop0.bad_vaddr;
        const auto current_external_irq =
            cpu.external_interrupt_pending;

        cpu = *interrupt_resume_state_;
        cpu.cop0.cause = current_cause;
        cpu.cop0.epc = current_epc;
        cpu.cop0.target_address = current_target;
        cpu.cop0.bad_vaddr = current_bad_vaddr;
        cpu.external_interrupt_pending = current_external_irq;
        cpu.gpr[0] = 0u;
        interrupt_resume_state_.reset();
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0ResetEntryInt) {
        // The title ignores ResetEntryInt's BIOS-owned default-structure pointer.
        // Clear the custom HookEntryInt state without fabricating kernel memory.
        interrupt_hook_address_.reset();
        interrupt_resume_state_.reset();
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0HookEntryInt) {
        interrupt_hook_address_ = cpu.gpr[4];
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0InitCard2) {
        card_initialized_ = true;
        card_started_ = false;
        card_pad_enabled_ = cpu.gpr[4] != 0u;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0StartCard2) {
        if (!card_initialized_) {
            return Ps1HleBiosDispatchStatus::unimplemented;
        }
        card_started_ = true;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0StopCard2) {
        if (!card_initialized_) {
            return Ps1HleBiosDispatchStatus::unimplemented;
        }
        card_started_ = false;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0GetC0Table) {
        cpu.gpr[2] = kPs1HleC0TableAddress;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0GetB0Table) {
        cpu.gpr[2] = kPs1HleB0TableAddress;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosB0 && selector == kB0ChangeClearPad) {
        pad_card_auto_ack_enabled_ = cpu.gpr[4] != 0u;
        return_from_bios_call(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (table_physical == kBiosC0 &&
        (selector == kC0SysEnqIntRP ||
         selector == kC0SysDeqIntRP)) {
        const auto priority = cpu.gpr[4];
        const auto structure = cpu.gpr[5];
        if (priority >= interrupt_priority_heads_.size() ||
            structure == 0u) {
            return Ps1HleBiosDispatchStatus::unimplemented;
        }

        auto& head =
            interrupt_priority_heads_[
                static_cast<std::size_t>(priority)];

        if (selector == kC0SysEnqIntRP) {
            if (bus == nullptr) {
                return Ps1HleBiosDispatchStatus::unimplemented;
            }
            const auto next = head.value_or(0u);
            const auto written = bus->write32(structure, next);
            if (written.status != R3000aBusStatus::ok) {
                return Ps1HleBiosDispatchStatus::unimplemented;
            }
            head = structure;
        } else if (head && *head == structure) {
            if (bus == nullptr) {
                return Ps1HleBiosDispatchStatus::unimplemented;
            }
            const auto next = bus->read32(structure);
            if (next.status != R3000aBusStatus::ok) {
                return Ps1HleBiosDispatchStatus::unimplemented;
            }
            if (next.value == 0u) head.reset();
            else head = next.value;
        } else if (!head) {
            // BIOS-owned default priority-chain elements are not materialized
            // in the clean-room runtime. The commercial title removes one
            // such priority-2 element before installing its own handlers.
            // Treat that removal as complete while preserving guest memory.
        } else {
            // Real BIOS C(03h) is bugged beyond the first chain element.
            // Preserve the modeled chain rather than fabricating traversal.
        }

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

Ps1HleBiosDispatchStatus Ps1HleBios::begin_interrupt_hook(
    R3000aState& cpu,
    const R3000aState& resume_state,
    R3000aBus& bus) noexcept {
    if (!interrupt_hook_address_ ||
        interrupt_resume_state_) {
        return Ps1HleBiosDispatchStatus::unimplemented;
    }

    std::array<std::uint32_t, 12> jump_buffer{};
    for (std::size_t index = 0u;
         index < jump_buffer.size();
         ++index) {
        const auto read = bus.read32(
            *interrupt_hook_address_ +
            static_cast<std::uint32_t>(index * 4u));
        if (read.status != R3000aBusStatus::ok) {
            return Ps1HleBiosDispatchStatus::unimplemented;
        }
        jump_buffer[index] = read.value;
    }

    const auto hook_pc = jump_buffer[0];
    if (hook_pc == 0u) {
        return Ps1HleBiosDispatchStatus::unimplemented;
    }

    interrupt_resume_state_ = resume_state;

    // HookEntryInt uses the same 30h-byte ABI save area as setjmp:
    // RA, SP, FP, S0-S7 and GP. It re-enters the setjmp return site
    // with V0=1 while leaving exception-mode COP0 state active.
    cpu.gpr[31] = jump_buffer[0];
    cpu.gpr[29] = jump_buffer[1];
    cpu.gpr[30] = jump_buffer[2];
    for (std::size_t index = 0u; index < 8u; ++index) {
        cpu.gpr[16u + index] = jump_buffer[3u + index];
    }
    cpu.gpr[28] = jump_buffer[11];
    cpu.gpr[2] = 1u;
    cpu.pc = hook_pc;
    cpu.next_pc = hook_pc + 4u;
    cpu.pending_load = {};
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
    return Ps1HleBiosDispatchStatus::handled;
}

void Ps1HleBios::deliver_event(
    std::uint32_t event_class,
    std::uint32_t spec) noexcept {
    for (auto& event : events_) {
        if (!event.allocated || !event.enabled ||
            event.event_class != event_class ||
            event.spec != spec) {
            continue;
        }
        if (event.mode == kEventModeReady) {
            event.ready = true;
        } else if (event.mode == kEventModeCallback) {
            event.ready = false;
        }
    }
}

Ps1HleBiosDispatchStatus Ps1HleBios::dispatch_syscall(
    R3000aState& cpu,
    std::uint32_t selector) noexcept {
    if (cpu.delay_slot.active) {
        return Ps1HleBiosDispatchStatus::unimplemented;
    }

    if (selector == kSysEnterCriticalSection) {
        const bool was_enabled =
            (cpu.cop0.status & kCriticalStatusMask) == kCriticalStatusMask;
        cpu.cop0.status &= ~kCriticalStatusMask;
        cpu.gpr[2] = was_enabled ? 1u : 0u;
        return_from_syscall(cpu);
        return Ps1HleBiosDispatchStatus::handled;
    }

    if (selector == kSysExitCriticalSection) {
        cpu.cop0.status |= kCriticalStatusMask;
        return_from_syscall(cpu);
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
    hash_bool(hash, interrupt_resume_state_.has_value());
    if (interrupt_resume_state_) {
        hash_r3000a_state(hash, *interrupt_resume_state_);
    }
    hash_optional_bool(hash, pad_card_auto_ack_enabled_);
    hash_bool(hash, card_initialized_);
    hash_bool(hash, card_started_);
    hash_bool(hash, card_pad_enabled_);
    hash_bool(hash, backup_unit_initialized_);
    for (const auto& event : events_) {
        hash_bool(hash, event.allocated);
        hash_bool(hash, event.enabled);
        hash_bool(hash, event.ready);
        hash_u32(hash, event.event_class);
        hash_u32(hash, event.spec);
        hash_u32(hash, event.mode);
        hash_u32(hash, event.function);
    }
    for (const auto& state : root_counter_auto_ack_enabled_) {
        hash_optional_bool(hash, state);
    }
    for (const auto& head : interrupt_priority_heads_) {
        hash_optional_u32(hash, head);
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

bool Ps1HleBios::card_initialized() const noexcept {
    return card_initialized_;
}

bool Ps1HleBios::card_started() const noexcept {
    return card_started_;
}

bool Ps1HleBios::card_pad_enabled() const noexcept {
    return card_pad_enabled_;
}

bool Ps1HleBios::backup_unit_initialized() const noexcept {
    return backup_unit_initialized_;
}

const std::array<Ps1BiosEventState, 16>& Ps1HleBios::events() const noexcept {
    return events_;
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

std::optional<std::uint32_t>
Ps1HleBios::interrupt_priority_head(
    std::uint32_t priority) const noexcept {
    if (priority >= interrupt_priority_heads_.size()) {
        return std::nullopt;
    }
    return interrupt_priority_heads_[
        static_cast<std::size_t>(priority)];
}

} // namespace jojo
