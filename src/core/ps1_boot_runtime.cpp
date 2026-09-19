#include "core/ps1_boot_runtime.h"

#include "core/mips_decoder.h"
#include "core/ps1_executable_loader.h"
#include "core/r3000a_reference_executor.h"

#include <array>
#include <new>
#include <set>
#include <span>
#include <utility>

namespace jojo {
namespace {

constexpr std::uint32_t kBiosA0 = 0x000000A0u;
constexpr std::uint32_t kBiosB0 = 0x000000B0u;
constexpr std::uint32_t kBiosC0 = 0x000000C0u;
constexpr std::uint32_t kA0SendGp1Command = 0x00000048u;
constexpr std::uint32_t kA0GpuCw = 0x00000049u;
constexpr std::uint32_t kA0GetGpuStatus = 0x0000004Du;
constexpr std::uint32_t kA0GpuSync = 0x0000004Eu;
constexpr std::uint32_t kGpuGp0 = 0x1F801810u;
constexpr std::uint32_t kGpuGp1 = 0x1F801814u;
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

    // A retail BIOS runs its CD-ROM initialization before transferring
    // control to the PS-X EXE. The commercial JoJo executable replaces the
    // BIOS CD IRQ callback but relies on the drive's host-interrupt mask
    // already being enabled. Reproduce that post-BIOS handoff through the
    // normal MMIO interface instead of changing the controller's reset state.
    const auto cd_bank_one = runtime.bus_.write8(0x1F801800u, 0x01u);
    const auto cd_irq_mask = runtime.bus_.write8(0x1F801802u, 0x1Fu);
    const auto cd_bank_zero = runtime.bus_.write8(0x1F801800u, 0x00u);
    if (cd_bank_one.status != R3000aBusStatus::ok ||
        cd_irq_mask.status != R3000aBusStatus::ok ||
        cd_bank_zero.status != R3000aBusStatus::ok) {
        return Result<Ps1BootRuntime>::failure(
            ErrorCode::invalid_installation,
            "failed to initialize post-BIOS CD-ROM interrupt mask");
    }

    const auto c0_exception_entry = runtime.bus_.write32(
        kPs1HleC0TableAddress + 6u * sizeof(std::uint32_t),
        kPs1HleExceptionHandlerAddress);
    if (c0_exception_entry.status != R3000aBusStatus::ok) {
        return Result<Ps1BootRuntime>::failure(
            ErrorCode::invalid_installation,
            "failed to initialize clean-room PS1 C0 HLE table");
    }

    // The retail BIOS exposes a B0 function-pointer table in low RAM.
    // JoJo fetches several pad functions from that table and calls them
    // indirectly, so populate real guest-side trampolines rather than only
    // supporting direct jumps to physical B0.
    struct B0Trampoline {
        std::uint32_t selector;
        std::uint32_t address;
    };
    constexpr std::array<B0Trampoline, 6> kB0Trampolines{{
        {0x12u, kPs1HleInitPad2HandlerAddress},
        {0x13u, kPs1HleStartPad2HandlerAddress},
        {0x14u, kPs1HleStopPad2HandlerAddress},
        {0x15u, kPs1HlePadInit2HandlerAddress},
        {0x16u, kPs1HlePadDrHandlerAddress},
        {0x5Bu, kPs1HleChangeClearPadHandlerAddress},
    }};
    constexpr std::uint32_t kJumpB0 =
        0x08000000u | ((kBiosB0 >> 2u) & 0x03FFFFFFu);

    for (const auto& trampoline : kB0Trampolines) {
        const auto table_entry = runtime.bus_.write32(
            kPs1HleB0TableAddress +
                trampoline.selector * sizeof(std::uint32_t),
            trampoline.address);
        const auto load_selector = runtime.bus_.write32(
            trampoline.address + 0u,
            0x24090000u | trampoline.selector); // addiu t1,zero,selector
        const auto jump_b0 = runtime.bus_.write32(
            trampoline.address + 4u,
            kJumpB0);
        const auto delay_nop = runtime.bus_.write32(
            trampoline.address + 8u,
            0x00000000u);
        if (table_entry.status != R3000aBusStatus::ok ||
            load_selector.status != R3000aBusStatus::ok ||
            jump_b0.status != R3000aBusStatus::ok ||
            delay_nop.status != R3000aBusStatus::ok) {
            return Result<Ps1BootRuntime>::failure(
                ErrorCode::invalid_installation,
                "failed to initialize clean-room PS1 B0 pad trampolines");
        }
    }

