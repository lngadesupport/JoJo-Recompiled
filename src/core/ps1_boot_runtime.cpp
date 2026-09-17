#include "core/ps1_boot_runtime.h"

#include "core/ps1_executable_loader.h"
#include "core/r3000a_reference_executor.h"

#include <set>
#include <utility>

namespace jojo {
namespace {

constexpr std::uint32_t kBiosA0 = 0x000000A0u;
constexpr std::uint32_t kBiosB0 = 0x000000B0u;
constexpr std::uint32_t kBiosC0 = 0x000000C0u;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

bool is_bios_table(std::uint32_t physical) noexcept {
    return physical == kBiosA0 ||
           physical == kBiosB0 ||
           physical == kBiosC0;
}

bool is_initial_mmio_window(std::uint32_t physical) noexcept {
    return physical >= 0x1F801000u && physical < 0x1F803000u;
}

std::uint64_t bios_dependency_key(std::uint32_t table, std::uint32_t selector) noexcept {
    return (static_cast<std::uint64_t>(table) << 32u) | selector;
}

std::uint64_t mmio_dependency_key(const Ps1UnsupportedAccess& access) noexcept {
    return (static_cast<std::uint64_t>(access.physical_address) << 16u) |
           (static_cast<std::uint64_t>(access.width) << 8u) |
           static_cast<std::uint64_t>(access.write ? 1u : 0u);
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

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void record_recent_trace(Ps1BootReport& report,
                         std::uint32_t pc,
                         const std::optional<std::uint32_t>& opcode,
                         std::size_t capacity) {
    if (capacity == 0u) return;
    if (report.recent_trace.size() == capacity) {
        report.recent_trace.pop_front();
    }
    report.recent_trace.push_back(Ps1TraceSample{pc, opcode});
}

void record_recent_bios(Ps1BootReport& report,
                        const Ps1BiosCallSummary& event,
                        std::size_t capacity) {
    if (capacity == 0u) return;
    if (report.recent_bios_calls.size() == capacity) {
        report.recent_bios_calls.erase(report.recent_bios_calls.begin());
    }
    report.recent_bios_calls.push_back(event);
}

void record_recent_mmio(Ps1BootReport& report,
                        const Ps1MmioSummary& event,
                        std::size_t capacity) {
    if (capacity == 0u) return;
    if (report.recent_mmio.size() == capacity) {
        report.recent_mmio.erase(report.recent_mmio.begin());
    }
    report.recent_mmio.push_back(event);
}

void return_from_bios_call(R3000aState& cpu) noexcept {
    cpu.pc = cpu.gpr[31];
    cpu.next_pc = cpu.pc + 4u;
    cpu.delay_slot = {};
    cpu.gpr[0] = 0u;
}

} // namespace

Result<Ps1BootRuntime> Ps1BootRuntime::create(const Ps1Executable& executable) {
    Ps1BootRuntime runtime{};
    auto loaded = load_ps1_executable_into_bus(runtime.bus_, executable);
    if (!loaded) {
        return Result<Ps1BootRuntime>::failure(loaded.error, loaded.detail);
    }
    runtime.cpu_ = std::move(loaded.value);
    return Result<Ps1BootRuntime>::success(std::move(runtime));
}

Ps1BootReport Ps1BootRuntime::run(const Ps1BootOptions& options) noexcept {
    Ps1BootReport report{};
    report.last_pc = cpu_.pc;
    report.diagnostic_probe_mode = options.diagnostic_mmio_probe;
    bus_.set_diagnostic_mmio_probe_enabled(options.diagnostic_mmio_probe);

    std::uint64_t instructions_since_progress = 0u;
    std::set<std::uint64_t> observed_bios_dependencies;
    std::set<std::uint64_t> observed_mmio_dependencies;

    while (report.instructions_retired < options.instruction_budget) {
        report.last_pc = cpu_.pc;

        const auto physical_pc = Ps1MemoryBus::guest_to_physical(cpu_.pc);
        if (physical_pc && is_bios_table(*physical_pc)) {
            if (observed_bios_dependencies.insert(
                    bios_dependency_key(*physical_pc, cpu_.gpr[9])).second) {
                instructions_since_progress = 0u;
            }
            ++report.bios_call_count;
            record_recent_bios(report, Ps1BiosCallSummary{
                cpu_.pc,
                *physical_pc,
                cpu_.gpr[9],
                cpu_.gpr[4],
                cpu_.gpr[5],
                cpu_.gpr[6],
                cpu_.gpr[7],
                cpu_.gpr[31],
            }, options.bios_event_capacity);
            const auto bios_status = bios_.dispatch(cpu_, *physical_pc, cpu_.gpr[9]);
            if (bios_status == Ps1HleBiosDispatchStatus::handled) {
                diagnostic_bios_frontier_pending_ = false;
                continue;
            }
            diagnostic_bios_frontier_pending_ = true;
            report.stop_reason = Ps1BootStopReason::bios_call_unimplemented;
            return report;
        }

        diagnostic_bios_frontier_pending_ = false;
        const auto observed_opcode = bus_.read32(cpu_.pc);
        if (observed_opcode.status == R3000aBusStatus::ok) {
            report.last_opcode = observed_opcode.value;
        } else {
            report.last_opcode.reset();
        }
        record_recent_trace(report, cpu_.pc, report.last_opcode, options.trace_capacity);
        bus_.clear_last_unsupported_access();
        bus_.clear_last_diagnostic_mmio_probe();

        const auto step = step_r3000a(cpu_, bus_);
        if (step.status == R3000aStepStatus::retired) {
            ++report.instructions_retired;
            ++instructions_since_progress;
            if (const auto& probe = bus_.last_diagnostic_mmio_probe(); probe) {
                ++report.speculative_mmio_count;
                record_recent_mmio(report, Ps1MmioSummary{
                    report.last_pc,
                    probe->guest_address,
                    probe->width,
                    probe->write,
                    probe->value,
                    true,
                }, options.mmio_event_capacity);
                if (observed_mmio_dependencies.insert(mmio_dependency_key(*probe)).second) {
                    instructions_since_progress = 0u;
                }
            }
            if (options.stagnation_instruction_limit != 0u &&
                instructions_since_progress >= options.stagnation_instruction_limit) {
                report.stop_reason = Ps1BootStopReason::diagnostic_stall;
                return report;
            }
            continue;
        }

        report.cpu_diagnostic = step.diagnostic;
        report.unsupported_access = bus_.last_unsupported_access();

        if (step.status == R3000aStepStatus::exception &&
            step.diagnostic.exception_code == R3000aExceptionCode::interrupt) {
            ++report.interrupts_accepted;
        }

        if (report.unsupported_access) {
            const auto physical = Ps1MemoryBus::guest_to_physical(
                report.unsupported_access->guest_address);
            if (physical && is_initial_mmio_window(*physical)) {
                report.stop_reason = Ps1BootStopReason::mmio_unimplemented;
                record_recent_mmio(report, Ps1MmioSummary{
                    step.diagnostic.pc,
                    report.unsupported_access->guest_address,
                    report.unsupported_access->width,
                    report.unsupported_access->write,
                    report.unsupported_access->value,
                    false,
                }, options.mmio_event_capacity);
                return report;
            }
        }

        report.stop_reason = Ps1BootStopReason::cpu_boundary;
        return report;
    }

    report.stop_reason = Ps1BootStopReason::execution_budget_exhausted;
    return report;
}

bool Ps1BootRuntime::apply_diagnostic_bios_fallback(Ps1BiosFallback fallback) noexcept {
    if (!diagnostic_bios_frontier_pending_) {
        return false;
    }
    const auto physical_pc = Ps1MemoryBus::guest_to_physical(cpu_.pc);
    if (!physical_pc || !is_bios_table(*physical_pc)) {
        diagnostic_bios_frontier_pending_ = false;
        return false;
    }

    switch (fallback) {
        case Ps1BiosFallback::return_zero:
            cpu_.gpr[2] = 0u;
            break;
        case Ps1BiosFallback::return_one:
            cpu_.gpr[2] = 1u;
            break;
        case Ps1BiosFallback::return_minus_one:
            cpu_.gpr[2] = 0xFFFFFFFFu;
            break;
        case Ps1BiosFallback::preserve_v0:
            break;
    }

    return_from_bios_call(cpu_);
    diagnostic_bios_frontier_pending_ = false;
    return true;
}

std::uint64_t Ps1BootRuntime::diagnostic_state_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_u64(hash, bus_.diagnostic_state_hash());
    for (const auto value : cpu_.gpr) hash_u32(hash, value);
    hash_u32(hash, cpu_.hi);
    hash_u32(hash, cpu_.lo);
    hash_u32(hash, cpu_.pc);
    hash_u32(hash, cpu_.next_pc);
    hash_bool(hash, cpu_.pending_load.valid);
    hash_byte(hash, cpu_.pending_load.reg);
    hash_u32(hash, cpu_.pending_load.value);
    hash_bool(hash, cpu_.delay_slot.active);
    hash_u32(hash, cpu_.delay_slot.branch_pc);
    hash_bool(hash, cpu_.delay_slot.taken);
    hash_u32(hash, cpu_.delay_slot.target);
    hash_u32(hash, cpu_.cop0.target_address);
    hash_u32(hash, cpu_.cop0.bad_vaddr);
    hash_u32(hash, cpu_.cop0.status);
    hash_u32(hash, cpu_.cop0.cause);
    hash_u32(hash, cpu_.cop0.epc);
    hash_byte(hash, cpu_.external_interrupt_pending);

