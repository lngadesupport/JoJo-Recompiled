#pragma once

#include "core/ps1_memory_bus.h"
#include "core/r3000a_diagnostics.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <vector>

namespace jojo {

enum class Ps1BootStopReason : std::uint8_t {
    none,
    execution_budget_exhausted,
    cpu_boundary,
    bios_call_unimplemented,
    bios_call_unknown,
    mmio_unimplemented,
    installed_media_missing,
    device_command_unimplemented,
    gpu_command_unimplemented,
    commercial_frame_presented,
    fatal_runtime_error,
    diagnostic_stall,
};

struct Ps1BiosCallSummary {
    std::uint32_t pc{};
    std::uint32_t table_physical{};
    std::uint32_t selector{};
    std::uint32_t a0{};
    std::uint32_t a1{};
    std::uint32_t a2{};
    std::uint32_t a3{};
    std::uint32_t ra{};
};

struct Ps1MmioSummary {
    std::uint32_t pc{};
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};
    std::uint32_t value{};
    bool speculative{};
};

struct Ps1CdromCommandSummary {
    std::uint8_t command{};
    std::uint8_t index{};
    std::uint8_t status{};
};

struct Ps1TraceSample {
    std::uint32_t pc{};
    std::optional<std::uint32_t> opcode{};
};

struct Ps1BootReport {
    std::uint64_t instructions_retired{};
    std::uint32_t last_pc{};
    std::optional<std::uint32_t> last_opcode{};
    Ps1BootStopReason stop_reason{Ps1BootStopReason::none};
    bool diagnostic_probe_mode{};
    std::uint64_t speculative_mmio_count{};
    std::uint64_t bios_call_count{};
    std::vector<Ps1BiosCallSummary> recent_bios_calls;
    std::vector<Ps1MmioSummary> recent_mmio;
    std::uint64_t interrupts_accepted{};
    std::uint64_t dma_transfer_count{};
    std::vector<Ps1CdromCommandSummary> recent_cdrom_commands;
    std::uint64_t gpu_gp0_command_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t vram_write_count{};
    std::uint64_t presented_frames{};
    std::deque<Ps1TraceSample> recent_trace;
    std::optional<R3000aDiagnostic> cpu_diagnostic{};
    std::optional<Ps1UnsupportedAccess> unsupported_access{};
};

struct Ps1BootOptions {
    std::uint64_t instruction_budget{10000u};
    std::size_t trace_capacity{16u};
    bool diagnostic_mmio_probe{false};
    std::size_t mmio_event_capacity{16u};
    std::size_t bios_event_capacity{16u};
    std::uint64_t stagnation_instruction_limit{};
};

[[nodiscard]] constexpr Ps1BootOptions ps1_local_evidence_options() noexcept {
    return Ps1BootOptions{
        std::numeric_limits<std::uint64_t>::max(),
        4096u,
        true,
        8192u,
        4096u,
        0u,
    };
}

} // namespace jojo
