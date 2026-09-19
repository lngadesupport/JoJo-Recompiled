#include "core/ps1_commercial_evidence.h"
#include "core/ps1_commercial_evidence_io.h"
#include "core/ps1_commercial_frontier.h"
#include "core/ps1_hle_bios.h"
#include "core/ps1_timing.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace {

std::uint64_t parse_u64(const char* text, std::uint64_t fallback) {
    try {
        const auto value = std::stoull(text);
        return value == 0u ? fallback : value;
    } catch (...) {
        return fallback;
    }
}

std::uint32_t parse_u32(const char* text, std::uint32_t fallback) {
    const auto value = parse_u64(text, fallback);
    if (value > std::numeric_limits<std::uint32_t>::max()) return fallback;
    return static_cast<std::uint32_t>(value);
}

jojo::Ps1VideoTimingMode timing_mode_from_display(
    const jojo::Ps1GpuDisplayState& display) noexcept {
    if (display.pal) {
        return display.interlaced
            ? jojo::Ps1VideoTimingMode::pal_interlaced
            : jojo::Ps1VideoTimingMode::pal_non_interlaced;
    }
    return display.interlaced
        ? jojo::Ps1VideoTimingMode::ntsc_interlaced
        : jojo::Ps1VideoTimingMode::ntsc_non_interlaced;
}

std::uint16_t scripted_buttons(std::uint64_t frame) noexcept {
    std::uint16_t buttons = 0xFFFFu;
    const auto press = [&](unsigned bit) {
        buttons = static_cast<std::uint16_t>(
            buttons & static_cast<std::uint16_t>(~(1u << bit)));
    };

    // Let the boot logos/title settle, then aggressively traverse the common
    // title/menu/character-select path with short edge-like button pulses.
    if (frame >= 120u) {
        const auto phase = static_cast<std::uint32_t>((frame - 120u) % 120u);
        if (phase < 2u) press(3u);                 // Start
        if (phase >= 16u && phase < 18u) press(14u); // Cross / confirm
        if (phase >= 32u && phase < 34u) press(14u); // Cross / confirm
        if (phase >= 48u && phase < 50u) press(5u);  // Right
        if (phase >= 64u && phase < 66u) press(14u); // Cross / confirm
        if (phase >= 80u && phase < 82u) press(6u);  // Down
        if (phase >= 96u && phase < 98u) press(14u); // Cross / confirm
        if (phase >= 110u && phase < 112u) {         // Fight activity
            press(14u); // Light
            press(15u); // Medium
            press(13u); // Heavy
            press(12u); // Stand
        }
    }
    return buttons;
}