    hash_u64(hash, bios_.diagnostic_state_hash());
    hash_bool(hash, diagnostic_bios_frontier_pending_);
    return hash;
}

const R3000aState& Ps1BootRuntime::cpu_state() const noexcept {
    return cpu_;
}

const std::optional<Ps1BiosHeapState>& Ps1BootRuntime::bios_heap_state() const noexcept {
    return bios_.heap_state();
}

const std::optional<std::uint32_t>&
Ps1BootRuntime::bios_interrupt_hook_address() const noexcept {
    return bios_.interrupt_hook_address();
}

const std::optional<bool>&
Ps1BootRuntime::bios_pad_card_auto_ack_enabled() const noexcept {
    return bios_.pad_card_auto_ack_enabled();
}

std::optional<bool> Ps1BootRuntime::bios_root_counter_auto_ack_enabled(
    std::uint32_t counter) const noexcept {
    return bios_.root_counter_auto_ack_enabled(counter);
}

bool Ps1BootRuntime::bios_iso9660_removed() const noexcept {
    return bios_.iso9660_removed();
}

Ps1MemoryBus& Ps1BootRuntime::bus() noexcept {
    return bus_;
}

const Ps1MemoryBus& Ps1BootRuntime::bus() const noexcept {
    return bus_;
}

} // namespace jojo