    runtime.native_text_begin_ =
        static_cast<std::uint64_t>(executable.metadata.text_load_address);
    runtime.native_text_end_ =
        runtime.native_text_begin_ +
        static_cast<std::uint64_t>(executable.metadata.text_size);
    return Result<Ps1BootRuntime>::success(std::move(runtime));
}

void Ps1BootRuntime::set_native_x64_enabled(bool enabled) noexcept {
    native_x64_enabled_ = enabled;
}

bool Ps1BootRuntime::native_x64_enabled() const noexcept {
    return native_x64_enabled_;
}

Ps1BootReport Ps1BootRuntime::run(const Ps1BootOptions& options) noexcept {
    Ps1BootReport report{};
    report.last_pc = cpu_.pc;
    report.diagnostic_probe_mode = options.diagnostic_mmio_probe;
    report.native_x64_enabled = native_x64_enabled_;
    bus_.set_diagnostic_mmio_probe_enabled(options.diagnostic_mmio_probe);
    const auto native_cache_at_start = native_x64_cache_.stats();

    const auto& hardware_at_start = bus_.hardware_services();
    const auto dma_transfer_count_at_start = hardware_at_start.completed_dma_transfer_count();
    const auto cdrom_command_count_at_start =
        hardware_at_start.cdrom().command_count();
    const auto gpu_gp0_word_count_at_start = hardware_at_start.gpu_gp0_word_count();
    const auto gpu_gp1_command_count_at_start = hardware_at_start.gpu_gp1_command_count();
    const auto vram_write_count_at_start = hardware_at_start.gpu_vram_write_count();

    const auto finish = [&](Ps1BootStopReason reason) {
        report.stop_reason = reason;
        const auto& hardware = bus_.hardware_services();
        report.dma_transfer_count =
            hardware.completed_dma_transfer_count() - dma_transfer_count_at_start;
        const auto& cdrom = hardware.cdrom();
        const auto cdrom_delta =
            cdrom.command_count() - cdrom_command_count_at_start;
        const auto& cdrom_history = cdrom.recent_commands();
        const auto history_size =
            static_cast<std::uint64_t>(cdrom_history.size());
        const auto history_take_u64 =
            cdrom_delta < history_size ? cdrom_delta : history_size;
        const auto history_take =
            static_cast<std::size_t>(history_take_u64);
        report.recent_cdrom_commands.reserve(history_take);
        for (std::size_t i = cdrom_history.size() - history_take;
             i < cdrom_history.size();
             ++i) {
            const auto& event = cdrom_history[i];
            report.recent_cdrom_commands.push_back(Ps1CdromCommandSummary{
                event.command,
                event.index,
                event.status,
                event.lba,
                event.mode,
                event.request,
                event.interrupt_flags,
                event.data_bytes,
                event.sector_buffer_bytes,
                event.drive_queue_depth,
                event.read_stream_active,
            });
        }
        report.gpu_gp0_command_count =
            hardware.gpu_gp0_word_count() - gpu_gp0_word_count_at_start;
        report.gpu_gp1_command_count =
            hardware.gpu_gp1_command_count() - gpu_gp1_command_count_at_start;
        report.unsupported_gpu_gp0_command =
            hardware.gpu().last_unsupported_gp0_command();
        report.unsupported_gpu_gp1_command =
            hardware.gpu().last_unsupported_gp1_command();
        report.vram_write_count =
            hardware.gpu_vram_write_count() - vram_write_count_at_start;
        const auto native_cache = native_x64_cache_.stats();
        report.native_x64_cache_compilations =
            native_cache.compilations - native_cache_at_start.compilations;
        report.native_x64_cache_reuses =
            native_cache.reuses - native_cache_at_start.reuses;
        report.native_x64_cache_invalidations =
            native_cache.invalidations - native_cache_at_start.invalidations;
        report.native_x64_cache_evictions =
            native_cache.evictions - native_cache_at_start.evictions;
        return report;
    };

    std::uint64_t instructions_since_progress = 0u;
    std::set<std::uint64_t> observed_bios_dependencies;
    std::set<std::uint64_t> observed_mmio_dependencies;

    const auto pump_hardware = [&]() noexcept {
        auto& hardware = bus_.hardware_services();
        hardware.step(1u);

        if (hardware.pending_dma_transfer()) {
            const auto channel =
                hardware.pending_dma_transfer()->channel;
            const bool completed =
                hardware.execute_pending_dma(
                    std::span<std::uint8_t>(
                        bus_.main_ram_data(),
                        Ps1MemoryBus::main_ram_size));
            if (completed && channel == 4u) {
                // PsyQ waits on the BIOS SPU hardware event after DMA4.
                // Spec 20h is the command-completed notification.
                bios_.deliver_event(0xF0000009u, 0x00000020u);
            }
        }

        cpu_.external_interrupt_pending =
            hardware.interrupt_pending() ? 0x04u : 0u;
    };

    while (report.execution_steps < options.instruction_budget) {
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
            if (*physical_pc == kBiosA0) {
                const auto selector = cpu_.gpr[9];
                if (selector == kA0SendGp1Command) {
                    const auto out = bus_.write32(kGpuGp1, cpu_.gpr[4]);
                    if (out.status != R3000aBusStatus::ok) {
                        report.unsupported_access = bus_.last_unsupported_access();
                        return finish(Ps1BootStopReason::gpu_command_unimplemented);
                    }
                    return_from_bios_call(cpu_);
                    diagnostic_bios_frontier_pending_ = false;
                    continue;
                }
                if (selector == kA0GpuCw) {
                    const auto out = bus_.write32(kGpuGp0, cpu_.gpr[4]);
                    if (out.status != R3000aBusStatus::ok) {
                        report.unsupported_access = bus_.last_unsupported_access();
                        return finish(Ps1BootStopReason::gpu_command_unimplemented);
                    }
                    cpu_.gpr[2] = 0u;
                    return_from_bios_call(cpu_);
                    diagnostic_bios_frontier_pending_ = false;
                    continue;
                }
                if (selector == kA0GetGpuStatus) {
                    cpu_.gpr[2] = bus_.hardware_services().gpu_status();
                    return_from_bios_call(cpu_);
                    diagnostic_bios_frontier_pending_ = false;
                    continue;
                }
                if (selector == kA0GpuSync) {
                    // The current ingress executes accepted commands synchronously.
                    cpu_.gpr[2] = 0u;
                    return_from_bios_call(cpu_);
                    diagnostic_bios_frontier_pending_ = false;
                    continue;
                }
            }

            const auto bios_status =
                bios_.dispatch(cpu_, *physical_pc, cpu_.gpr[9], &bus_);
            if (bios_status == Ps1HleBiosDispatchStatus::handled) {
                diagnostic_bios_frontier_pending_ = false;
                continue;
            }
            diagnostic_bios_frontier_pending_ = true;
            return finish(Ps1BootStopReason::bios_call_unimplemented);
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

        if (observed_opcode.status == R3000aBusStatus::ok &&
            decode_mips(observed_opcode.value).op == MipsOp::syscall &&
            !cpu_.delay_slot.active) {
            const auto syscall_status =
                bios_.dispatch_syscall(cpu_, cpu_.gpr[4]);
            if (syscall_status == Ps1HleBiosDispatchStatus::handled) {
                ++report.execution_steps;
                ++instructions_since_progress;
                pump_hardware();
                if (options.stagnation_instruction_limit != 0u &&
                    instructions_since_progress >=
                        options.stagnation_instruction_limit) {
                    return finish(Ps1BootStopReason::diagnostic_stall);
                }
                continue;
            }
        }

        const bool pc_in_native_text =
            native_text_end_ > native_text_begin_ &&
            cpu_.pc >= native_text_begin_ &&
            cpu_.pc < native_text_end_ &&
            static_cast<std::uint64_t>(cpu_.pc) + 4u <=
                static_cast<std::uint64_t>(native_text_end_);

        if (native_x64_enabled_ &&
            cpu_.external_interrupt_pending == 0u &&
            pc_in_native_text &&
            observed_opcode.status == R3000aBusStatus::ok) {
            const auto compiled =
                native_x64_cache_.get_or_compile_instruction(
                    cpu_.pc,
                    observed_opcode.value);
            if (compiled) {
                const auto native =
                    execute_r3000a_x64_block(
                        *compiled.value,
                        cpu_,
                        bus_.main_ram_data());
                if (native.status == R3000aX64ExecutionStatus::executed) {
                    ++report.execution_steps;
                    ++report.instructions_retired;
                    ++report.native_x64_instructions_retired;
                    ++instructions_since_progress;
                    pump_hardware();
                    if (options.stagnation_instruction_limit != 0u &&
                        instructions_since_progress >=
                            options.stagnation_instruction_limit) {
                        return finish(Ps1BootStopReason::diagnostic_stall);
                    }
                    continue;
                }
                if (native.status == R3000aX64ExecutionStatus::host_error) {
                    return finish(Ps1BootStopReason::fatal_runtime_error);
                }
            }
        }

        const auto cpu_before_step = cpu_;
        const auto step = step_r3000a(cpu_, bus_);
        if (step.status == R3000aStepStatus::retired) {
            ++report.execution_steps;
            ++report.instructions_retired;
            ++report.reference_instructions_retired;
            ++instructions_since_progress;
            pump_hardware();
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
                return finish(Ps1BootStopReason::diagnostic_stall);
            }
            continue;
        }

        if (step.status == R3000aStepStatus::exception) {
            ++report.execution_steps;
            pump_hardware();

            if (step.diagnostic.exception_code ==
                R3000aExceptionCode::interrupt) {
                ++report.interrupts_accepted;

                // step_r3000a has already retired any pending load and entered
                // the architectural exception vector. Build the thread state
                // that B(17h) ReturnFromException must restore, while retaining
                // the post-exception GPR result of that retired load.
                auto resume_state = cpu_;
                resume_state.pc = cpu_before_step.pc;
                resume_state.next_pc = cpu_before_step.next_pc;
                resume_state.pending_load = {};
                resume_state.delay_slot = cpu_before_step.delay_slot;
                resume_state.cop0.status =
                    cpu_before_step.cop0.status;
                resume_state.external_interrupt_pending =
                    cpu_.external_interrupt_pending;

                const auto hooked =
                    bios_.begin_interrupt_hook(
                        cpu_, resume_state, bus_);
                if (hooked ==
                    Ps1HleBiosDispatchStatus::handled) {
                    instructions_since_progress = 0u;
                    continue;
                }
            }
        }

        report.cpu_diagnostic = step.diagnostic;
        report.unsupported_access = bus_.last_unsupported_access();

        if (report.unsupported_access) {
            const auto physical = Ps1MemoryBus::guest_to_physical(
                report.unsupported_access->guest_address);
            if (physical && is_initial_mmio_window(*physical)) {
                record_recent_mmio(report, Ps1MmioSummary{
                    step.diagnostic.pc,
                    report.unsupported_access->guest_address,
                    report.unsupported_access->width,
                    report.unsupported_access->write,
                    report.unsupported_access->value,
                    false,
                }, options.mmio_event_capacity);
                const auto& hardware =
                    bus_.hardware_services();
                const auto& cdrom = hardware.cdrom();
                if (*physical == 0x1F801801u &&
                    report.unsupported_access->write &&
                    report.unsupported_access->width == 1u &&
                    cdrom.last_unsupported_command() &&
                    static_cast<std::uint8_t>(
                        report.unsupported_access->value) ==
                        *cdrom.last_unsupported_command()) {
                    return finish(
                        Ps1BootStopReason::device_command_unimplemented);
                }
                const auto& gpu = hardware.gpu();
                if ((physical == std::optional<std::uint32_t>{0x1F801810u} &&
                     gpu.last_unsupported_gp0_command()) ||
                    (physical == std::optional<std::uint32_t>{0x1F801814u} &&
                     gpu.last_unsupported_gp1_command())) {
                    return finish(Ps1BootStopReason::gpu_command_unimplemented);
                }
                return finish(Ps1BootStopReason::mmio_unimplemented);
            }
        }

        return finish(Ps1BootStopReason::cpu_boundary);
    }

