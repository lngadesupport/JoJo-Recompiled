#include "core/ps1_commercial_evidence.h"
#include "core/ps1_timing.h"

#include <utility>

namespace jojo {
namespace {

Ps1VideoTimingMode timing_mode_from_display(
    const Ps1GpuDisplayState& display) noexcept {
    if (display.pal) {
        return display.interlaced
            ? Ps1VideoTimingMode::pal_interlaced
            : Ps1VideoTimingMode::pal_non_interlaced;
    }
    return display.interlaced
        ? Ps1VideoTimingMode::ntsc_interlaced
        : Ps1VideoTimingMode::ntsc_non_interlaced;
}

constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ull;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t byte) noexcept {
    hash ^= byte;
    hash *= kFnv1a64Prime;
}

} // namespace


std::string_view ps1_commercial_session_termination_name(
    Ps1CommercialSessionTermination termination) noexcept {
    switch (termination) {
        case Ps1CommercialSessionTermination::bounded_run:
            return "bounded_run";
        case Ps1CommercialSessionTermination::frontier_stop:
            return "frontier_stop";
        case Ps1CommercialSessionTermination::manual_stop:
            return "manual_stop";
        case Ps1CommercialSessionTermination::periodic_checkpoint:
            return "periodic_checkpoint";
    }
    return "bounded_run";
}

std::optional<Ps1CommercialFrameEvidence>
make_ps1_commercial_frame_evidence(const Ps1DisplayFrame& frame) noexcept {
    if (frame.width == 0u || frame.height == 0u) return std::nullopt;
    const auto expected =
        static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    if (frame.rgba8.size() != expected) return std::nullopt;

    Ps1CommercialFrameEvidence evidence{};
    evidence.width = frame.width;
    evidence.height = frame.height;
    evidence.frame_hash_fnv1a64 = kFnv1a64Offset;

    for (const auto pixel : frame.rgba8) {
        if ((pixel & 0x00FFFFFFu) != 0u) ++evidence.non_black_pixels;
        hash_byte(evidence.frame_hash_fnv1a64, static_cast<std::uint8_t>(pixel));
        hash_byte(evidence.frame_hash_fnv1a64, static_cast<std::uint8_t>(pixel >> 8u));
        hash_byte(evidence.frame_hash_fnv1a64, static_cast<std::uint8_t>(pixel >> 16u));
        hash_byte(evidence.frame_hash_fnv1a64, static_cast<std::uint8_t>(pixel >> 24u));
    }

    if (evidence.non_black_pixels == 0u) return std::nullopt;
    return evidence;
}



void Ps1CommercialFrameProgress::observe(
    const Ps1DisplayFrame& frame) noexcept {
    const auto evidence = make_ps1_commercial_frame_evidence(frame);
    if (!evidence) return;

    ++observed_non_black_frames_;
    if (last_frame_hash_ &&
        *last_frame_hash_ != evidence->frame_hash_fnv1a64) {
        ++frame_change_count_;
    }
    last_frame_hash_ = evidence->frame_hash_fnv1a64;
    if (!first_frame_) first_frame_ = *evidence;
}

void Ps1CommercialFrameProgress::reset() noexcept {
    observed_non_black_frames_ = 0u;
    frame_change_count_ = 0u;
    last_frame_hash_.reset();
    first_frame_.reset();
}

std::uint64_t
Ps1CommercialFrameProgress::observed_non_black_frames() const noexcept {
    return observed_non_black_frames_;
}

std::uint64_t Ps1CommercialFrameProgress::frame_change_count() const noexcept {
    return frame_change_count_;
}

const std::optional<Ps1CommercialFrameEvidence>&
Ps1CommercialFrameProgress::first_frame() const noexcept {
    return first_frame_;
}

Ps1GameplayValidationSummary summarize_ps1_gameplay_validation(
    const Ps1CommercialEvidenceReport& report) noexcept {
    Ps1GameplayValidationSummary summary{};
    summary.frame_observed =
        report.observed_non_black_frames != 0u ||
        (report.first_frame.has_value() &&
         report.first_frame->non_black_pixels != 0u);
    summary.dynamic_video_observed = report.frame_change_count != 0u;
    summary.controller_poll_observed =
        report.pad_poll_count[0] != 0u ||
        report.pad_poll_count[1] != 0u;
    summary.controller_input_observed =
        report.pad_pressed_poll_count[0] != 0u ||
        report.pad_pressed_poll_count[1] != 0u;
    summary.audio_non_silent_observed =
        report.spu_nonzero_samples != 0u;
    summary.memory_card_read_observed =
        report.memory_card_read_sector_count[0] != 0u ||
        report.memory_card_read_sector_count[1] != 0u;
    summary.memory_card_write_observed =
        report.memory_card_write_sector_count[0] != 0u ||
        report.memory_card_write_sector_count[1] != 0u;
    summary.memory_card_content_change_observed =
        report.memory_card_changed_write_sector_count[0] != 0u ||
        report.memory_card_changed_write_sector_count[1] != 0u;
    return summary;
}

