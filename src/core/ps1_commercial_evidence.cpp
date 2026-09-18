#include "core/ps1_commercial_evidence.h"

#include <utility>

namespace jojo {
namespace {

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
        report.boot.recent_cdrom_commands = recent_cdrom_commands();
        return report;
    };

    std::size_t fallback_index = 0u;
    std::uint32_t execution_segments = 0u;
    const auto max_execution_segments =
        options.max_execution_segments == 0u ? 1u : options.max_execution_segments;
    while (true) {
        auto segment = run_segment(options.boot);
        ++execution_segments;
        report.execution_segments = execution_segments;
        report.total_instructions_retired += segment.instructions_retired;
        report.boot = std::move(segment);

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
