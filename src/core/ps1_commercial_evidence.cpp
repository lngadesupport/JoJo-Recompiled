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
        report.memory_card_read_sector_count =
            counters.memory_card_read_sector_count;
        report.memory_card_write_sector_count =
            counters.memory_card_write_sector_count;
        report.spu_sample_frames = counters.spu_sample_frames;
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
        counters.memory_card_read_sector_count[port] =
            sio0.memory_card_read_sector_count(port);
        counters.memory_card_write_sector_count[port] =
            sio0.memory_card_write_sector_count(port);
    }
    counters.spu_sample_frames = hardware.spu().generated_sample_frames();
    return counters;
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