Result<Ps1CommercialEvidenceRunner> Ps1CommercialEvidenceRunner::open(
    const std::filesystem::path& source,
    const Ps1DiscOpenOptions& open_options) {
    auto disc = Ps1DiscSession::open(source, open_options);
    if (!disc) {
        return Result<Ps1CommercialEvidenceRunner>::failure(disc.error, disc.detail);
    }

    auto runtime = Ps1BootRuntime::create(disc.value.boot_executable());
    if (!runtime) {
        return Result<Ps1CommercialEvidenceRunner>::failure(runtime.error, runtime.detail);
    }

    Ps1CommercialEvidenceRunner runner{};
    runner.disc_ = std::move(disc.value);
    runner.runtime_ = std::move(runtime.value);
    return Result<Ps1CommercialEvidenceRunner>::success(std::move(runner));
}

Ps1BootReport Ps1CommercialEvidenceRunner::run_segment(
    const Ps1BootOptions& options) noexcept {
    runtime_.bus().hardware_services().attach_disc(&disc_);
    return runtime_.run(options);
}

void Ps1CommercialEvidenceRunner::set_native_x64_enabled(
    bool enabled) noexcept {
    runtime_.set_native_x64_enabled(enabled);
}

bool Ps1CommercialEvidenceRunner::native_x64_enabled() const noexcept {
    return runtime_.native_x64_enabled();
}

void Ps1CommercialEvidenceRunner::signal_vblank() noexcept {
    runtime_.signal_vblank();
}

