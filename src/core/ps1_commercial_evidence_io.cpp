#include "core/ps1_commercial_evidence_io.h"

#include "core/ps1_boot_report_io.h"

#include <fstream>
#include <iomanip>
#include <optional>
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

std::string fallback_name(Ps1BiosFallback fallback) {
    switch (fallback) {
        case Ps1BiosFallback::return_zero: return "return_zero";
        case Ps1BiosFallback::return_one: return "return_one";
        case Ps1BiosFallback::return_minus_one: return "return_minus_one";
        case Ps1BiosFallback::preserve_v0: return "preserve_v0";
    }
    return "unknown";
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
                                 "failed to atomically replace commercial frontier report");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (!ec) return Result<void>::success();
    cleanup_temp(temp);
    return Result<void>::failure(
        ErrorCode::io_error,
        "failed to atomically replace commercial frontier report: " + ec.message());
#endif
}

} // namespace

std::string format_ps1_commercial_evidence_report(
    const Ps1CommercialEvidenceReport& report) {
    std::ostringstream out;
    out << "format=jojo-commercial-frontier-v1\n";
    out << "source_format=" << report.source.source_format << '\n';
    out << "source_size=" << report.source.source_size << '\n';
    out << "source_hash_fnv1a64=" << report.source.source_hash_fnv1a64 << '\n';
    out << "revision_id=" << report.source.revision_id << '\n';
    out << "frontier=" << ps1_commercial_frontier_class_name(report.frontier) << '\n';
    out << "stop_reason=" << ps1_boot_stop_reason_name(report.boot.stop_reason) << '\n';
    out << "total_instructions_retired=" << report.total_instructions_retired << '\n';
    out << "segment_instructions_retired=" << report.boot.instructions_retired << '\n';
    out << "last_pc=" << hex32(report.boot.last_pc) << '\n';
    out << "last_opcode=" << optional_hex32(report.boot.last_opcode) << '\n';
    out << "diagnostic_probe_mode=" << (report.boot.diagnostic_probe_mode ? 1 : 0) << '\n';
    out << "speculative_mmio_count=" << report.boot.speculative_mmio_count << '\n';
    out << "bios_call_count=" << report.boot.bios_call_count << '\n';
    out << "interrupts_accepted=" << report.boot.interrupts_accepted << '\n';
    out << "dma_transfer_count=" << report.boot.dma_transfer_count << '\n';
    out << "gpu_gp0_command_count=" << report.boot.gpu_gp0_command_count << '\n';
    out << "gpu_gp1_command_count=" << report.boot.gpu_gp1_command_count << '\n';
    out << "vram_write_count=" << report.boot.vram_write_count << '\n';
    out << "presented_frames=" << report.boot.presented_frames << '\n';

    if (report.boot.cpu_diagnostic) {
        const auto& cpu = *report.boot.cpu_diagnostic;
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

    out << "trace_sample_count=" << report.boot.recent_trace.size() << '\n';
    for (std::size_t i = 0; i < report.boot.recent_trace.size(); ++i) {
        out << "trace_" << i << "_pc=" << hex32(report.boot.recent_trace[i].pc) << '\n';
        out << "trace_" << i << "_opcode=" << optional_hex32(report.boot.recent_trace[i].opcode) << '\n';
    }

    out << "bios_event_count=" << report.boot.recent_bios_calls.size() << '\n';
    for (std::size_t i = 0; i < report.boot.recent_bios_calls.size(); ++i) {
        const auto& event = report.boot.recent_bios_calls[i];
        out << "bios_event_" << i << "_pc=" << hex32(event.pc) << '\n';
        out << "bios_event_" << i << "_table=" << hex32(event.table_physical) << '\n';
        out << "bios_event_" << i << "_selector=" << hex32(event.selector) << '\n';
        out << "bios_event_" << i << "_a0=" << hex32(event.a0) << '\n';
        out << "bios_event_" << i << "_a1=" << hex32(event.a1) << '\n';
        out << "bios_event_" << i << "_a2=" << hex32(event.a2) << '\n';
        out << "bios_event_" << i << "_a3=" << hex32(event.a3) << '\n';
        out << "bios_event_" << i << "_ra=" << hex32(event.ra) << '\n';
    }

    out << "mmio_event_count=" << report.boot.recent_mmio.size() << '\n';
    for (std::size_t i = 0; i < report.boot.recent_mmio.size(); ++i) {
        const auto& event = report.boot.recent_mmio[i];
        out << "mmio_event_" << i << "_pc=" << hex32(event.pc) << '\n';
        out << "mmio_event_" << i << "_address=" << hex32(event.address) << '\n';
        out << "mmio_event_" << i << "_width=" << static_cast<unsigned>(event.width) << '\n';
        out << "mmio_event_" << i << "_write=" << (event.write ? 1 : 0) << '\n';
        out << "mmio_event_" << i << "_value=" << hex32(event.value) << '\n';
        out << "mmio_event_" << i << "_speculative=" << (event.speculative ? 1 : 0) << '\n';
    }

    out << "cdrom_event_count=" << report.boot.recent_cdrom_commands.size() << '\n';
    for (std::size_t i = 0; i < report.boot.recent_cdrom_commands.size(); ++i) {
        const auto& event = report.boot.recent_cdrom_commands[i];
        out << "cdrom_event_" << i << "_command=" << static_cast<unsigned>(event.command) << '\n';
        out << "cdrom_event_" << i << "_index=" << static_cast<unsigned>(event.index) << '\n';
        out << "cdrom_event_" << i << "_status=" << static_cast<unsigned>(event.status) << '\n';
    }

    if (report.boot.unsupported_access) {
        const auto& access = *report.boot.unsupported_access;
        out << "unsupported_guest_address=" << hex32(access.guest_address) << '\n';
        out << "unsupported_physical_address=" << hex32(access.physical_address) << '\n';
        out << "unsupported_width=" << static_cast<unsigned>(access.width) << '\n';
        out << "unsupported_write=" << (access.write ? 1 : 0) << '\n';
        out << "unsupported_value=" << hex32(access.value) << '\n';
    } else {
        out << "unsupported_guest_address=none\n";
        out << "unsupported_physical_address=none\n";
        out << "unsupported_width=none\n";
        out << "unsupported_write=none\n";
        out << "unsupported_value=none\n";
    }

    out << "diagnostic_decision_count=" << report.diagnostic_decisions.size() << '\n';
    for (std::size_t i = 0; i < report.diagnostic_decisions.size(); ++i) {
        const auto& decision = report.diagnostic_decisions[i];
        out << "diagnostic_decision_" << i << "_bios_table=" << hex32(decision.bios_table) << '\n';
        out << "diagnostic_decision_" << i << "_bios_selector=" << hex32(decision.bios_selector) << '\n';
        out << "diagnostic_decision_" << i << "_fallback=" << fallback_name(decision.fallback) << '\n';
    }

    return out.str();
}

Result<void> save_ps1_commercial_evidence_report_atomic(
    const std::filesystem::path& path,
    const Ps1CommercialEvidenceReport& report) {
    if (path.empty()) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "commercial frontier report path cannot be empty");
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed to create commercial frontier diagnostics directory: " + ec.message());
        }
    }

    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed to open temporary commercial frontier report");
        }
        const auto text = format_ps1_commercial_evidence_report(report);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            file.close();
            cleanup_temp(temp);
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed to write temporary commercial frontier report");
        }
    }

    return replace_file(temp, path);
}

} // namespace jojo