bool save_ppm(
    const std::filesystem::path& path,
    const jojo::Ps1DisplayFrame& frame) {
    if (frame.width == 0u || frame.height == 0u ||
        frame.rgba8.size() !=
            static_cast<std::size_t>(frame.width) *
            static_cast<std::size_t>(frame.height)) {
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (const auto pixel : frame.rgba8) {
        const char rgb[3]{
            static_cast<char>(pixel & 0xFFu),
            static_cast<char>((pixel >> 8u) & 0xFFu),
            static_cast<char>((pixel >> 16u) & 0xFFu),
        };
        out.write(rgb, 3);
    }
    return static_cast<bool>(out);
}

int run_gameplay_probe(
    jojo::Ps1CommercialEvidenceRunner& runner,
    const std::filesystem::path& report_path,
    std::uint64_t segment_budget,
    std::uint32_t gameplay_frames,
    bool scripted_input,
    bool memory_card) {
    if (memory_card) {
        auto card_path = report_path;
        card_path += ".mcr";
        std::error_code ec;
        std::filesystem::remove(card_path, ec);
        const auto card = runner.load_or_create_memory_card(0u, card_path);
        if (!card) {
            std::cerr << "memory_card_error="
                      << static_cast<unsigned>(card.error) << "\n";
            std::cerr << "memory_card_detail=" << card.detail << "\n";
            return 5;
        }
    }

    jojo::Ps1CommercialFrameProgress frame_progress;
    jojo::Ps1VideoReferenceClock video_clock{
        timing_mode_from_display(runner.gpu_display_state())};
    std::uint64_t frame_ticks_remaining = video_clock.next_frame_ticks();
    std::uint64_t total_execution_steps = 0u;
    std::uint64_t total_instructions_retired = 0u;
    std::uint64_t total_native_retired = 0u;
    std::uint64_t total_reference_retired = 0u;
    std::uint64_t completed_frames = 0u;
    std::optional<std::uint64_t> first_pad_poll_frame{};
    std::array<std::uint64_t, 5> pad_bios_calls{};
    std::uint64_t pad_internal_set_calls = 0u;
    std::uint64_t pad_internal_clear_calls = 0u;
    jojo::Ps1BootReport last_boot{};
    auto frontier = jojo::Ps1CommercialFrontierClass::execution_budget;
    std::optional<jojo::Ps1DisplayFrame> last_non_black_frame;
    auto checkpoints_path = report_path;
    checkpoints_path += ".checkpoints.txt";
    std::ofstream checkpoints(
        checkpoints_path,
        std::ios::binary | std::ios::trunc);
    if (checkpoints) {
        checkpoints << "format=jojo-gameplay-checkpoints-v1\n";
    }

    while (completed_frames < gameplay_frames) {
        const auto counters_before_frame = runner.validation_counters();
        if (!first_pad_poll_frame &&
            counters_before_frame.pad_poll_count[0] != 0u) {
            first_pad_poll_frame = completed_frames;
        }

        const auto scripted_frame = first_pad_poll_frame
            ? completed_frames - *first_pad_poll_frame + 120u
            : completed_frames;
        runner.set_pad_buttons(
            0u,
            scripted_input ? scripted_buttons(scripted_frame) : 0xFFFFu);

        jojo::Ps1BootOptions options{};
        options.instruction_budget =
            std::min<std::uint64_t>(
                segment_budget,
                frame_ticks_remaining);
        // Long gameplay validation can retire hundreds of millions of
        // instructions. Keep only a compact frontier tail here; the legacy
        // first-frame/frontier mode below still retains its deep 4096 trace.
        options.trace_capacity = 64u;
        options.mmio_event_capacity = 128u;
        options.bios_event_capacity = 128u;
        options.stagnation_instruction_limit = 0u;

        last_boot = runner.run_segment(options);
        total_execution_steps += last_boot.execution_steps;
        total_instructions_retired += last_boot.instructions_retired;
        total_native_retired += last_boot.native_x64_instructions_retired;
        total_reference_retired += last_boot.reference_instructions_retired;

        {
            const auto cumulative = runner.validation_counters();
            pad_bios_calls = cumulative.pad_bios_call_count;
            pad_internal_set_calls =
                cumulative.pad_internal_set_call_count;
            pad_internal_clear_calls =
                cumulative.pad_internal_clear_call_count;
        }

        frontier = jojo::classify_ps1_commercial_frontier(last_boot);
        if (frontier != jojo::Ps1CommercialFrontierClass::execution_budget) {
            break;
        }
        if (last_boot.execution_steps == 0u ||
            last_boot.execution_steps > frame_ticks_remaining) {
            break;
        }

        frame_ticks_remaining -= last_boot.execution_steps;
        if (frame_ticks_remaining != 0u) continue;

        runner.signal_vblank();
        ++completed_frames;

        const auto frame = runner.display_frame();
        frame_progress.observe(frame);
        const auto frame_evidence =
            jojo::make_ps1_commercial_frame_evidence(frame);
        if (frame_evidence) {
            last_non_black_frame = frame;
        }

        if (completed_frames % 300u == 0u) {
            const auto checkpoint_counters =
                runner.validation_counters();
            if (checkpoints) {
                checkpoints
                    << "frame=" << completed_frames
                    << " pc=" << last_boot.last_pc
                    << " state_hash=" << runner.diagnostic_state_hash()
                    << " frame_hash="
                    << (frame_evidence
                            ? frame_evidence->frame_hash_fnv1a64
                            : 0u)
                    << " non_black_pixels="
                    << (frame_evidence
                            ? frame_evidence->non_black_pixels
                            : 0u)
                    << " pad0_polls="
                    << checkpoint_counters.pad_poll_count[0]
                    << " pad0_pressed="
                    << checkpoint_counters.pad_pressed_poll_count[0]
                    << " spu_nonzero="
                    << checkpoint_counters.spu_nonzero_samples
                    << " dma="
                    << checkpoint_counters.dma_transfer_count
                    << " cd="
                    << checkpoint_counters.cdrom_command_count
                    << " gp0="
                    << checkpoint_counters.gpu_gp0_word_count
                    << " vram_writes="
                    << checkpoint_counters.vram_write_count
                    << " bios_b12=" << checkpoint_counters.pad_bios_call_count[0]
                    << " bios_b13=" << checkpoint_counters.pad_bios_call_count[1]
                    << " bios_b14=" << checkpoint_counters.pad_bios_call_count[2]
                    << " bios_b15=" << checkpoint_counters.pad_bios_call_count[3]
                    << " bios_b16=" << checkpoint_counters.pad_bios_call_count[4]
                    << " pad_internal_set=" << checkpoint_counters.pad_internal_set_call_count
                    << " pad_internal_clear=" << checkpoint_counters.pad_internal_clear_call_count
                    << " sio_data_reads=" << checkpoint_counters.sio_data_read_count
                    << " sio_data_writes=" << checkpoint_counters.sio_data_write_count
                    << " sio_status_reads=" << checkpoint_counters.sio_status_read_count
                    << " sio_control_writes=" << checkpoint_counters.sio_control_write_count
                    << " sio_pad_addr_bytes=" << checkpoint_counters.sio_controller_address_byte_count
                    << " sio_pad_cmd_bytes=" << checkpoint_counters.sio_controller_command_byte_count
                    << " sio_id_stage=" << checkpoint_counters.sio_controller_id_high_stage_byte_count
                    << " sio_btn_lo_stage=" << checkpoint_counters.sio_controller_buttons_low_stage_byte_count
                    << " sio_btn_hi_stage=" << checkpoint_counters.sio_controller_buttons_high_stage_byte_count
                    << " sio_dtr_resets=" << checkpoint_counters.sio_dtr_fall_reset_count
                    << " sio_port_resets=" << checkpoint_counters.sio_port_change_reset_count
                    << " sio_ctrl_resets=" << checkpoint_counters.sio_control_reset_count
                    << " sio_card_addr_bytes=" << checkpoint_counters.sio_memory_card_address_byte_count
                    << " first_pad_poll_frame="
                    << (first_pad_poll_frame
                            ? std::to_string(*first_pad_poll_frame)
                            : std::string{"none"});
                if (!last_boot.recent_bios_calls.empty()) {
                    const auto& bios =
                        last_boot.recent_bios_calls.back();
                    checkpoints
                        << " bios_table=" << bios.table_physical
                        << " bios_selector=" << bios.selector;
                }
                checkpoints << '\n';
                checkpoints.flush();
            }

            if (frame_evidence) {
                auto checkpoint_frame_path = report_path;
                checkpoint_frame_path +=
                    ".frame" +
                    std::to_string(completed_frames) +
                    ".ppm";
                static_cast<void>(
                    save_ppm(checkpoint_frame_path, frame));
            }
        }

        // Keep the host-neutral PCM queue bounded. Validation counters are
        // cumulative inside the SPU and survive draining.
        static_cast<void>(runner.drain_audio_samples());

        video_clock.set_mode(
            timing_mode_from_display(runner.gpu_display_state()));
        frame_ticks_remaining = video_clock.next_frame_ticks();
    }

    const auto counters = runner.validation_counters();
    const auto validation_frame =
        frame_progress.first_frame().has_value() ? 1 : 0;
    const auto dynamic_video =
        frame_progress.frame_change_count() != 0u ? 1 : 0;
    const auto controller_poll =
        (counters.pad_poll_count[0] != 0u ||
         counters.pad_poll_count[1] != 0u) ? 1 : 0;
    const auto controller_input =
        (counters.pad_pressed_poll_count[0] != 0u ||
         counters.pad_pressed_poll_count[1] != 0u) ? 1 : 0;
    const auto audio_non_silent =
        counters.spu_nonzero_samples != 0u ? 1 : 0;
    const auto memory_read =
        (counters.memory_card_read_sector_count[0] != 0u ||
         counters.memory_card_read_sector_count[1] != 0u) ? 1 : 0;
    const auto memory_write =
        (counters.memory_card_write_sector_count[0] != 0u ||
         counters.memory_card_write_sector_count[1] != 0u) ? 1 : 0;

    auto frame_path = report_path;
    frame_path += ".ppm";
    bool frame_saved = false;
    if (last_non_black_frame) {
        frame_saved = save_ppm(frame_path, *last_non_black_frame);
    }

    std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
    if (!report) return 4;
    report << "format=jojo-gameplay-probe-v1\n";
    report << "revision_id=" << runner.disc_session().binding().revision_id << '\n';
    report << "source_size=" << runner.disc_session().binding().source_size << '\n';
    report << "source_hash_fnv1a64="
           << runner.disc_session().binding().source_hash_fnv1a64 << '\n';
    report << "frontier="
           << jojo::ps1_commercial_frontier_class_name(frontier) << '\n';
    report << "completed_frames=" << completed_frames << '\n';
    report << "first_pad_poll_frame=";
    if (first_pad_poll_frame) report << *first_pad_poll_frame;
    else report << "none";
    report << '\n';
    report << "total_execution_steps=" << total_execution_steps << '\n';
    report << "total_instructions_retired=" << total_instructions_retired << '\n';
    report << "native_x64_instructions_retired=" << total_native_retired << '\n';
    report << "reference_instructions_retired=" << total_reference_retired << '\n';
    report << "last_pc=" << last_boot.last_pc << '\n';
    report << "observed_non_black_frames="
           << frame_progress.observed_non_black_frames() << '\n';
    report << "frame_change_count=" << frame_progress.frame_change_count() << '\n';
    report << "validation_frame_observed=" << validation_frame << '\n';
    report << "validation_dynamic_video_observed=" << dynamic_video << '\n';
    report << "pad0_poll_count=" << counters.pad_poll_count[0] << '\n';
    report << "pad0_pressed_poll_count="
           << counters.pad_pressed_poll_count[0] << '\n';
    report << "validation_controller_poll_observed="
           << controller_poll << '\n';
    report << "validation_controller_input_observed="
           << controller_input << '\n';
    report << "bios_b12_initpad2_calls=" << pad_bios_calls[0] << '\n';
    report << "bios_b13_startpad2_calls=" << pad_bios_calls[1] << '\n';
    report << "bios_b14_stoppad2_calls=" << pad_bios_calls[2] << '\n';
    report << "bios_b15_pad_init2_calls=" << pad_bios_calls[3] << '\n';
    report << "bios_b16_pad_dr_calls=" << pad_bios_calls[4] << '\n';
    report << "bios_pad_internal_set_calls=" << pad_internal_set_calls << '\n';
    report << "bios_pad_internal_clear_calls=" << pad_internal_clear_calls << '\n';
    report << "sio_data_read_count=" << counters.sio_data_read_count << '\n';
    report << "sio_data_write_count=" << counters.sio_data_write_count << '\n';
    report << "sio_status_read_count=" << counters.sio_status_read_count << '\n';
    report << "sio_control_write_count=" << counters.sio_control_write_count << '\n';
    report << "sio_controller_address_byte_count="
           << counters.sio_controller_address_byte_count << '\n';
    report << "sio_controller_command_byte_count="
           << counters.sio_controller_command_byte_count << '\n';
    report << "sio_memory_card_address_byte_count="
           << counters.sio_memory_card_address_byte_count << '\n';
    report << "spu_sample_frames=" << counters.spu_sample_frames << '\n';
    report << "spu_nonzero_samples=" << counters.spu_nonzero_samples << '\n';
    report << "validation_audio_non_silent_observed="
           << audio_non_silent << '\n';
    report << "memory_card0_read_sector_count="
           << counters.memory_card_read_sector_count[0] << '\n';
    report << "memory_card0_write_sector_count="
           << counters.memory_card_write_sector_count[0] << '\n';
    report << "memory_card0_changed_write_sector_count="
           << counters.memory_card_changed_write_sector_count[0] << '\n';
    report << "validation_memory_card_read_observed=" << memory_read << '\n';
    report << "validation_memory_card_write_observed=" << memory_write << '\n';
    report << "dma_transfer_count=" << counters.dma_transfer_count << '\n';
    report << "cdrom_command_count=" << counters.cdrom_command_count << '\n';
    report << "gpu_gp0_word_count=" << counters.gpu_gp0_word_count << '\n';
    report << "gpu_gp1_command_count=" << counters.gpu_gp1_command_count << '\n';
    report << "vram_write_count=" << counters.vram_write_count << '\n';
    report << "vblank_count=" << counters.vblank_count << '\n';
    report << "frame_saved=" << (frame_saved ? 1 : 0) << '\n';
    if (frame_progress.first_frame()) {
        report << "first_frame_width=" << frame_progress.first_frame()->width << '\n';
        report << "first_frame_height=" << frame_progress.first_frame()->height << '\n';
        report << "first_frame_non_black_pixels="
               << frame_progress.first_frame()->non_black_pixels << '\n';
        report << "first_frame_hash_fnv1a64="
               << frame_progress.first_frame()->frame_hash_fnv1a64 << '\n';
    }

    if (memory_card) {
        const auto flushed = runner.flush_memory_cards();
        if (!flushed) {
            std::cerr << "memory_card_flush_error="
                      << static_cast<unsigned>(flushed.error) << "\n";
        }
    }

    std::cout << "frontier="
              << jojo::ps1_commercial_frontier_class_name(frontier) << "\n";
    std::cout << "completed_frames=" << completed_frames << "\n";
    std::cout << "first_pad_poll_frame=";
    if (first_pad_poll_frame) std::cout << *first_pad_poll_frame;
    else std::cout << "none";
    std::cout << "\n";
    std::cout << "observed_non_black_frames="
              << frame_progress.observed_non_black_frames() << "\n";
    std::cout << "frame_change_count="
              << frame_progress.frame_change_count() << "\n";
    std::cout << "pad0_poll_count=" << counters.pad_poll_count[0] << "\n";
    std::cout << "pad0_pressed_poll_count="
              << counters.pad_pressed_poll_count[0] << "\n";
    std::cout << "bios_b12_initpad2_calls=" << pad_bios_calls[0] << "\n";
    std::cout << "bios_b13_startpad2_calls=" << pad_bios_calls[1] << "\n";
    std::cout << "bios_b14_stoppad2_calls=" << pad_bios_calls[2] << "\n";
    std::cout << "bios_b15_pad_init2_calls=" << pad_bios_calls[3] << "\n";
    std::cout << "bios_b16_pad_dr_calls=" << pad_bios_calls[4] << "\n";
    std::cout << "bios_pad_internal_set_calls=" << pad_internal_set_calls << "\n";
    std::cout << "bios_pad_internal_clear_calls=" << pad_internal_clear_calls << "\n";
    std::cout << "sio_data_read_count=" << counters.sio_data_read_count << "\n";
    std::cout << "sio_data_write_count=" << counters.sio_data_write_count << "\n";
    std::cout << "sio_status_read_count=" << counters.sio_status_read_count << "\n";
    std::cout << "sio_control_write_count=" << counters.sio_control_write_count << "\n";
    std::cout << "sio_controller_address_byte_count="
              << counters.sio_controller_address_byte_count << "\n";
    std::cout << "sio_controller_command_byte_count="
              << counters.sio_controller_command_byte_count << "\n";
    std::cout << "sio_memory_card_address_byte_count="
              << counters.sio_memory_card_address_byte_count << "\n";
    std::cout << "spu_nonzero_samples="
              << counters.spu_nonzero_samples << "\n";
    std::cout << "memory_card0_read_sector_count="
              << counters.memory_card_read_sector_count[0] << "\n";
    std::cout << "memory_card0_write_sector_count="
              << counters.memory_card_write_sector_count[0] << "\n";
    std::cout << "report_path=" << report_path.string() << "\n";
    if (frame_saved) {
        std::cout << "frame_path=" << frame_path.string() << "\n";
    }
    return frontier == jojo::Ps1CommercialFrontierClass::execution_budget
        ? 0
        : 6;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr
            << "usage: jojo_ps1_commercial_probe <source.bin|source.cue|source.iso> "
               "<report.txt> [segments=128] [instructions_per_segment=500000] "
               "[native_x64=0|1] [gameplay_frames=0] [scripted_input=0|1] "
               "[memory_card=0|1]\n";
        return 2;
    }

    const std::filesystem::path source = argv[1];
    const std::filesystem::path report_path = argv[2];
    const auto max_segments = argc >= 4 ? parse_u32(argv[3], 128u) : 128u;
    const auto segment_budget =
        argc >= 5 ? parse_u64(argv[4], 500000u) : 500000u;
    const bool native_x64 =
        argc >= 6 ? parse_u32(argv[5], 0u) != 0u : false;
    const auto gameplay_frames =
        argc >= 7 ? parse_u32(argv[6], 0u) : 0u;
    const bool scripted_input =
        argc >= 8 ? parse_u32(argv[7], 0u) != 0u : false;
    const bool memory_card =
        argc >= 9 ? parse_u32(argv[8], 0u) != 0u : false;
#if (defined(_WIN32) && defined(_M_X64)) || \
    (defined(__linux__) && defined(__x86_64__))
    constexpr bool native_x64_backend_available = true;
#else
    constexpr bool native_x64_backend_available = false;
#endif

    auto runner = jojo::Ps1CommercialEvidenceRunner::open(source);
    if (!runner) {
        std::cerr << "open_error=" << static_cast<unsigned>(runner.error) << "\n";
        std::cerr << "open_detail=" << runner.detail << "\n";
        return 3;
    }

    runner.value.set_native_x64_enabled(native_x64);

    if (gameplay_frames != 0u) {
        return run_gameplay_probe(
            runner.value,
            report_path,
            segment_budget,
            gameplay_frames,
            scripted_input,
            memory_card);
    }

    jojo::Ps1CommercialEvidenceOptions options{};
    options.boot.instruction_budget = segment_budget;
    options.boot.trace_capacity = 4096u;
    options.boot.mmio_event_capacity = 1024u;
    options.boot.bios_event_capacity = 1024u;
    options.boot.stagnation_instruction_limit = 0u;
    options.max_execution_segments = max_segments;

    const auto report = runner.value.run(options);
    const auto saved =
        jojo::save_ps1_commercial_evidence_report_atomic(report_path, report);
    if (!saved) {
        std::cerr << "save_error=" << static_cast<unsigned>(saved.error) << "\n";
        std::cerr << "save_detail=" << saved.detail << "\n";
        return 4;
    }

    std::cout << "revision_id=" << report.source.revision_id << "\n";
    std::cout << "source_size=" << report.source.source_size << "\n";
    std::cout << "source_hash_fnv1a64=" << report.source.source_hash_fnv1a64 << "\n";
    std::cout << "native_x64_requested="
              << (native_x64 ? 1 : 0)
              << "\n";
    std::cout << "native_x64_backend_available="
              << (native_x64_backend_available ? 1 : 0)
              << "\n";
    std::cout << "native_x64_enabled="
              << (runner.value.native_x64_enabled() ? 1 : 0)
              << "\n";
    std::cout << "native_x64_instructions_retired="
              << report.total_native_x64_instructions_retired
              << "\n";
    std::cout << "reference_instructions_retired="
              << report.total_reference_instructions_retired
              << "\n";
    std::cout << "native_x64_cache_compilations="
              << report.total_native_x64_cache_compilations
              << "\n";
    std::cout << "native_x64_cache_reuses="
              << report.total_native_x64_cache_reuses
              << "\n";
    std::cout << "frontier="
              << jojo::ps1_commercial_frontier_class_name(report.frontier)
              << "\n";
    std::cout << "stop_reason="
              << static_cast<unsigned>(report.boot.stop_reason)
              << "\n";
    std::cout << "total_execution_steps="
              << report.total_execution_steps << "\n";
    std::cout << "total_instructions_retired="
              << report.total_instructions_retired << "\n";
    std::cout << "execution_segments=" << report.execution_segments << "\n";
    std::cout << "presented_frames=" << report.boot.presented_frames << "\n";
    if (report.first_frame) {
        std::cout << "frame_width=" << report.first_frame->width << "\n";
        std::cout << "frame_height=" << report.first_frame->height << "\n";
        std::cout << "frame_non_black_pixels="
                  << report.first_frame->non_black_pixels << "\n";
    }
    std::cout << "report_path=" << report_path.string() << "\n";
    return 0;
}
