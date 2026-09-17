#include "core/ps1_installation.h"

#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace jojo {
namespace {

constexpr std::string_view kGenerationPrefix = "generation-";
constexpr std::uint32_t kMaxGenerationNumber = 999999u;

std::string format_generation_id(std::uint32_t number) {
    std::ostringstream out;
    out << kGenerationPrefix << std::setfill('0') << std::setw(6) << number;
    return out.str();
}

std::optional<std::uint32_t> parse_generation_id(std::string_view id) {
    if (id.size() != kGenerationPrefix.size() + 6u ||
        id.substr(0, kGenerationPrefix.size()) != kGenerationPrefix) {
        return std::nullopt;
    }
    const auto digits = id.substr(kGenerationPrefix.size());
    for (const char c : digits) {
        if (c < '0' || c > '9') return std::nullopt;
    }
    std::uint32_t number{};
    const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (ec != std::errc{} || ptr != digits.data() + digits.size() ||
        number == 0u || number > kMaxGenerationNumber) {
        return std::nullopt;
    }
    return number;
}

bool same_path(const std::filesystem::path& a, const std::filesystem::path& b) {
    return a.lexically_normal().generic_string() == b.lexically_normal().generic_string();
}

Result<void> atomic_replace_file(const std::filesystem::path& temp,
                                 const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::io_error,
                                 "failed to atomically replace active installation pointer");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to atomically replace active installation pointer: " +
                                         ec.message());
    }
    return Result<void>::success();
#endif
}

Result<std::filesystem::path> nearest_existing_directory(
    const std::filesystem::path& target) {
    std::filesystem::path probe = target;
    std::error_code ec;
    while (!probe.empty()) {
        const bool exists = std::filesystem::exists(probe, ec);
        if (ec) {
            return Result<std::filesystem::path>::failure(
                ErrorCode::io_error, "failed while inspecting install destination: " + ec.message());
        }
        if (exists) {
            if (!std::filesystem::is_directory(probe, ec) || ec) {
                return Result<std::filesystem::path>::failure(
                    ErrorCode::invalid_installation,
                    "nearest existing install destination ancestor is not a directory");
            }
            return Result<std::filesystem::path>::success(probe);
        }
        const auto parent = probe.parent_path();
        if (parent.empty() || parent == probe) break;
        probe = parent;
    }
    return Result<std::filesystem::path>::failure(
        ErrorCode::invalid_installation,
        "install destination has no existing directory ancestor");
}

Result<void> verify_directory_writable(const std::filesystem::path& directory) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto probe = directory /
        (".jojo-write-probe-" + std::to_string(static_cast<long long>(stamp)) + ".tmp");
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Result<void>::failure(ErrorCode::invalid_installation,
                                         "install destination is not writable");
        }
        out.put('\0');
        out.flush();
        if (!out) {
            std::error_code cleanup_ec;
            std::filesystem::remove(probe, cleanup_ec);
            return Result<void>::failure(ErrorCode::invalid_installation,
                                         "install destination write probe failed");
        }
    }
    std::error_code ec;
    std::filesystem::remove(probe, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to remove install destination write probe: " +
                                         ec.message());
    }
    return Result<void>::success();
}

Result<std::uint32_t> highest_generation_number(const std::filesystem::path& root) {
    std::uint32_t highest = 0u;
    const std::array<std::filesystem::path, 2> parents{{
        root / "generations",
        root / ".staging",
    }};

    for (const auto& parent : parents) {
        std::error_code ec;
        if (!std::filesystem::exists(parent, ec)) {
            if (ec) {
                return Result<std::uint32_t>::failure(
                    ErrorCode::io_error, "failed to inspect generation directory: " + ec.message());
            }
            continue;
        }
        if (!std::filesystem::is_directory(parent, ec) || ec) {
            return Result<std::uint32_t>::failure(
                ErrorCode::invalid_installation,
                "generation container is not a directory");
        }
        std::filesystem::directory_iterator it(parent, ec);
        if (ec) {
            return Result<std::uint32_t>::failure(
                ErrorCode::io_error, "failed to enumerate generation directory: " + ec.message());
        }
        for (const auto& entry : it) {
            const auto parsed = parse_generation_id(entry.path().filename().string());
            if (parsed.has_value() && *parsed > highest) highest = *parsed;
        }
    }
    return Result<std::uint32_t>::success(highest);
}

