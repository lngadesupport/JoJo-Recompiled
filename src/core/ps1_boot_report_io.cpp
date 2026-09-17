#include "core/ps1_boot_report_io.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#ifdef exception_code
#undef exception_code
#endif
#endif

namespace jojo {
namespace {

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

std::string optional_hex32(const std::optional<std::uint32_t>& value) {
    return value ? hex32(*value) : "none";
}

std::string optional_u8(const std::optional<std::uint8_t>& value) {
    return value ? std::to_string(static_cast<unsigned>(*value)) : "none";
}

void cleanup_temp(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

Result<void> replace_file(const std::filesystem::path& temp,
                          const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Result<void>::success();
    }
    cleanup_temp(temp);
    return Result<void>::failure(ErrorCode::io_error,
                                 "failed to atomically replace JoJo checkpoint report");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (!ec) return Result<void>::success();
    cleanup_temp(temp);
    return Result<void>::failure(
        ErrorCode::io_error,
        "failed to atomically replace JoJo checkpoint report: " + ec.message());
#endif
}

} // namespace

std::string ps1_boot_stop_reason_name(Ps1BootStopReason reason) noexcept {
    switch (reason) {
        case Ps1BootStopReason::none: return "none";
        case Ps1BootStopReason::execution_budget_exhausted: return "execution_budget_exhausted";
        case Ps1BootStopReason::cpu_boundary: return "cpu_boundary";
        case Ps1BootStopReason::bios_call_unimplemented: return "bios_call_unimplemented";
        case Ps1BootStopReason::bios_call_unknown: return "bios_call_unknown";
        case Ps1BootStopReason::mmio_unimplemented: return "mmio_unimplemented";
        case Ps1BootStopReason::installed_media_missing: return "installed_media_missing";
        case Ps1BootStopReason::device_command_unimplemented: return "device_command_unimplemented";
        case Ps1BootStopReason::gpu_command_unimplemented: return "gpu_command_unimplemented";
        case Ps1BootStopReason::commercial_frame_presented: return "commercial_frame_presented";
        case Ps1BootStopReason::fatal_runtime_error: return "fatal_runtime_error";
        case Ps1BootStopReason::diagnostic_stall: return "diagnostic_stall";
    }
    return "unknown";
}

std::string format_ps1_boot_report(const Ps1BootReport& report) {
    std::ostringstream out;
    out << "format="
        << (report.diagnostic_probe_mode ? "jojo-mega-checkpoint-v1" : "jojo-m3a-checkpoint-v1")
        << '\n';
    out << "stop_reason=" << ps1_boot_stop_reason_name(report.stop_reason) << '\n';
    out << "instructions_retired=" << report.instructions_retired << '\n';
    out << "last_pc=" << hex32(report.last_pc) << '\n';
    out << "last_opcode=" << optional_hex32(report.last_opcode) << '\n';
    out << "diagnostic_probe_mode=" << (report.diagnostic_probe_mode ? 1 : 0) << '\n';
    out << "speculative_mmio_count=" << report.speculative_mmio_count << '\n';
    out << "bios_call_count=" << report.bios_call_count << '\n';
    out << "interrupts_accepted=" << report.interrupts_accepted << '\n';
    out << "dma_transfer_count=" << report.dma_transfer_count << '\n';
    out << "gpu_gp0_command_count=" << report.gpu_gp0_command_count << '\n';
    out << "gpu_gp1_command_count=" << report.gpu_gp1_command_count << '\n';
    out << "vram_write_count=" << report.vram_write_count << '\n';
    out << "presented_frames=" << report.presented_frames << '\n';
    out << "trace_sample_count=" << report.recent_trace.size() << '\n';
    for (std::size_t i = 0; i < report.recent_trace.size(); ++i) {
        const auto& sample = report.recent_trace[i];
        out << "trace_" << i << "_pc=" << hex32(sample.pc) << '\n';
        out << "trace_" << i << "_opcode=" << optional_hex32(sample.opcode) << '\n';
    }

    out << "bios_event_count=" << report.recent_bios_calls.size() << '\n';
    for (std::size_t i = 0; i < report.recent_bios_calls.size(); ++i) {
        const auto& bios = report.recent_bios_calls[i];
        out << "bios_event_" << i << "_pc=" << hex32(bios.pc) << '\n';
        out << "bios_event_" << i << "_table=" << hex32(bios.table_physical) << '\n';
        out << "bios_event_" << i << "_selector=" << hex32(bios.selector) << '\n';
        out << "bios_event_" << i << "_a0=" << hex32(bios.a0) << '\n';
        out << "bios_event_" << i << "_a1=" << hex32(bios.a1) << '\n';
        out << "bios_event_" << i << "_a2=" << hex32(bios.a2) << '\n';
        out << "bios_event_" << i << "_a3=" << hex32(bios.a3) << '\n';
        out << "bios_event_" << i << "_ra=" << hex32(bios.ra) << '\n';
    }

    out << "mmio_event_count=" << report.recent_mmio.size() << '\n';
    for (std::size_t i = 0; i < report.recent_mmio.size(); ++i) {
        const auto& mmio = report.recent_mmio[i];
        out << "mmio_event_" << i << "_pc=" << hex32(mmio.pc) << '\n';
        out << "mmio_event_" << i << "_address=" << hex32(mmio.address) << '\n';
        out << "mmio_event_" << i << "_width=" << static_cast<unsigned>(mmio.width) << '\n';
        out << "mmio_event_" << i << "_write=" << (mmio.write ? 1 : 0) << '\n';
        out << "mmio_event_" << i << "_value=" << hex32(mmio.value) << '\n';
        out << "mmio_event_" << i << "_speculative=" << (mmio.speculative ? 1 : 0) << '\n';
    }

    if (report.recent_bios_calls.empty()) {
        out << "bios_last_pc=none\n"
            << "bios_last_table=none\n"
            << "bios_last_selector=none\n"
            << "bios_last_a0=none\n"
            << "bios_last_a1=none\n"
            << "bios_last_a2=none\n"
            << "bios_last_a3=none\n"
            << "bios_last_ra=none\n";
    } else {
        const auto& bios = report.recent_bios_calls.back();
        out << "bios_last_pc=" << hex32(bios.pc) << '\n';
        out << "bios_last_table=" << hex32(bios.table_physical) << '\n';
        out << "bios_last_selector=" << hex32(bios.selector) << '\n';
        out << "bios_last_a0=" << hex32(bios.a0) << '\n';
        out << "bios_last_a1=" << hex32(bios.a1) << '\n';
        out << "bios_last_a2=" << hex32(bios.a2) << '\n';
        out << "bios_last_a3=" << hex32(bios.a3) << '\n';
        out << "bios_last_ra=" << hex32(bios.ra) << '\n';
    }

    if (report.recent_mmio.empty()) {
        out << "mmio_last_pc=none\n"
            << "mmio_last_address=none\n"
            << "mmio_last_width=none\n"
            << "mmio_last_write=none\n"
            << "mmio_last_value=none\n"
            << "mmio_last_speculative=none\n";
    } else {
        const auto& mmio = report.recent_mmio.back();
        out << "mmio_last_pc=" << hex32(mmio.pc) << '\n';
        out << "mmio_last_address=" << hex32(mmio.address) << '\n';
        out << "mmio_last_width=" << static_cast<unsigned>(mmio.width) << '\n';
        out << "mmio_last_write=" << (mmio.write ? 1 : 0) << '\n';
        out << "mmio_last_value=" << hex32(mmio.value) << '\n';
        out << "mmio_last_speculative=" << (mmio.speculative ? 1 : 0) << '\n';
    }

    if (report.recent_cdrom_commands.empty()) {
        out << "cdrom_last_command=none\n"
            << "cdrom_last_index=none\n"
            << "cdrom_last_status=none\n";
    } else {
        const auto& cdrom = report.recent_cdrom_commands.back();
        out << "cdrom_last_command=" << static_cast<unsigned>(cdrom.command) << '\n';
        out << "cdrom_last_index=" << static_cast<unsigned>(cdrom.index) << '\n';
        out << "cdrom_last_status=" << static_cast<unsigned>(cdrom.status) << '\n';
    }

    if (report.cpu_diagnostic) {
        const auto& cpu = *report.cpu_diagnostic;
        out << "cpu_boundary=" << static_cast<unsigned>(cpu.boundary) << '\n';
        out << "cpu_stage=" << static_cast<unsigned>(cpu.stage) << '\n';
        out << "cpu_pc=" << hex32(cpu.pc) << '\n';
        out << "cpu_opcode=" << optional_hex32(cpu.opcode) << '\n';
        out << "cpu_address=" << optional_hex32(cpu.address) << '\n';
        out << "cpu_write_value=" << optional_hex32(cpu.write_value) << '\n';
        out << "cpu_access_width=" << optional_u8(cpu.access_width) << '\n';
        out << "cpu_coprocessor=" << optional_u8(cpu.coprocessor) << '\n';
        out << "cpu_register_index=" << optional_u8(cpu.register_index) << '\n';
        out << "cpu_exception_code="
            << (cpu.exception_code
                    ? std::to_string(static_cast<unsigned>(*cpu.exception_code))
                    : std::string("none"))
            << '\n';
    } else {
        out << "cpu_boundary=none\n"
            << "cpu_stage=none\n"
            << "cpu_pc=none\n"
            << "cpu_opcode=none\n"
            << "cpu_address=none\n"
            << "cpu_write_value=none\n"
            << "cpu_access_width=none\n"
            << "cpu_coprocessor=none\n"
            << "cpu_register_index=none\n"
            << "cpu_exception_code=none\n";
    }

    if (report.unsupported_access) {
        const auto& access = *report.unsupported_access;
        out << "unsupported_guest_address=" << hex32(access.guest_address) << '\n';
        out << "unsupported_physical_address=" << hex32(access.physical_address) << '\n';
        out << "unsupported_width=" << static_cast<unsigned>(access.width) << '\n';
        out << "unsupported_write=" << (access.write ? 1 : 0) << '\n';
        out << "unsupported_value=" << hex32(access.value) << '\n';
    } else {
        out << "unsupported_guest_address=none\n"
            << "unsupported_physical_address=none\n"
            << "unsupported_width=none\n"
            << "unsupported_write=none\n"
            << "unsupported_value=none\n";
    }

    return out.str();
}

Result<void> save_ps1_boot_report_atomic(
    const std::filesystem::path& path,
    const Ps1BootReport& report) {
    if (path.empty()) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "JoJo checkpoint report path cannot be empty");
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed to create JoJo checkpoint diagnostics directory: " + ec.message());
        }
    }

    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed to open temporary JoJo checkpoint report");
        }
        const auto text = format_ps1_boot_report(report);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            file.close();
            cleanup_temp(temp);
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed to write temporary JoJo checkpoint report");
        }
    }

    return replace_file(temp, path);
}

} // namespace jojo
