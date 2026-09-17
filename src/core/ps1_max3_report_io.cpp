#include "core/ps1_boot_report_io.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace jojo {
namespace {

std::string hex32_max3(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

std::string hex64_max3(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

const char* dependency_kind_name(Ps1Max3DependencyKind kind) noexcept {
    switch (kind) {
        case Ps1Max3DependencyKind::bios_frontier: return "bios_frontier";
        case Ps1Max3DependencyKind::speculative_mmio: return "speculative_mmio";
    }
    return "unknown";
}

void cleanup_temp_max3(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

Result<void> replace_file_max3(const std::filesystem::path& temp,
                               const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Result<void>::success();
    }
    cleanup_temp_max3(temp);
    return Result<void>::failure(
        ErrorCode::io_error,
        "failed to atomically replace JoJo MAX3 checkpoint report");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (!ec) return Result<void>::success();
    cleanup_temp_max3(temp);
    return Result<void>::failure(
        ErrorCode::io_error,
        "failed to atomically replace JoJo MAX3 checkpoint report: " + ec.message());
#endif
}

} // namespace

std::string ps1_bios_fallback_name(Ps1BiosFallback fallback) noexcept {
    switch (fallback) {
        case Ps1BiosFallback::return_zero: return "return_zero";
        case Ps1BiosFallback::return_one: return "return_one";
        case Ps1BiosFallback::return_minus_one: return "return_minus_one";
        case Ps1BiosFallback::preserve_v0: return "preserve_v0";
    }
    return "unknown";
}

std::string ps1_max3_termination_reason_name(Ps1Max3TerminationReason reason) noexcept {
    switch (reason) {
        case Ps1Max3TerminationReason::completed: return "completed";
        case Ps1Max3TerminationReason::node_limit: return "node_limit";
        case Ps1Max3TerminationReason::total_retired_limit: return "total_retired_limit";
    }
    return "unknown";
}

std::string format_ps1_max3_report(const Ps1Max3Report& report) {
    std::ostringstream out;
    out << "format=jojo-max3-checkpoint-v1\n";
    out << "max_nodes=" << report.options.max_nodes << '\n';
    out << "max_branch_depth=" << report.options.max_branch_depth << '\n';
    out << "max_total_retired=" << report.options.max_total_retired << '\n';
    out << "segment_instruction_budget=" << report.options.segment_options.instruction_budget << '\n';
    out << "trace_capacity=" << report.options.segment_options.trace_capacity << '\n';
    out << "diagnostic_mmio_probe=" << (report.options.segment_options.diagnostic_mmio_probe ? 1 : 0) << '\n';
    out << "mmio_event_capacity=" << report.options.segment_options.mmio_event_capacity << '\n';
    out << "bios_event_capacity=" << report.options.segment_options.bios_event_capacity << '\n';
    out << "stagnation_instruction_limit="
        << report.options.segment_options.stagnation_instruction_limit << '\n';
    out << "termination_reason="
        << ps1_max3_termination_reason_name(report.termination_reason) << '\n';
    out << "total_retired=" << report.total_retired << '\n';
    out << "node_count=" << report.nodes.size() << '\n';
    out << "best_node=" << report.best_node << '\n';

    out << "best_path_count=" << report.best_path.size() << '\n';
    for (std::size_t i = 0; i < report.best_path.size(); ++i) {
        const auto& decision = report.best_path[i];
        out << "best_path_" << i << "_table=" << hex32_max3(decision.table) << '\n';
        out << "best_path_" << i << "_selector=" << hex32_max3(decision.selector) << '\n';
        out << "best_path_" << i << "_fallback="
            << ps1_bios_fallback_name(decision.fallback) << '\n';
    }

    out << "dependency_count=" << report.dependencies.size() << '\n';
    for (std::size_t i = 0; i < report.dependencies.size(); ++i) {
        const auto& dependency = report.dependencies[i];
        out << "dependency_" << i << "_kind=" << dependency_kind_name(dependency.kind) << '\n';
        if (dependency.kind == Ps1Max3DependencyKind::bios_frontier) {
            out << "dependency_" << i << "_table=" << hex32_max3(dependency.table) << '\n';
            out << "dependency_" << i << "_selector=" << hex32_max3(dependency.selector) << '\n';
            out << "dependency_" << i << "_address=none\n";
            out << "dependency_" << i << "_width=none\n";
            out << "dependency_" << i << "_write=none\n";
        } else {
            out << "dependency_" << i << "_table=none\n";
            out << "dependency_" << i << "_selector=none\n";
            out << "dependency_" << i << "_address=" << hex32_max3(dependency.address) << '\n';
            out << "dependency_" << i << "_width=" << static_cast<unsigned>(dependency.width) << '\n';
            out << "dependency_" << i << "_write=" << (dependency.write ? 1 : 0) << '\n';
        }
    }

    for (const auto& node : report.nodes) {
        const auto prefix = std::string("node_") + std::to_string(node.index) + "_";
        out << prefix << "parent="
            << (node.parent ? std::to_string(*node.parent) : std::string("none")) << '\n';
        out << prefix << "depth=" << node.depth << '\n';
        out << prefix << "fallback="
            << (node.fallback ? ps1_bios_fallback_name(*node.fallback) : std::string("none")) << '\n';
        out << prefix << "stop_reason=" << ps1_boot_stop_reason_name(node.stop_reason) << '\n';
        out << prefix << "segment_retired=" << node.segment_retired << '\n';
        out << prefix << "cumulative_retired=" << node.cumulative_retired << '\n';
        out << prefix << "state_hash=" << hex64_max3(node.state_hash) << '\n';
        out << prefix << "deduplicated=" << (node.deduplicated ? 1 : 0) << '\n';
        out << prefix << "frontier_table="
            << (node.frontier_table ? hex32_max3(node.frontier_table) : std::string("none")) << '\n';
        out << prefix << "frontier_selector="
            << (node.frontier_table ? hex32_max3(node.frontier_selector) : std::string("none")) << '\n';
        out << prefix << "path_presented_frames=" << node.path_presented_frames << '\n';
        out << prefix << "path_vram_write_count=" << node.path_vram_write_count << '\n';
        out << prefix << "path_gpu_gp0_command_count=" << node.path_gpu_gp0_command_count << '\n';
        out << prefix << "path_gpu_gp1_command_count=" << node.path_gpu_gp1_command_count << '\n';
        out << prefix << "path_cdrom_command_count=" << node.path_cdrom_command_count << '\n';
        out << prefix << "path_dma_transfer_count=" << node.path_dma_transfer_count << '\n';
        out << prefix << "path_dependency_count=" << node.path_dependency_count << '\n';
    }

    out << "best_report_begin=1\n";
    out << format_ps1_boot_report(report.best_report);
    out << "best_report_end=1\n";
    return out.str();
}

Result<void> save_ps1_max3_report_atomic(
    const std::filesystem::path& path,
    const Ps1Max3Report& report) {
    if (path.empty()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "JoJo MAX3 checkpoint report path cannot be empty");
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed to create JoJo MAX3 diagnostics directory: " + ec.message());
        }
    }

    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed to open temporary JoJo MAX3 checkpoint report");
        }
        const auto text = format_ps1_max3_report(report);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            file.close();
            cleanup_temp_max3(temp);
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed to write temporary JoJo MAX3 checkpoint report");
        }
    }

    return replace_file_max3(temp, path);
}

} // namespace jojo