bool contains_parent_component(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (component == "..") return true;
    }
    return false;
}

struct ActivePointer {
    std::string format;
    std::string generation_id;
    std::filesystem::path manifest;
};

Result<ActivePointer> read_active_pointer(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return Result<ActivePointer>::failure(ErrorCode::file_not_found,
                                              "active installation pointer not found");
    }

    ActivePointer pointer{};
    std::unordered_set<std::string> seen;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0u) {
            return Result<ActivePointer>::failure(ErrorCode::invalid_installation,
                                                  "malformed active installation pointer");
        }
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1u);
        if (!seen.insert(key).second) {
            return Result<ActivePointer>::failure(ErrorCode::invalid_installation,
                                                  "duplicate active installation pointer key");
        }
        if (key == "format") pointer.format = value;
        else if (key == "generation_id") pointer.generation_id = value;
        else if (key == "manifest") pointer.manifest = std::filesystem::path(value);
        else {
            return Result<ActivePointer>::failure(ErrorCode::invalid_installation,
                                                  "unknown active installation pointer key");
        }
    }

    if (seen.size() != 3u || pointer.format != "1" ||
        !parse_generation_id(pointer.generation_id).has_value() ||
        pointer.manifest.empty()) {
        return Result<ActivePointer>::failure(ErrorCode::invalid_installation,
                                              "active installation pointer is incomplete or invalid");
    }
    return Result<ActivePointer>::success(std::move(pointer));
}

} // namespace

Result<void> validate_install_destination(const std::filesystem::path& install_root,
                                          std::uint64_t required_bytes) {
    if (install_root.empty()) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "installation destination cannot be empty");
    }

    std::error_code ec;
    const bool exists = std::filesystem::exists(install_root, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to inspect installation destination: " + ec.message());
    }
    if (exists && !std::filesystem::is_directory(install_root, ec)) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "installation destination is not a directory");
    }
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to inspect installation destination type: " + ec.message());
    }

    auto existing = nearest_existing_directory(install_root);
    if (!existing) return Result<void>::failure(existing.error, existing.detail);

    const auto writable_directory = exists ? install_root : existing.value;
    auto writable = verify_directory_writable(writable_directory);
    if (!writable) return writable;

    const auto space = std::filesystem::space(existing.value, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to query installation destination free space: " + ec.message());
    }
    if (required_bytes > space.available) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "installation destination does not have enough free space");
    }
    return Result<void>::success();
}

Result<PendingInstallGeneration> begin_install_generation(
    const std::filesystem::path& install_root) {
    if (install_root.empty()) {
        return Result<PendingInstallGeneration>::failure(
            ErrorCode::invalid_argument, "installation destination cannot be empty");
    }

    std::error_code ec;
    std::filesystem::create_directories(install_root / ".staging", ec);
    if (ec) {
        return Result<PendingInstallGeneration>::failure(
            ErrorCode::io_error, "failed to create staging directory: " + ec.message());
    }
    std::filesystem::create_directories(install_root / "generations", ec);
    if (ec) {
        return Result<PendingInstallGeneration>::failure(
            ErrorCode::io_error, "failed to create generations directory: " + ec.message());
    }

    auto highest = highest_generation_number(install_root);
    if (!highest) {
        return Result<PendingInstallGeneration>::failure(highest.error, highest.detail);
    }
    if (highest.value >= kMaxGenerationNumber) {
        return Result<PendingInstallGeneration>::failure(
            ErrorCode::invalid_installation, "installation generation sequence is exhausted");
    }

    PendingInstallGeneration generation{};
    generation.generation_id = format_generation_id(highest.value + 1u);
    generation.staging_dir = install_root / ".staging" / generation.generation_id;
    generation.final_dir = install_root / "generations" / generation.generation_id;

    if (std::filesystem::exists(generation.staging_dir, ec) || ec ||
        std::filesystem::exists(generation.final_dir, ec) || ec) {
        return Result<PendingInstallGeneration>::failure(
            ErrorCode::invalid_installation, "next installation generation already exists");
    }
    std::filesystem::create_directory(generation.staging_dir, ec);
    if (ec) {
        return Result<PendingInstallGeneration>::failure(
            ErrorCode::io_error, "failed to create installation staging generation: " + ec.message());
    }
    return Result<PendingInstallGeneration>::success(std::move(generation));
}

