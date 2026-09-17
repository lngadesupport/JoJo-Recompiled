#include "core/mod_runtime.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>

namespace jojo {
namespace {

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) --last;
    return std::string(value.substr(first, last - first));
}

bool valid_mod_id(std::string_view id) noexcept {
    if (id.empty()) return false;
    const auto allowed = [](char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    };
    if (!((id.front() >= 'a' && id.front() <= 'z') || (id.front() >= '0' && id.front() <= '9'))) return false;
    return std::all_of(id.begin(), id.end(), allowed);
}

bool safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    const auto normalized = path.lexically_normal();
    if (normalized.empty() || normalized == ".") return false;
    for (const auto& part : normalized) {
        if (part == "..") return false;
    }
    return true;
}

Result<bool> parse_bool(std::string_view value) {
    if (value == "1" || value == "true") return Result<bool>::success(true);
    if (value == "0" || value == "false") return Result<bool>::success(false);
    return Result<bool>::failure(ErrorCode::invalid_argument, "invalid mod manifest boolean");
}

Result<ModKind> parse_kind(std::string_view value) {
    if (value == "data") return Result<ModKind>::success(ModKind::data);
    if (value == "native") return Result<ModKind>::success(ModKind::native);
    return Result<ModKind>::failure(ErrorCode::invalid_argument, "invalid mod kind: " + std::string(value));
}

std::vector<std::string> split_csv(std::string_view text) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto comma = text.find(',', start);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        auto item = trim(text.substr(start, end - start));
        if (!item.empty()) result.push_back(std::move(item));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return result;
}

Result<ModDependency> parse_dependency(std::string_view text) {
    const auto at = text.find('@');
    const std::string id = trim(text.substr(0, at));
    if (!valid_mod_id(id)) {
        return Result<ModDependency>::failure(ErrorCode::invalid_argument, "invalid dependency mod id: " + id);
    }
    std::string requirement_text;
    if (at != std::string_view::npos) {
        requirement_text = trim(text.substr(at + 1));
        if (requirement_text.empty()) {
            return Result<ModDependency>::failure(ErrorCode::invalid_argument, "dependency version requirement cannot be empty");
        }
    }
    const auto requirement = parse_version_requirement(requirement_text);
    if (!requirement) return Result<ModDependency>::failure(requirement.error, requirement.detail);
    return Result<ModDependency>::success({id, requirement.value});
}

bool api_compatible(const SemanticVersion& requested) noexcept {
    return requested.major == kModApiVersion.major && compare_semver(requested, kModApiVersion) <= 0;
}

Result<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Result<std::string>::failure(ErrorCode::file_not_found, "failed to open mod manifest: " + path.string());
    std::ostringstream out;
    out << in.rdbuf();
    if (!in.good() && !in.eof()) {
        return Result<std::string>::failure(ErrorCode::io_error, "failed while reading mod manifest: " + path.string());
    }
    return Result<std::string>::success(out.str());
}

}

Result<ModManifest> parse_mod_manifest(std::string_view text, const std::filesystem::path& root) {
    std::map<std::string, std::string> fields;
    std::size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const auto newline = text.find('\n');
        std::string line = trim(text.substr(0, newline));
        if (newline == std::string_view::npos) text = {};
        else text.remove_prefix(newline + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return Result<ModManifest>::failure(ErrorCode::invalid_argument,
                "invalid mod manifest line " + std::to_string(line_number) + " in " + root.string());
        }
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string value = trim(std::string_view(line).substr(eq + 1));
        static const std::set<std::string> allowed{
            "id", "name", "version", "api_version", "kind", "gameplay", "entry", "depends", "conflicts"
        };
        if (!allowed.contains(key)) {
            return Result<ModManifest>::failure(ErrorCode::invalid_argument, "unknown mod manifest key: " + key);
        }
        if (!fields.emplace(key, value).second) {
            return Result<ModManifest>::failure(ErrorCode::invalid_argument, "duplicate mod manifest key: " + key);
        }
    }

    const auto required = [&](const char* key) -> Result<std::string> {
        const auto it = fields.find(key);
        if (it == fields.end() || it->second.empty()) {
            return Result<std::string>::failure(ErrorCode::invalid_argument, "missing required mod manifest key: " + std::string(key));
        }
        return Result<std::string>::success(it->second);
    };

    const auto id = required("id");
    const auto name = required("name");
    const auto version_text = required("version");
    const auto api_text = required("api_version");
    const auto kind_text = required("kind");
    const auto gameplay_text = required("gameplay");
    if (!id) return Result<ModManifest>::failure(id.error, id.detail);
    if (!name) return Result<ModManifest>::failure(name.error, name.detail);
    if (!version_text) return Result<ModManifest>::failure(version_text.error, version_text.detail);
    if (!api_text) return Result<ModManifest>::failure(api_text.error, api_text.detail);
    if (!kind_text) return Result<ModManifest>::failure(kind_text.error, kind_text.detail);
    if (!gameplay_text) return Result<ModManifest>::failure(gameplay_text.error, gameplay_text.detail);
    if (!valid_mod_id(id.value)) {
        return Result<ModManifest>::failure(ErrorCode::invalid_argument, "invalid mod id: " + id.value);
    }

    const auto version = parse_semver(version_text.value);
    const auto api_version = parse_semver(api_text.value);
    const auto kind = parse_kind(kind_text.value);
    const auto gameplay = parse_bool(gameplay_text.value);
    if (!version) return Result<ModManifest>::failure(version.error, version.detail);
    if (!api_version) return Result<ModManifest>::failure(api_version.error, api_version.detail);
    if (!kind) return Result<ModManifest>::failure(kind.error, kind.detail);
    if (!gameplay) return Result<ModManifest>::failure(gameplay.error, gameplay.detail);
    if (!api_compatible(api_version.value)) {
        return Result<ModManifest>::failure(ErrorCode::invalid_argument,
            "mod " + id.value + " requires incompatible API " + to_string(api_version.value));
    }

    ModManifest manifest{};
    manifest.id = id.value;
    manifest.name = name.value;
    manifest.version = version.value;
    manifest.api_version = api_version.value;
    manifest.kind = kind.value;
    manifest.gameplay = gameplay.value;

    const auto entry_it = fields.find("entry");
    if (manifest.kind == ModKind::native) {
        if (entry_it == fields.end() || entry_it->second.empty()) {
            return Result<ModManifest>::failure(ErrorCode::invalid_argument, "native mod requires entry: " + manifest.id);
        }
        const std::filesystem::path declared_entry(entry_it->second);
        if (!safe_relative_path(declared_entry)) {
            return Result<ModManifest>::failure(ErrorCode::invalid_argument, "unsafe native mod entry path: " + entry_it->second);
        }
        manifest.entry = declared_entry.lexically_normal();
    } else if (entry_it != fields.end()) {
        return Result<ModManifest>::failure(ErrorCode::invalid_argument, "data mod must not declare entry: " + manifest.id);
    }

    if (const auto it = fields.find("depends"); it != fields.end()) {
        for (const auto& item : split_csv(it->second)) {
            const auto dependency = parse_dependency(item);
            if (!dependency) return Result<ModManifest>::failure(dependency.error, dependency.detail);
            manifest.dependencies.push_back(dependency.value);
        }
    }

    if (const auto it = fields.find("conflicts"); it != fields.end()) {
        for (const auto& conflict : split_csv(it->second)) {
            if (!valid_mod_id(conflict)) {
                return Result<ModManifest>::failure(ErrorCode::invalid_argument, "invalid conflict mod id: " + conflict);
            }
            manifest.conflicts.push_back(conflict);
        }
    }

    return Result<ModManifest>::success(std::move(manifest));
}