    return finish(Ps1BootStopReason::execution_budget_exhausted);
}

void Ps1BootRuntime::signal_vblank() noexcept {
    auto& hardware = bus_.hardware_services();
    hardware.signal_vblank();
    bios_.service_pad_vblank(bus_, hardware.sio0());
    cpu_.external_interrupt_pending =
        hardware.interrupt_pending() ? 0x04u : 0u;
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

Ps1DisplayFrame Ps1BootRuntime::display_frame() const {
    return capture_ps1_display_frame(bus_.hardware_services().gpu());
}

Ps1BootRuntimeState Ps1BootRuntime::save_state() const {
    return Ps1BootRuntimeState{
        bus_,
        cpu_,
        bios_,
        native_text_begin_,
        native_text_end_,
        native_x64_enabled_,
        diagnostic_bios_frontier_pending_,
    };
}

Result<void> Ps1BootRuntime::load_state(const Ps1BootRuntimeState& state) {
    try {
        Ps1MemoryBus restored_bus = state.bus;
        Ps1HleBios restored_bios = state.bios;

        bus_ = std::move(restored_bus);
        cpu_ = state.cpu;
        bios_ = std::move(restored_bios);
        native_text_begin_ = state.native_text_begin;
        native_text_end_ = state.native_text_end;
        native_x64_enabled_ = state.native_x64_enabled;
        diagnostic_bios_frontier_pending_ =
            state.diagnostic_bios_frontier_pending;

        // Compiled host code is derived state. Never restore stale cache
        // entries across a guest-state rewind.
        native_x64_cache_.clear();
        return Result<void>::success();
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "insufficient memory while restoring PS1 runtime snapshot");
    }
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
