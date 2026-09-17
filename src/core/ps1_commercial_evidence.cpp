#include "core/ps1_commercial_evidence.h"

#include <utility>

namespace jojo {

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

} // namespace jojo