Result<void> commit_install_generation(const std::filesystem::path& install_root,
                                       const PendingInstallGeneration& generation) {
    if (install_root.empty() || !parse_generation_id(generation.generation_id).has_value()) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "invalid installation generation identity");
    }

    const auto expected_staging = install_root / ".staging" / generation.generation_id;
    const auto expected_final = install_root / "generations" / generation.generation_id;
    if (!same_path(generation.staging_dir, expected_staging) ||
        !same_path(generation.final_dir, expected_final)) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "installation generation paths do not match install root");
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(expected_staging, ec) || ec) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "installation staging generation does not exist");
    }
    const auto staging_manifest = expected_staging / "game_manifest.ini";
    if (!std::filesystem::is_regular_file(staging_manifest, ec) || ec) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "installation staging generation has no manifest");
    }
    if (std::filesystem::exists(expected_final, ec) || ec) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "final installation generation already exists");
    }

    std::filesystem::rename(expected_staging, expected_final, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to commit installation generation: " + ec.message());
    }

    const auto pointer_path = install_root / "active_install.ini";
    auto pointer_temp = pointer_path;
    pointer_temp += ".tmp";
    {
        std::ofstream out(pointer_temp, std::ios::trunc);
        if (!out) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed to create temporary active installation pointer");
        }
        out << "format=1\n";
        out << "generation_id=" << generation.generation_id << '\n';
        out << "manifest=generations/" << generation.generation_id
            << "/game_manifest.ini\n";
        out.flush();
        if (!out) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed while writing active installation pointer");
        }
    }

    return atomic_replace_file(pointer_temp, pointer_path);
}

Result<ActiveInstallGeneration> resolve_active_install_generation(
    const std::filesystem::path& install_root) {
    if (install_root.empty()) {
        return Result<ActiveInstallGeneration>::failure(
            ErrorCode::invalid_argument, "installation destination cannot be empty");
    }

    auto pointer = read_active_pointer(install_root / "active_install.ini");
    if (!pointer) {
        return Result<ActiveInstallGeneration>::failure(pointer.error, pointer.detail);
    }

    if (pointer.value.manifest.is_absolute() ||
        pointer.value.manifest.has_root_name() ||
        pointer.value.manifest.has_root_directory() ||
        contains_parent_component(pointer.value.manifest)) {
        return Result<ActiveInstallGeneration>::failure(
            ErrorCode::invalid_installation, "active manifest path escapes installation root");
    }

    const auto expected_relative = std::filesystem::path("generations") /
                                   pointer.value.generation_id /
                                   "game_manifest.ini";
    if (!same_path(pointer.value.manifest, expected_relative)) {
        return Result<ActiveInstallGeneration>::failure(
            ErrorCode::invalid_installation,
            "active manifest path does not match generation identity");
    }

    ActiveInstallGeneration active{};
    active.install_root = install_root;
    active.generation_id = pointer.value.generation_id;
    active.generation_dir = install_root / "generations" / active.generation_id;
    active.manifest_path = active.generation_dir / "game_manifest.ini";

    std::error_code ec;
    if (!std::filesystem::is_directory(active.generation_dir, ec) || ec ||
        !std::filesystem::is_regular_file(active.manifest_path, ec) || ec) {
        return Result<ActiveInstallGeneration>::failure(
            ErrorCode::invalid_installation,
            "active installation generation or manifest does not exist");
    }
    return Result<ActiveInstallGeneration>::success(std::move(active));
}

} // namespace jojo
