#include "core/ps1_commercial_frontier.h"

namespace jojo {
namespace {

constexpr std::uint32_t kDmaStart = 0x1F801080u;
constexpr std::uint32_t kDmaEnd = 0x1F8010FFu;
constexpr std::uint32_t kGpuGp0 = 0x1F801810u;
constexpr std::uint32_t kGpuGp1 = 0x1F801814u;

} // namespace

Ps1CommercialFrontierClass classify_ps1_commercial_frontier(
    const Ps1BootReport& report) noexcept {
    switch (report.stop_reason) {
        case Ps1BootStopReason::none:
            return Ps1CommercialFrontierClass::none;
        case Ps1BootStopReason::execution_budget_exhausted:
            return Ps1CommercialFrontierClass::execution_budget;
        case Ps1BootStopReason::bios_call_unimplemented:
        case Ps1BootStopReason::bios_call_unknown:
            return Ps1CommercialFrontierClass::bios_call;
        case Ps1BootStopReason::mmio_unimplemented:
            if (report.unsupported_access) {
                const auto physical = report.unsupported_access->physical_address;
                if (physical >= kDmaStart && physical <= kDmaEnd) {
                    return Ps1CommercialFrontierClass::dma_operation;
                }
                if (physical == kGpuGp0) {
                    return Ps1CommercialFrontierClass::gpu_gp0_command;
                }
                if (physical == kGpuGp1) {
                    return Ps1CommercialFrontierClass::gpu_gp1_command;
                }
            }
            return Ps1CommercialFrontierClass::mmio_access;
        case Ps1BootStopReason::device_command_unimplemented:
            if (!report.recent_cdrom_commands.empty()) {
                return Ps1CommercialFrontierClass::cdrom_command;
            }
            return Ps1CommercialFrontierClass::mmio_access;
        case Ps1BootStopReason::gpu_command_unimplemented:
            return Ps1CommercialFrontierClass::mmio_access;
        case Ps1BootStopReason::cpu_boundary:
            return Ps1CommercialFrontierClass::cpu_boundary;
        case Ps1BootStopReason::diagnostic_stall:
            return Ps1CommercialFrontierClass::diagnostic_stall;
        case Ps1BootStopReason::commercial_frame_presented:
            return Ps1CommercialFrontierClass::commercial_frame_presented;
        case Ps1BootStopReason::fatal_runtime_error:
        case Ps1BootStopReason::installed_media_missing:
            return Ps1CommercialFrontierClass::fatal_runtime_error;
    }
    return Ps1CommercialFrontierClass::fatal_runtime_error;
}

std::string_view ps1_commercial_frontier_class_name(
    Ps1CommercialFrontierClass frontier) noexcept {
    switch (frontier) {
        case Ps1CommercialFrontierClass::none: return "none";
        case Ps1CommercialFrontierClass::execution_budget: return "execution_budget";
        case Ps1CommercialFrontierClass::bios_call: return "bios_call";
        case Ps1CommercialFrontierClass::mmio_access: return "mmio_access";
        case Ps1CommercialFrontierClass::cdrom_command: return "cdrom_command";
        case Ps1CommercialFrontierClass::dma_operation: return "dma_operation";
        case Ps1CommercialFrontierClass::gpu_gp0_command: return "gpu_gp0_command";
        case Ps1CommercialFrontierClass::gpu_gp1_command: return "gpu_gp1_command";
        case Ps1CommercialFrontierClass::cpu_boundary: return "cpu_boundary";
        case Ps1CommercialFrontierClass::diagnostic_stall: return "diagnostic_stall";
        case Ps1CommercialFrontierClass::commercial_frame_presented: return "commercial_frame_presented";
        case Ps1CommercialFrontierClass::fatal_runtime_error: return "fatal_runtime_error";
    }
    return "fatal_runtime_error";
}

} // namespace jojo
