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
    Ps1CommercialEvidenceReport report{};
    report.source = disc_.binding();
    report.boot = runtime_.run(options.boot);
    report.total_instructions_retired = report.boot.instructions_retired;
    report.frontier = classify_ps1_commercial_frontier(report.boot);
    return report;
}

const Ps1DiscSession& Ps1CommercialEvidenceRunner::disc_session() const noexcept {
    return disc_;
}

} // namespace jojo