Ps1CommercialEvidenceReport Ps1CommercialEvidenceRunner::run(
    const Ps1CommercialEvidenceOptions& options) noexcept {
    runtime_.bus().hardware_services().attach_disc(&disc_);

    Ps1CommercialEvidenceReport report{};
    report.source = disc_.binding();

    const auto finalize_report = [&]() {
        const auto counters = validation_counters();
        report.pad_poll_count = counters.pad_poll_count;
        report.pad_pressed_poll_count = counters.pad_pressed_poll_count;
        report.memory_card_read_sector_count =
            counters.memory_card_read_sector_count;
        report.memory_card_write_sector_count =
            counters.memory_card_write_sector_count;
        report.memory_card_changed_write_sector_count =
            counters.memory_card_changed_write_sector_count;
        report.session_dma_transfer_count = counters.dma_transfer_count;
        report.session_cdrom_command_count = counters.cdrom_command_count;
        report.session_gpu_gp0_word_count = counters.gpu_gp0_word_count;
        report.session_gpu_gp1_command_count = counters.gpu_gp1_command_count;
        report.session_vram_write_count = counters.vram_write_count;
        report.session_vblank_count = counters.vblank_count;
        report.spu_sample_frames = counters.spu_sample_frames;
        report.spu_nonzero_samples = counters.spu_nonzero_samples;

        const auto& gpu =
            runtime_.bus().hardware_services().gpu();
        report.gpu_display = gpu.display_state();
        report.gpu_nonzero_vram_words = 0u;
        report.gpu_display_region_nonzero_words = 0u;
        report.gpu_nonzero_bounds_valid = false;

        for (std::uint32_t y = 0u;
             y < Ps1GpuIngress::vram_height;
             ++y) {
            for (std::uint32_t x = 0u;
                 x < Ps1GpuIngress::vram_width;
                 ++x) {
                if (gpu.vram_pixel(x, y) == 0u) continue;
                ++report.gpu_nonzero_vram_words;
                if (!report.gpu_nonzero_bounds_valid) {
                    report.gpu_nonzero_bounds_valid = true;
                    report.gpu_nonzero_min_x = x;
                    report.gpu_nonzero_max_x = x;
                    report.gpu_nonzero_min_y = y;
                    report.gpu_nonzero_max_y = y;
                } else {
                    report.gpu_nonzero_min_x =
                        std::min(report.gpu_nonzero_min_x, x);
                    report.gpu_nonzero_max_x =
                        std::max(report.gpu_nonzero_max_x, x);
                    report.gpu_nonzero_min_y =
                        std::min(report.gpu_nonzero_min_y, y);
                    report.gpu_nonzero_max_y =
                        std::max(report.gpu_nonzero_max_y, y);
                }
            }
        }

        if (report.gpu_display.width != 0u &&
            report.gpu_display.height != 0u) {
            for (std::uint32_t y = 0u;
                 y < report.gpu_display.height;
                 ++y) {
                const auto source_y =
                    (report.gpu_display.start_y + y) &
                    (Ps1GpuIngress::vram_height - 1u);
                for (std::uint32_t x = 0u;
                     x < report.gpu_display.width;
                     ++x) {
                    const auto source_x =
                        (report.gpu_display.start_x + x) &
                        (Ps1GpuIngress::vram_width - 1u);
                    if (gpu.vram_pixel(source_x, source_y) != 0u) {
                        ++report.gpu_display_region_nonzero_words;
                    }
                }
            }
        }

        report.boot.recent_cdrom_commands = recent_cdrom_commands();
        return report;
    };

    std::size_t fallback_index = 0u;
    std::uint32_t execution_segments = 0u;
    Ps1VideoReferenceClock video_clock{
        timing_mode_from_display(gpu_display_state())};
    std::uint64_t frame_ticks_remaining =
        video_clock.next_frame_ticks();
    const auto max_execution_segments =
        options.max_execution_segments == 0u ? 1u : options.max_execution_segments;
    while (true) {
        Ps1BootOptions segment_options = options.boot;
        if (frame_ticks_remaining != 0u) {
            segment_options.instruction_budget =
                std::min<std::uint64_t>(
                    segment_options.instruction_budget,
                    frame_ticks_remaining);
        }

        auto segment = run_segment(segment_options);
        ++execution_segments;
        report.execution_segments = execution_segments;
        report.total_execution_steps += segment.execution_steps;
        report.total_instructions_retired += segment.instructions_retired;
        report.total_native_x64_instructions_retired +=
            segment.native_x64_instructions_retired;
        report.total_reference_instructions_retired +=
            segment.reference_instructions_retired;
        report.total_native_x64_cache_compilations +=
            segment.native_x64_cache_compilations;
        report.total_native_x64_cache_reuses +=
            segment.native_x64_cache_reuses;
        report.total_native_x64_cache_invalidations +=
            segment.native_x64_cache_invalidations;
        report.total_native_x64_cache_evictions +=
            segment.native_x64_cache_evictions;
        report.boot = std::move(segment);

        const auto segment_frontier =
            classify_ps1_commercial_frontier(report.boot);
        if (segment_frontier ==
                Ps1CommercialFrontierClass::execution_budget &&
            report.boot.execution_steps != 0u &&
            report.boot.execution_steps <= frame_ticks_remaining) {
            frame_ticks_remaining -= report.boot.execution_steps;
            if (frame_ticks_remaining == 0u) {
                runtime_.signal_vblank();
                ++report.completed_frames;
                video_clock.set_mode(
                    timing_mode_from_display(
                        gpu_display_state()));
                frame_ticks_remaining =
                    video_clock.next_frame_ticks();
            }
        }

        const auto frame = runtime_.display_frame();
        report.first_frame = make_ps1_commercial_frame_evidence(frame);
        if (report.first_frame) {
            report.observed_non_black_frames = 1u;
            report.boot.presented_frames = 1u;
            report.boot.stop_reason = Ps1BootStopReason::commercial_frame_presented;
            report.frontier = Ps1CommercialFrontierClass::commercial_frame_presented;
            return finalize_report();
        }

        report.frontier = classify_ps1_commercial_frontier(report.boot);

        if (report.frontier == Ps1CommercialFrontierClass::execution_budget &&
            execution_segments < max_execution_segments) {
            continue;
        }

        if (report.frontier != Ps1CommercialFrontierClass::bios_call) {
            return finalize_report();
        }
        if (fallback_index >= options.diagnostic_bios_fallbacks.size()) {
            return finalize_report();
        }
        if (report.boot.recent_bios_calls.empty()) {
            return finalize_report();
        }

        const auto bios = report.boot.recent_bios_calls.back();
        const auto fallback = options.diagnostic_bios_fallbacks[fallback_index++];
        if (!runtime_.apply_diagnostic_bios_fallback(fallback)) {
            return finalize_report();
        }

        report.diagnostic_decisions.push_back(Ps1CommercialDiagnosticDecision{
            bios.table_physical,
            bios.selector,
            fallback,
        });
    }
}

