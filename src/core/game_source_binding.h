#pragma once

#include "core/result.h"
#include "core/revision.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace jojo {

struct GameSourceBinding {
    std::filesystem::path source_path;
    std::string source_format;
    std::uint64_t source_size{};
    std::string source_hash_fnv1a64;
    std::string revision_id;
};

struct Ps1DiscOpenOptions;
class Ps1DiscSession;

[[nodiscard]] Result<std::optional<std::filesystem::path>> discover_single_ps1_source(
    const std::filesystem::path& rom_dir);

[[nodiscard]] Result<void> save_game_source_binding_atomic(
    const std::filesystem::path& path,
    const GameSourceBinding& binding);

[[nodiscard]] Result<GameSourceBinding> load_game_source_binding(
    const std::filesystem::path& path);

[[nodiscard]] Result<Ps1DiscSession> reopen_bound_source(
    const GameSourceBinding& binding,
    const Ps1DiscOpenOptions& options);

} // namespace jojo
