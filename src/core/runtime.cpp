#include "core/runtime.h"

#include "core/ps1_boot_report_io.h"
#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "core/ps1_installation.h"
#include "core/ps1_max3_explorer.h"

#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace jojo {
namespace {

Result<std::vector<std::uint8_t>> read_local_file(
    const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::file_not_found,
            "installed PS1 file is missing: " + path.string());
    }

    std::vector<std::uint8_t> bytes;
    for (std::istreambuf_iterator<char> it(in), end; it != end; ++it) {
        bytes.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(*it)));
    }
    if (in.bad()) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error,
            "failed while reading installed PS1 file: " + path.string());
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<ConversionManifest> load_active_ps1_manifest(
    const std::filesystem::path& install_root,
    std::filesystem::path* generation_dir = nullptr) {
    auto active = resolve_active_install_generation(install_root);
    if (!active) {
        return Result<ConversionManifest>::failure(active.error, active.detail);
    }

    auto manifest = load_conversion_manifest(active.value.manifest_path);
    if (!manifest) {
        return Result<ConversionManifest>::failure(manifest.error, manifest.detail);
    }
    if (manifest.value.manifest_version != "2") {
        return Result<ConversionManifest>::failure(
            ErrorCode::invalid_installation,
            "active installation generation is not a PS1 manifest v2 installation");
    }
    if (generation_dir) *generation_dir = active.value.generation_dir;
    return manifest;
}

bool executable_metadata_matches(const ConversionManifest& manifest,
                                 const Ps1ExeMetadata& metadata) noexcept {
    return manifest.psx_exe_hash_fnv1a64 == metadata.fnv1a64_hex &&
           manifest.psx_exe_entry.has_value() &&
           *manifest.psx_exe_entry == metadata.entry_pc &&
           manifest.psx_exe_load_address.has_value() &&
           *manifest.psx_exe_load_address == metadata.text_load_address &&
           manifest.psx_exe_initial_gp.has_value() &&
           *manifest.psx_exe_initial_gp == metadata.initial_gp &&
           manifest.psx_exe_text_size.has_value() &&
           *manifest.psx_exe_text_size == metadata.text_size &&
           manifest.psx_exe_stack_base.has_value() &&
           *manifest.psx_exe_stack_base == metadata.stack_base &&
           manifest.psx_exe_stack_size.has_value() &&
           *manifest.psx_exe_stack_size == metadata.stack_size;
}

Result<Ps1Executable> load_validated_installed_executable(
    const InstallationInfo& install) {
    const auto executable_path = install.generation_dir / "data" / "boot.psxexe";
    auto bytes = read_local_file(executable_path);
    if (!bytes) {
        return Result<Ps1Executable>::failure(bytes.error, bytes.detail);
    }
    auto executable = parse_ps1_executable(bytes.value);
    if (!executable) {
        return Result<Ps1Executable>::failure(
            ErrorCode::invalid_installation,
            "installed PS-X EXE is invalid: " + executable.detail);
    }
    if (!executable_metadata_matches(install.manifest, executable.value.metadata)) {
        return Result<Ps1Executable>::failure(
            ErrorCode::invalid_installation,
            "installed PS-X EXE does not match the verified manifest metadata");
    }
    return executable;
}

} // namespace

Result<InstallationKind> classify_installation(
    const std::filesystem::path& install_root) {
    if (install_root.empty()) {
        return Result<InstallationKind>::failure(
            ErrorCode::invalid_argument,
            "installation root cannot be empty");
    }

    std::error_code ec;
    const bool root_exists = std::filesystem::exists(install_root, ec);
    if (ec) {
        return Result<InstallationKind>::failure(
            ErrorCode::io_error,
            "failed to inspect installation root: " + ec.message());
    }
    if (root_exists && !std::filesystem::is_directory(install_root, ec)) {
        if (ec) {
            return Result<InstallationKind>::failure(
                ErrorCode::io_error,
                "failed to inspect installation root: " + ec.message());
        }
        return Result<InstallationKind>::failure(
            ErrorCode::invalid_installation,
            "installation root is not a directory");
    }

    const auto active_pointer = install_root / "active_install.ini";
    const bool has_active_pointer = std::filesystem::exists(active_pointer, ec);
    if (ec) {
        return Result<InstallationKind>::failure(
            ErrorCode::io_error,
            "failed to inspect active installation pointer: " + ec.message());
    }
    if (has_active_pointer) {
        auto manifest = load_active_ps1_manifest(install_root);
        if (!manifest) {
            return Result<InstallationKind>::failure(
                manifest.error, manifest.detail);
        }
        return Result<InstallationKind>::success(InstallationKind::ps1_m1);
    }

    const auto legacy_manifest_path = install_root / "game_manifest.ini";
    const bool has_root_manifest = std::filesystem::exists(legacy_manifest_path, ec);
    if (ec) {
        return Result<InstallationKind>::failure(
            ErrorCode::io_error,
            "failed to inspect root installation manifest: " + ec.message());
    }
    if (!has_root_manifest) {
        return Result<InstallationKind>::success(InstallationKind::absent);
    }

    auto legacy = load_conversion_manifest(legacy_manifest_path);
    if (!legacy) {
        return Result<InstallationKind>::failure(legacy.error, legacy.detail);
    }
    if (legacy.value.manifest_version == "1") {
        return Result<InstallationKind>::success(InstallationKind::legacy_v1);
    }
    return Result<InstallationKind>::failure(
        ErrorCode::invalid_installation,
        "root-level manifest is not a supported legacy v1 installation");
}

