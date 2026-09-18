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

Ps1CommercialEvidenceReport Ps1CommercialEvidenceRunner::run(
    const Ps1CommercialEvidenceOptions& options) noexcept {
    runtime_.bus().hardware_services().attach_disc(&disc_);

    Ps1CommercialEvidenceReport report{};
    report.source = disc_.binding();

    std::size_t fallback_index = 0u;
    while (true) {
        auto segment = runtime_.run(options.boot);
        report.total_instructions_retired += segment.instructions_retired;
        report.boot = std::move(segment);

        const auto frame = runtime_.display_frame();
        report.first_frame = make_ps1_commercial_frame_evidence(frame);
        if (report.first_frame) {
            report.boot.presented_frames = 1u;
            report.boot.stop_reason = Ps1BootStopReason::commercial_frame_presented;
            report.frontier = Ps1CommercialFrontierClass::commercial_frame_presented;
            return report;
        }

        report.frontier = classify_ps1_commercial_frontier(report.boot);

        if (report.frontier != Ps1CommercialFrontierClass::bios_call) {
            return report;
        }
        if (fallback_index >= options.diagnostic_bios_fallbacks.size()) {
            return report;
        }
        if (report.boot.recent_bios_calls.empty()) {
            return report;
        }

        const auto bios = report.boot.recent_bios_calls.back();
        const auto fallback = options.diagnostic_bios_fallbacks[fallback_index++];
        if (!runtime_.apply_diagnostic_bios_fallback(fallback)) {
            return report;
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

} // namespace jojo
