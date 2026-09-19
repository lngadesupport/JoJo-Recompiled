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

std::string hex64_plain(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

std::string optional_u8(const std::optional<std::uint8_t>& value) {
    return value ? std::to_string(static_cast<unsigned>(*value)) : "none";
}

std::string optional_hex8(const std::optional<std::uint8_t>& value) {
    if (!value) return "none";
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase
        << std::setfill('0') << std::setw(2)
        << static_cast<unsigned>(*value);
    return out.str();
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
    out << "session_termination="
        << ps1_commercial_session_termination_name(report.session_termination)
        << '\n';
    out << "stop_reason=" << ps1_boot_stop_reason_name(report.boot.stop_reason) << '\n';
    out << "total_execution_steps=" << report.total_execution_steps << '\n';
    out << "total_instructions_retired=" << report.total_instructions_retired << '\n';
    out << "total_native_x64_instructions_retired="
        << report.total_native_x64_instructions_retired << '\n';
    out << "total_reference_instructions_retired="
        << report.total_reference_instructions_retired << '\n';
    out << "total_native_x64_cache_compilations="
        << report.total_native_x64_cache_compilations << '\n';
    out << "total_native_x64_cache_reuses="
        << report.total_native_x64_cache_reuses << '\n';
    out << "total_native_x64_cache_invalidations="
        << report.total_native_x64_cache_invalidations << '\n';
    out << "total_native_x64_cache_evictions="
        << report.total_native_x64_cache_evictions << '\n';
    out << "execution_segments=" << report.execution_segments << '\n';
    out << "completed_frames=" << report.completed_frames << '\n';
    out << "observed_non_black_frames="
        << report.observed_non_black_frames << '\n';
    out << "frame_change_count=" << report.frame_change_count << '\n';
    out << "pad0_poll_count=" << report.pad_poll_count[0] << '\n';
    out << "pad1_poll_count=" << report.pad_poll_count[1] << '\n';
    out << "pad0_pressed_poll_count="
        << report.pad_pressed_poll_count[0] << '\n';
    out << "pad1_pressed_poll_count="
        << report.pad_pressed_poll_count[1] << '\n';
    out << "memory_card0_read_sector_count="
        << report.memory_card_read_sector_count[0] << '\n';
    out << "memory_card0_write_sector_count="
        << report.memory_card_write_sector_count[0] << '\n';
    out << "memory_card1_read_sector_count="
        << report.memory_card_read_sector_count[1] << '\n';
    out << "memory_card1_write_sector_count="
        << report.memory_card_write_sector_count[1] << '\n';
    out << "memory_card0_changed_write_sector_count="
        << report.memory_card_changed_write_sector_count[0] << '\n';
    out << "memory_card1_changed_write_sector_count="
        << report.memory_card_changed_write_sector_count[1] << '\n';
    out << "session_dma_transfer_count="
        << report.session_dma_transfer_count << '\n';
    out << "session_cdrom_command_count="
        << report.session_cdrom_command_count << '\n';
    out << "session_gpu_gp0_word_count="
        << report.session_gpu_gp0_word_count << '\n';
    out << "session_gpu_gp1_command_count="
        << report.session_gpu_gp1_command_count << '\n';
    out << "session_vram_write_count="
        << report.session_vram_write_count << '\n';
    out << "session_vblank_count="
        << report.session_vblank_count << '\n';
    out << "spu_sample_frames=" << report.spu_sample_frames << '\n';
    out << "spu_nonzero_samples=" << report.spu_nonzero_samples << '\n';
    out << "interrupt_status=" << hex32(report.interrupt_status) << '\n';
    out << "interrupt_mask=" << hex32(report.interrupt_mask) << '\n';
    out << "cpu_cop0_status=" << hex32(report.cpu_cop0_status) << '\n';
    out << "cpu_cop0_cause=" << hex32(report.cpu_cop0_cause) << '\n';
    out << "cpu_external_interrupt_pending="
        << static_cast<unsigned>(report.cpu_external_interrupt_pending)
        << '\n';
    out << "bios_interrupt_hook_address=";
    if (report.bios_interrupt_hook_address) {
        out << hex32(*report.bios_interrupt_hook_address);
    } else {
        out << "none";
    }
    out << '\n';
    out << "gpu_display_enabled=" << (report.gpu_display.enabled ? 1 : 0) << '\n';
    out << "gpu_display_rgb24=" << (report.gpu_display.rgb24 ? 1 : 0) << '\n';
    out << "gpu_display_pal=" << (report.gpu_display.pal ? 1 : 0) << '\n';
    out << "gpu_display_interlaced=" << (report.gpu_display.interlaced ? 1 : 0) << '\n';
    out << "gpu_display_start_x=" << report.gpu_display.start_x << '\n';
    out << "gpu_display_start_y=" << report.gpu_display.start_y << '\n';
    out << "gpu_display_width=" << report.gpu_display.width << '\n';
    out << "gpu_display_height=" << report.gpu_display.height << '\n';
    out << "gpu_nonzero_vram_words=" << report.gpu_nonzero_vram_words << '\n';
    out << "gpu_display_region_nonzero_words="
        << report.gpu_display_region_nonzero_words << '\n';
    out << "gpu_nonzero_bounds_valid="
        << (report.gpu_nonzero_bounds_valid ? 1 : 0) << '\n';
    out << "spu_control=" << hex32(report.spu_control) << '\n';
    out << "spu_status=" << hex32(report.spu_status) << '\n';
    out << "spu_transfer_control=" << hex32(report.spu_transfer_control) << '\n';
    out << "spu_transfer_current_address="
        << report.spu_transfer_current_address << '\n';
    out << "spu_keyed_on_voice_count="
        << report.spu_keyed_on_voice_count << '\n';
    out << "spu_nonzero_sound_ram_bytes="
        << report.spu_nonzero_sound_ram_bytes << '\n';
    if (report.gpu_nonzero_bounds_valid) {
        out << "gpu_nonzero_min_x=" << report.gpu_nonzero_min_x << '\n';
        out << "gpu_nonzero_min_y=" << report.gpu_nonzero_min_y << '\n';
        out << "gpu_nonzero_max_x=" << report.gpu_nonzero_max_x << '\n';
        out << "gpu_nonzero_max_y=" << report.gpu_nonzero_max_y << '\n';
    } else {
        out << "gpu_nonzero_min_x=none\n"
            << "gpu_nonzero_min_y=none\n"
            << "gpu_nonzero_max_x=none\n"
            << "gpu_nonzero_max_y=none\n";
    }
    const auto validation = summarize_ps1_gameplay_validation(report);
    out << "validation_frame_observed=" << (validation.frame_observed ? 1 : 0) << '\n';
    out << "validation_dynamic_video_observed="
        << (validation.dynamic_video_observed ? 1 : 0) << '\n';
    out << "validation_controller_poll_observed="
        << (validation.controller_poll_observed ? 1 : 0) << '\n';
    out << "validation_controller_input_observed="
        << (validation.controller_input_observed ? 1 : 0) << '\n';
    out << "validation_audio_non_silent_observed="
        << (validation.audio_non_silent_observed ? 1 : 0) << '\n';
    out << "validation_memory_card_read_observed="
        << (validation.memory_card_read_observed ? 1 : 0) << '\n';
    out << "validation_memory_card_write_observed="
        << (validation.memory_card_write_observed ? 1 : 0) << '\n';
    out << "validation_memory_card_content_change_observed="
        << (validation.memory_card_content_change_observed ? 1 : 0) << '\n';
    out << "segment_execution_steps=" << report.boot.execution_steps << '\n';
    out << "segment_instructions_retired=" << report.boot.instructions_retired << '\n';
    out << "segment_native_x64_instructions_retired="
        << report.boot.native_x64_instructions_retired << '\n';
    out << "segment_reference_instructions_retired="
        << report.boot.reference_instructions_retired << '\n';
    out << "segment_native_x64_enabled="
        << (report.boot.native_x64_enabled ? 1 : 0) << '\n';
    out << "segment_native_x64_cache_compilations="
        << report.boot.native_x64_cache_compilations << '\n';
    out << "segment_native_x64_cache_reuses="
        << report.boot.native_x64_cache_reuses << '\n';
    out << "segment_native_x64_cache_invalidations="
        << report.boot.native_x64_cache_invalidations << '\n';
    out << "segment_native_x64_cache_evictions="
        << report.boot.native_x64_cache_evictions << '\n';
    out << "last_pc=" << hex32(report.boot.last_pc) << '\n';
    out << "last_opcode=" << optional_hex32(report.boot.last_opcode) << '\n';
    out << "diagnostic_probe_mode=" << (report.boot.diagnostic_probe_mode ? 1 : 0) << '\n';
    out << "speculative_mmio_count=" << report.boot.speculative_mmio_count << '\n';
    out << "bios_call_count=" << report.boot.bios_call_count << '\n';
    out << "interrupts_accepted=" << report.boot.interrupts_accepted << '\n';
    out << "dma_transfer_count=" << report.boot.dma_transfer_count << '\n';
    out << "gpu_gp0_command_count=" << report.boot.gpu_gp0_command_count << '\n';
    out << "gpu_gp1_command_count=" << report.boot.gpu_gp1_command_count << '\n';
    out << "unsupported_gpu_gp0_command="
        << optional_hex8(report.boot.unsupported_gpu_gp0_command) << '\n';
    out << "unsupported_gpu_gp1_command="
        << optional_hex8(report.boot.unsupported_gpu_gp1_command) << '\n';
    out << "vram_write_count=" << report.boot.vram_write_count << '\n';
    out << "presented_frames=" << report.boot.presented_frames << '\n';
    if (report.first_frame) {
        out << "frame_width=" << report.first_frame->width << '\n';
        out << "frame_height=" << report.first_frame->height << '\n';
        out << "frame_hash_fnv1a64=" << hex64_plain(report.first_frame->frame_hash_fnv1a64) << '\n';
        out << "frame_non_black_pixels=" << report.first_frame->non_black_pixels << '\n';
    } else {
        out << "frame_width=none\n"
            << "frame_height=none\n"
            << "frame_hash_fnv1a64=none\n"
            << "frame_non_black_pixels=none\n";
    }

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
        out << "cdrom_event_" << i << "_lba=" << event.lba << '\n';
        out << "cdrom_event_" << i << "_mode=" << static_cast<unsigned>(event.mode) << '\n';
        out << "cdrom_event_" << i << "_request=" << static_cast<unsigned>(event.request) << '\n';
        out << "cdrom_event_" << i << "_interrupt_flags=" << static_cast<unsigned>(event.interrupt_flags) << '\n';
        out << "cdrom_event_" << i << "_data_bytes=" << event.data_bytes << '\n';
        out << "cdrom_event_" << i << "_sector_buffer_bytes=" << event.sector_buffer_bytes << '\n';
        out << "cdrom_event_" << i << "_drive_queue_depth=" << static_cast<unsigned>(event.drive_queue_depth) << '\n';
        out << "cdrom_event_" << i << "_read_stream_active=" << (event.read_stream_active ? 1 : 0) << '\n';
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
