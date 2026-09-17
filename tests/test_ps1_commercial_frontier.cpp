#include "core/ps1_commercial_frontier.h"
#include "core/ps1_boot_report.h"

#include <iostream>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

jojo::Ps1BootReport report_with(jojo::Ps1BootStopReason reason) {
    jojo::Ps1BootReport report{};
    report.stop_reason = reason;
    return report;
}

jojo::Ps1BootReport mmio_at(std::uint32_t physical) {
    auto report = report_with(jojo::Ps1BootStopReason::mmio_unimplemented);
    report.unsupported_access = jojo::Ps1UnsupportedAccess{
        physical,
        physical,
        32u,
        true,
        0u,
    };
    return report;
}

jojo::Ps1BootReport gte_boundary(std::uint32_t opcode) {
    auto report = report_with(jojo::Ps1BootStopReason::cpu_boundary);
    report.last_pc = 0x80012340u;
    report.last_opcode = opcode;
    report.cpu_diagnostic = jojo::R3000aDiagnostic{
        jojo::R3000aBoundaryCode::cop2_unimplemented,
        jojo::R3000aStage::cop2,
        report.last_pc,
        opcode,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        2u,
        std::nullopt,
        std::nullopt,
    };
    return report;
}
}

int main() {
    using jojo::Ps1BootStopReason;
    using jojo::Ps1CommercialFrontierClass;
    using jojo::classify_ps1_commercial_frontier;

    CHECK(classify_ps1_commercial_frontier(report_with(Ps1BootStopReason::none)) ==
          Ps1CommercialFrontierClass::none);
    CHECK(classify_ps1_commercial_frontier(report_with(Ps1BootStopReason::execution_budget_exhausted)) ==
          Ps1CommercialFrontierClass::execution_budget);
    CHECK(classify_ps1_commercial_frontier(report_with(Ps1BootStopReason::bios_call_unimplemented)) ==
          Ps1CommercialFrontierClass::bios_call);
    CHECK(classify_ps1_commercial_frontier(report_with(Ps1BootStopReason::bios_call_unknown)) ==
          Ps1CommercialFrontierClass::bios_call);
    CHECK(classify_ps1_commercial_frontier(mmio_at(0x1F801080u)) ==
          Ps1CommercialFrontierClass::dma_operation);
    CHECK(classify_ps1_commercial_frontier(mmio_at(0x1F8010FCu)) ==
          Ps1CommercialFrontierClass::dma_operation);
    CHECK(classify_ps1_commercial_frontier(mmio_at(0x1F801810u)) ==
          Ps1CommercialFrontierClass::gpu_gp0_command);
    CHECK(classify_ps1_commercial_frontier(mmio_at(0x1F801814u)) ==
          Ps1CommercialFrontierClass::gpu_gp1_command);
    CHECK(classify_ps1_commercial_frontier(mmio_at(0x1F801070u)) ==
          Ps1CommercialFrontierClass::mmio_access);

    auto cdrom = report_with(Ps1BootStopReason::device_command_unimplemented);
    cdrom.recent_cdrom_commands.push_back({0x06u, 0u, 0u});
    CHECK(classify_ps1_commercial_frontier(cdrom) ==
          Ps1CommercialFrontierClass::cdrom_command);

    CHECK(classify_ps1_commercial_frontier(gte_boundary(0x4A000001u)) ==
          Ps1CommercialFrontierClass::gte_command);
    CHECK(classify_ps1_commercial_frontier(report_with(Ps1BootStopReason::cpu_boundary)) ==
          Ps1CommercialFrontierClass::cpu_boundary);
    CHECK(classify_ps1_commercial_frontier(report_with(Ps1BootStopReason::diagnostic_stall)) ==
          Ps1CommercialFrontierClass::diagnostic_stall);
    CHECK(classify_ps1_commercial_frontier(report_with(Ps1BootStopReason::commercial_frame_presented)) ==
          Ps1CommercialFrontierClass::commercial_frame_presented);
    CHECK(classify_ps1_commercial_frontier(report_with(Ps1BootStopReason::fatal_runtime_error)) ==
          Ps1CommercialFrontierClass::fatal_runtime_error);

    CHECK(jojo::ps1_commercial_frontier_class_name(Ps1CommercialFrontierClass::gpu_gp0_command) == "gpu_gp0_command");
    CHECK(jojo::ps1_commercial_frontier_class_name(Ps1CommercialFrontierClass::gte_command) == "gte_command");
    CHECK(jojo::ps1_commercial_frontier_class_name(Ps1CommercialFrontierClass::bios_call) == "bios_call");

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "commercial frontier classification passed\n";
    return 0;
}