Result<ModCatalog> discover_mods(const std::filesystem::path& mods_root) {
    std::error_code ec;
    if (!std::filesystem::exists(mods_root, ec)) {
        if (ec) return Result<ModCatalog>::failure(ErrorCode::io_error, "failed to inspect mods root: " + ec.message());
        return Result<ModCatalog>::success({});
    }
    if (!std::filesystem::is_directory(mods_root, ec) || ec) {
        return Result<ModCatalog>::failure(ErrorCode::invalid_argument, "mods root is not a directory: " + mods_root.string());
    }

    ModCatalog catalog;
    for (std::filesystem::directory_iterator it(mods_root, ec), end; !ec && it != end; it.increment(ec)) {
        const auto status = it->symlink_status(ec);
        if (ec) {
            return Result<ModCatalog>::failure(
                ErrorCode::io_error,
                "failed to inspect mod directory: " + ec.message());
        }
        if (std::filesystem::is_symlink(status)) {
            return Result<ModCatalog>::failure(
                ErrorCode::invalid_argument,
                "symlinked mod directory is not allowed: " + it->path().string());
        }
        if (!std::filesystem::is_directory(status)) {
            continue;
        }
        const auto manifest_path = it->path() / "mod.ini";
        const auto manifest_status = std::filesystem::symlink_status(manifest_path, ec);
        if (ec) {
            if (ec == std::errc::no_such_file_or_directory ||
                ec == std::errc::not_a_directory) {
                ec.clear();
                continue;
            }
            return Result<ModCatalog>::failure(
                ErrorCode::io_error,
                "failed to inspect mod manifest: " + ec.message());
        }
        if (!std::filesystem::exists(manifest_status)) continue;
        if (std::filesystem::is_symlink(manifest_status)) {
            return Result<ModCatalog>::failure(
                ErrorCode::invalid_argument,
                "symlinked mod manifest is not allowed: " + manifest_path.string());
        }
        if (!std::filesystem::is_regular_file(manifest_status)) {
            return Result<ModCatalog>::failure(
                ErrorCode::invalid_argument,
                "mod manifest is not a regular file: " + manifest_path.string());
        }
        const auto text = read_text_file(manifest_path);
        if (!text) return Result<ModCatalog>::failure(text.error, text.detail);
        const auto manifest = parse_mod_manifest(text.value, it->path());
        if (!manifest) return Result<ModCatalog>::failure(manifest.error, manifest.detail);
        catalog.push_back({manifest.value, it->path()});
    }
    if (ec) return Result<ModCatalog>::failure(ErrorCode::io_error, "failed while scanning mods root: " + ec.message());

    std::sort(catalog.begin(), catalog.end(), [](const DiscoveredMod& a, const DiscoveredMod& b) {
        if (a.manifest.id != b.manifest.id) return a.manifest.id < b.manifest.id;
        return a.root.generic_string() < b.root.generic_string();
    });

    for (std::size_t i = 1; i < catalog.size(); ++i) {
        if (catalog[i - 1].manifest.id == catalog[i].manifest.id) {
            return Result<ModCatalog>::failure(
                ErrorCode::invalid_argument,
                "duplicate mod id " + catalog[i].manifest.id + " in " +
                    catalog[i - 1].root.string() + " and " + catalog[i].root.string());
        }
    }

    return Result<ModCatalog>::success(std::move(catalog));
}

}