Result<InstallationInfo> validate_installation(
    const std::filesystem::path& install_root) {
    auto kind = classify_installation(install_root);
    if (!kind) {
        return Result<InstallationInfo>::failure(kind.error, kind.detail);
    }
    if (kind.value == InstallationKind::absent) {
        return Result<InstallationInfo>::failure(
            ErrorCode::invalid_installation,
            "PS1 installation is absent");
    }
    if (kind.value == InstallationKind::legacy_v1) {
        return Result<InstallationInfo>::failure(
            ErrorCode::invalid_installation,
            "legacy Dreamcast/SH-4 installation detected; reconvert the owned PlayStation image to create a PS1 M1 generation");
    }

    std::filesystem::path generation_dir;
    auto manifest = load_active_ps1_manifest(install_root, &generation_dir);
    if (!manifest) {
        return Result<InstallationInfo>::failure(manifest.error, manifest.detail);
    }

    if (manifest.value.platform != "playstation") {
        return Result<InstallationInfo>::failure(
            ErrorCode::invalid_installation,
            "active manifest does not target PlayStation");
    }
    if (manifest.value.media_status != "verified") {
        return Result<InstallationInfo>::failure(
            ErrorCode::invalid_installation,
            "PS1 media status is not verified");
    }
    if (manifest.value.executable_status != "verified") {
        return Result<InstallationInfo>::failure(
            ErrorCode::invalid_installation,
            "PS-X EXE status is not verified");
    }

    std::error_code ec;
    const auto system_cnf = generation_dir / "data" / "SYSTEM.CNF";
    if (!std::filesystem::is_regular_file(system_cnf, ec) || ec) {
        return Result<InstallationInfo>::failure(
            ErrorCode::invalid_installation,
            "PS1 installation is missing local data/SYSTEM.CNF");
    }

    InstallationInfo info{
        install_root,
        generation_dir,
        std::move(manifest.value),
    };
    auto executable = load_validated_installed_executable(info);
    if (!executable) {
        return Result<InstallationInfo>::failure(executable.error, executable.detail);
    }

    return Result<InstallationInfo>::success(std::move(info));
}

Result<Ps1BootReport> bootstrap_runtime_checkpoint(
    const std::filesystem::path& install_root,
    const Ps1BootOptions& options) {
    auto install = validate_installation(install_root);
    if (!install) {
        return Result<Ps1BootReport>::failure(install.error, install.detail);
    }

    auto executable = load_validated_installed_executable(install.value);
    if (!executable) {
        return Result<Ps1BootReport>::failure(executable.error, executable.detail);
    }

    auto runtime = Ps1BootRuntime::create(executable.value);
    if (!runtime) {
        return Result<Ps1BootReport>::failure(runtime.error, runtime.detail);
    }
    return Result<Ps1BootReport>::success(runtime.value.run(options));
}

Result<Ps1BootReport> bootstrap_runtime_checkpoint_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path,
    const Ps1BootOptions& options) {
    auto report = bootstrap_runtime_checkpoint(install_root, options);
    if (!report) return report;

    auto saved = save_ps1_boot_report_atomic(report_path, report.value);
    if (!saved) {
        return Result<Ps1BootReport>::failure(saved.error, saved.detail);
    }
    return report;
}

Result<Ps1Max3Report> bootstrap_runtime_max3_local_evidence_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path,
    Ps1Max3Options options) {
    auto install = validate_installation(install_root);
    if (!install) {
        return Result<Ps1Max3Report>::failure(install.error, install.detail);
    }

    auto executable = load_validated_installed_executable(install.value);
    if (!executable) {
        return Result<Ps1Max3Report>::failure(executable.error, executable.detail);
    }

    auto report = explore_ps1_max3(executable.value, options);
    if (!report) {
        return Result<Ps1Max3Report>::failure(report.error, report.detail);
    }

    auto saved = save_ps1_max3_report_atomic(report_path, report.value);
    if (!saved) {
        return Result<Ps1Max3Report>::failure(saved.error, saved.detail);
    }
    return report;
}

Result<Ps1BootReport> bootstrap_runtime_local_evidence_to_file(
    const std::filesystem::path& install_root,
    const std::filesystem::path& report_path) {
    auto max3 = bootstrap_runtime_max3_local_evidence_to_file(
        install_root, report_path, ps1_max3_local_evidence_options());
    if (!max3) {
        return Result<Ps1BootReport>::failure(max3.error, max3.detail);
    }
    return Result<Ps1BootReport>::success(std::move(max3.value.best_report));
}

Result<void> bootstrap_runtime(const std::filesystem::path& install_root) {
    Ps1BootOptions options{};
    options.instruction_budget = 10000u;
    auto checkpoint = bootstrap_runtime_checkpoint(install_root, options);
    if (!checkpoint) {
        return Result<void>::failure(checkpoint.error, checkpoint.detail);
    }

    if (checkpoint.value.stop_reason == Ps1BootStopReason::commercial_frame_presented) {
        return Result<void>::success();
    }

    return Result<void>::failure(
        ErrorCode::backend_unavailable,
        "JoJo PS1 reference checkpoint stopped at " +
            ps1_boot_stop_reason_name(checkpoint.value.stop_reason) +
            "; commercial JoJo boot is not verified");
}

} // namespace jojo
