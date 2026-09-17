#pragma once

#include "core/ps1_boot_report.h"
#include "core/ps1_disc_session.h"
#include "core/result.h"

#include <filesystem>

namespace jojo {

[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_checkpoint_from_disc(
    const std::filesystem::path& source,
    const Ps1DiscOpenOptions& open_options,
    const Ps1BootOptions& options = {});

[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_checkpoint_from_disc_to_file(
    const std::filesystem::path& source,
    const Ps1DiscOpenOptions& open_options,
    const std::filesystem::path& report_path,
    const Ps1BootOptions& options = {});

} // namespace jojo
