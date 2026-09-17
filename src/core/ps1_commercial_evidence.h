#pragma once

#include "core/game_source_binding.h"
#include "core/ps1_boot_runtime.h"
#include "core/ps1_commercial_frontier.h"
#include "core/ps1_disc_session.h"
#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace jojo {

struct Ps1CommercialDiagnosticDecision {
    std::uint32_t bios_table{};
    std::uint32_t bios_selector{};
    Ps1BiosFallback fallback{Ps1BiosFallback::return_zero};
};

struct Ps1CommercialEvidenceOptions {
    Ps1BootOptions boot{};
    std::vector<Ps1BiosFallback> diagnostic_bios_fallbacks;
};

struct Ps1CommercialEvidenceReport {
    GameSourceBinding source{};
    Ps1CommercialFrontierClass frontier{Ps1CommercialFrontierClass::none};
    Ps1BootReport boot{};
    std::uint64_t total_instructions_retired{};
    std::vector<Ps1CommercialDiagnosticDecision> diagnostic_decisions;
};

class Ps1CommercialEvidenceRunner {
public:
    Ps1CommercialEvidenceRunner() = default;

    [[nodiscard]] static Result<Ps1CommercialEvidenceRunner> open(
        const std::filesystem::path& source,
        const Ps1DiscOpenOptions& open_options = {});

    [[nodiscard]] Ps1CommercialEvidenceReport run(
        const Ps1CommercialEvidenceOptions& options) noexcept;

    [[nodiscard]] const Ps1DiscSession& disc_session() const noexcept;

private:
    Ps1DiscSession disc_{};
    Ps1BootRuntime runtime_{};
};

} // namespace jojo