const Ps1DiscSession& Ps1CommercialEvidenceRunner::disc_session() const noexcept {
    return disc_;
}

Ps1DisplayFrame Ps1CommercialEvidenceRunner::display_frame() const {
    return runtime_.display_frame();
}

Ps1GpuDisplayState Ps1CommercialEvidenceRunner::gpu_display_state() const noexcept {
    return runtime_.bus().hardware_services().gpu().display_state();
}

Ps1CommercialRuntimeCounters
Ps1CommercialEvidenceRunner::validation_counters() const noexcept {
    Ps1CommercialRuntimeCounters counters{};
    const auto& hardware = runtime_.bus().hardware_services();
    const auto& sio0 = hardware.sio0();
    for (std::uint32_t port = 0u; port < 2u; ++port) {
        counters.pad_poll_count[port] = sio0.digital_pad_poll_count(port);
        counters.pad_pressed_poll_count[port] =
            sio0.digital_pad_pressed_poll_count(port);
        counters.memory_card_read_sector_count[port] =
            sio0.memory_card_read_sector_count(port);
        counters.memory_card_write_sector_count[port] =
            sio0.memory_card_write_sector_count(port);
        counters.memory_card_changed_write_sector_count[port] =
            sio0.memory_card_changed_write_sector_count(port);
    }
    counters.dma_transfer_count =
        hardware.completed_dma_transfer_count();
    counters.cdrom_command_count = hardware.cdrom().command_count();
    counters.gpu_gp0_word_count = hardware.gpu_gp0_word_count();
    counters.gpu_gp1_command_count = hardware.gpu_gp1_command_count();
    counters.vram_write_count = hardware.gpu_vram_write_count();
    counters.vblank_count = hardware.vblank_count();
    counters.spu_sample_frames = hardware.spu().generated_sample_frames();
    counters.spu_nonzero_samples = hardware.spu().nonzero_sample_count();
    return counters;
}


std::vector<Ps1CdromCommandSummary>
Ps1CommercialEvidenceRunner::recent_cdrom_commands() const {
    std::vector<Ps1CdromCommandSummary> out;
    const auto& history =
        runtime_.bus().hardware_services().cdrom().recent_commands();
    out.reserve(history.size());
    for (const auto& event : history) {
        out.push_back(Ps1CdromCommandSummary{
            event.command,
            event.index,
            event.status,
        });
    }
    return out;
}

std::vector<std::int16_t> Ps1CommercialEvidenceRunner::drain_audio_samples() {
    return runtime_.bus().hardware_services().spu().drain_audio_samples();
}

Ps1BootRuntimeState Ps1CommercialEvidenceRunner::save_runtime_state() const {
    return runtime_.save_state();
}

Result<void> Ps1CommercialEvidenceRunner::load_runtime_state(
    const Ps1BootRuntimeState& state) {
    auto restored = runtime_.load_state(state);
    if (!restored) return restored;
    runtime_.bus().hardware_services().attach_disc(&disc_);
    return Result<void>::success();
}

std::uint64_t
Ps1CommercialEvidenceRunner::diagnostic_state_hash() const noexcept {
    return runtime_.diagnostic_state_hash();
}

void Ps1CommercialEvidenceRunner::set_pad_buttons(
    std::uint32_t port,
    std::uint16_t active_low_buttons) noexcept {
    runtime_.bus().hardware_services().sio0().set_digital_pad_buttons(
        port,
        active_low_buttons);
}

Result<void> Ps1CommercialEvidenceRunner::load_or_create_memory_card(
    std::uint32_t port,
    const std::filesystem::path& path) {
    if (port >= 2u) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "PS1 memory-card port must be 0 or 1");
    }
    auto loaded = Ps1MemoryCard::load_or_create(path);
    if (!loaded) {
        return Result<void>::failure(loaded.error, loaded.detail);
    }
    runtime_.bus().hardware_services().sio0().memory_card(port) =
        std::move(loaded.value);
    return Result<void>::success();
}

Result<void> Ps1CommercialEvidenceRunner::flush_memory_cards() {
    auto& sio0 = runtime_.bus().hardware_services().sio0();
    for (std::uint32_t port = 0u; port < 2u; ++port) {
        auto& card = sio0.memory_card(port);
        if (!card.has_backing_path()) continue;
        auto flushed = card.flush();
        if (!flushed) return flushed;
    }
    return Result<void>::success();
}

} // namespace jojo
