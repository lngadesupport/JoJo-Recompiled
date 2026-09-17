#pragma once

#include "core/ps1_commercial_evidence.h"
#include "core/result.h"

#include <filesystem>
#include <string>

namespace jojo {

[[nodiscard]] std::string format_ps1_commercial_evidence_report(
    const Ps1CommercialEvidenceReport& report);

[[nodiscard]] Result<void> save_ps1_commercial_evidence_report_atomic(
    const std::filesystem::path& path,
    const Ps1CommercialEvidenceReport& report);

} // namespace jojo
