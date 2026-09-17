#pragma once

#include "core/conversion.h"
#include "core/ps1_boot_report.h"
#include "core/ps1_disc_session.h"
#include "core/ps1_max3_explorer.h"
#include "core/result.h"

#include <filesystem>

namespace jojo {

enum class InstallationKind {
    absent,
    legacy_v1,
    ps1_m1
};

struct InstallationInfo {
    std::filesystem::path install_root;
    std::filesystem::path generation_dir;
    ConversionManifest manifest;
};

[[nodiscard]] Result<InstallationKind> classify_installation(
    const std::filesystem::path& install_root);
[[nodiscard]] Result<InstallationInfo> validate_installation(
    const std::filesystem::path& install_root);
[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_checkpoint(
    const std::filesystem::path& install_root,
    const Ps1BootOptions& options = {});
[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_checkpoint_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path,
    const Ps1BootOptions& options = {});
[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_checkpoint_from_disc(
    const std::filesystem::path& source,
    const Ps1DiscOpenOptions& open_options,
    const Ps1BootOptions& options = {});
[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_checkpoint_from_disc_to_file(
    const std::filesystem::path& source,
    const Ps1DiscOpenOptions& open_options,
    const std::filesystem::path& report_path,
    const Ps1BootOptions& options = {});
[[nodiscard]] Result<Ps1Max3Report> bootstrap_runtime_max3_local_evidence_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path,
    Ps1Max3Options options = ps1_max3_local_evidence_options());
[[nodiscard]] Result<Ps1BootReport> bootstrap_runtime_local_evidence_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path);
[[nodiscard]] Result<void> bootstrap_runtime(
    const std::filesystem::path& install_root);

} // namespace jojo
