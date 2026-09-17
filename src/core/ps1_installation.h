#pragma once

#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace jojo {

struct PendingInstallGeneration {
    std::string generation_id;
    std::filesystem::path staging_dir;
    std::filesystem::path final_dir;
};

struct ActiveInstallGeneration {
    std::filesystem::path install_root;
    std::string generation_id;
    std::filesystem::path generation_dir;
    std::filesystem::path manifest_path;
};

[[nodiscard]] Result<void> validate_install_destination(
    const std::filesystem::path& install_root,
    std::uint64_t required_bytes);

[[nodiscard]] Result<PendingInstallGeneration> begin_install_generation(
    const std::filesystem::path& install_root);

[[nodiscard]] Result<void> commit_install_generation(
    const std::filesystem::path& install_root,
    const PendingInstallGeneration& generation);

[[nodiscard]] Result<ActiveInstallGeneration> resolve_active_install_generation(
    const std::filesystem::path& install_root);

}
