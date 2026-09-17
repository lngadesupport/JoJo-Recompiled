#pragma once

#include "core/result.h"
#include "core/semver.h"

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

inline constexpr SemanticVersion kModApiVersion{1, 0, 0};

enum class ModKind {
    data,
    native,
};

struct ModDependency {
    std::string id;
    VersionRequirement requirement;

    friend bool operator==(const ModDependency&, const ModDependency&) = default;
};

struct ModManifest {
    std::string id;
    std::string name;
    SemanticVersion version{};
    SemanticVersion api_version{};
    ModKind kind{ModKind::data};
    bool gameplay{};
    std::filesystem::path entry;
    std::vector<ModDependency> dependencies;
    std::vector<std::string> conflicts;
};

struct DiscoveredMod {
    ModManifest manifest;
    std::filesystem::path root;
};

using ModCatalog = std::vector<DiscoveredMod>;

struct ResolvedModSet {
    std::vector<const DiscoveredMod*> load_order;
    std::vector<std::string> diagnostics;
};

struct OverlayEntry {
    std::string mod_id;
    std::filesystem::path host_path;
};

struct OverlayCollision {
    std::string logical_path;
    std::string previous_mod_id;
    std::string replacing_mod_id;
};

struct ModOverlay {
    std::map<std::string, OverlayEntry> files;
    std::vector<OverlayCollision> collisions;
};

struct ModSetHashes {
    std::string mod_set_hash;
    std::string gameplay_hash;
};

enum class ModSessionMode {
    offline,
    ranked,
    custom,
};

struct ModSessionPolicy {
    ModSessionMode mode{ModSessionMode::offline};
    std::string required_mod_set_hash;
};

[[nodiscard]] Result<ModManifest> parse_mod_manifest(
    std::string_view text,
    const std::filesystem::path& root);
[[nodiscard]] Result<ModCatalog> discover_mods(const std::filesystem::path& mods_root);
[[nodiscard]] Result<ResolvedModSet> resolve_mod_set(
    const ModCatalog& catalog,
    std::span<const std::string> requested_ids);
[[nodiscard]] Result<ModOverlay> build_mod_overlay(const ResolvedModSet& mods);
[[nodiscard]] Result<std::string> compute_mod_content_hash(const DiscoveredMod& mod);
[[nodiscard]] Result<ModSetHashes> compute_mod_set_hashes(const ResolvedModSet& mods);
[[nodiscard]] Result<void> validate_mod_session(
    const ResolvedModSet& mods,
    const ModSetHashes& hashes,
    const ModSessionPolicy& policy);

}
