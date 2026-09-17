#pragma once

#include "core/ps1_boot_report.h"

#include <cstdint>
#include <string_view>

namespace jojo {

enum class Ps1CommercialFrontierClass : std::uint8_t {
    none,
    execution_budget,
    bios_call,
    mmio_access,
    cdrom_command,
    dma_operation,
    gpu_gp0_command,
    gpu_gp1_command,
    cpu_boundary,
    diagnostic_stall,
    commercial_frame_presented,
    fatal_runtime_error,
};

[[nodiscard]] Ps1CommercialFrontierClass classify_ps1_commercial_frontier(
    const Ps1BootReport& report) noexcept;

[[nodiscard]] std::string_view ps1_commercial_frontier_class_name(
    Ps1CommercialFrontierClass frontier) noexcept;

} // namespace jojo
