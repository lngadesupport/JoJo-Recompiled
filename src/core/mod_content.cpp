#include "core/mod_runtime.h"
#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <utility>
#include <vector>

namespace jojo {
namespace {

struct ContentFile {
    std::string relative_path;
    std::filesystem::path host_path;
};

Result<std::vector<ContentFile>> collect_regular_files(
    const std::filesystem::path& root,
    const std::filesystem::path& relative_base) {
    std::error_code ec;
    const auto root_status = std::filesystem::symlink_status(root, ec);
    if (ec) {
        return Result<std::vector<ContentFile>>::failure(
            ErrorCode::io_error, "failed to inspect mod path: " + root.string() + ": " + ec.message());
    }
    if (std::filesystem::is_symlink(root_status)) {
        return Result<std::vector<ContentFile>>::failure(
            ErrorCode::invalid_argument, "symlinked mod path is not allowed: " + root.string());
    }
    if (!std::filesystem::exists(root_status)) {
        return Result<std::vector<ContentFile>>::success({});
    }
    if (!std::filesystem::is_directory(root_status)) {
        return Result<std::vector<ContentFile>>::failure(
            ErrorCode::invalid_argument, "mod path is not a directory: " + root.string());
    }

    std::vector<ContentFile> files;
    std::filesystem::recursive_directory_iterator it(root, ec), end;
    if (ec) {
        return Result<std::vector<ContentFile>>::failure(
            ErrorCode::io_error, "failed to enumerate mod path: " + root.string() + ": " + ec.message());
    }
    while (it != end) {
        const auto path = it->path();
        const auto status = it->symlink_status(ec);
        if (ec) {
            return Result<std::vector<ContentFile>>::failure(
                ErrorCode::io_error, "failed to inspect mod content: " + path.string() + ": " + ec.message());
        }
        if (std::filesystem::is_symlink(status)) {
            return Result<std::vector<ContentFile>>::failure(
                ErrorCode::invalid_argument, "symlinked mod content is not allowed: " + path.string());
        }
        if (std::filesystem::is_regular_file(status)) {
            const auto relative = path.lexically_relative(relative_base).lexically_normal();
            if (relative.empty() || relative == ".") {
                return Result<std::vector<ContentFile>>::failure(
                    ErrorCode::invalid_argument, "failed to derive mod content path: " + path.string());
            }
            const auto logical = relative.generic_string();
            if (logical.empty() || logical.starts_with("../") || logical == "..") {
                return Result<std::vector<ContentFile>>::failure(
                    ErrorCode::invalid_argument, "mod content escaped its root: " + path.string());
            }
            files.push_back({logical, path});
        } else if (!std::filesystem::is_directory(status)) {
            return Result<std::vector<ContentFile>>::failure(
                ErrorCode::invalid_argument, "unsupported mod filesystem object: " + path.string());
        }
        it.increment(ec);
        if (ec) {
            return Result<std::vector<ContentFile>>::failure(
                ErrorCode::io_error, "failed while enumerating mod content: " + ec.message());
        }
    }

    std::sort(files.begin(), files.end(), [](const ContentFile& a, const ContentFile& b) {
        return a.relative_path < b.relative_path;
    });
    return Result<std::vector<ContentFile>>::success(std::move(files));
}

Result<void> hash_bytes(Sha256Hasher& hasher, std::span<const std::uint8_t> bytes) {
    const auto updated = hasher.update(bytes);
    if (!updated) return Result<void>::failure(updated.error, updated.detail);
    return Result<void>::success();
}

Result<void> hash_u64(Sha256Hasher& hasher, std::uint64_t value) {
    std::array<std::uint8_t, 8> encoded{};
    for (unsigned i = 0; i < 8; ++i) encoded[7u - i] = static_cast<std::uint8_t>(value >> (i * 8u));
    return hash_bytes(hasher, encoded);
}

Result<void> hash_string(Sha256Hasher& hasher, std::string_view value) {
    const auto length = hash_u64(hasher, static_cast<std::uint64_t>(value.size()));
    if (!length) return length;
    return hash_bytes(hasher, std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

Result<void> hash_file_payload(Sha256Hasher& hasher, const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return Result<void>::failure(
            ErrorCode::io_error, "failed to read mod file size: " + path.string() + ": " + ec.message());
    }
    const auto length = hash_u64(hasher, size);
    if (!length) return length;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<void>::failure(ErrorCode::file_not_found, "failed to open mod content: " + path.string());
    }
    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0) {
            const auto updated = hash_bytes(hasher, std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(buffer.data()), static_cast<std::size_t>(count)));
            if (!updated) return updated;
        }
    }
    if (!in.eof()) {
        return Result<void>::failure(ErrorCode::io_error, "failed while reading mod content: " + path.string());
    }
    return Result<void>::success();
}

Result<void> hash_mod_tuple(
    Sha256Hasher& hasher,
    const DiscoveredMod& mod,
    std::string_view content_hash) {
    auto result = hash_string(hasher, mod.manifest.id);
    if (!result) return result;
    result = hash_string(hasher, to_string(mod.manifest.version));
    if (!result) return result;
    return hash_string(hasher, content_hash);
}

Result<std::string> finish_hex(Sha256Hasher& hasher) {
    const auto digest = hasher.finalize();
    if (!digest) return Result<std::string>::failure(digest.error, digest.detail);
    return Result<std::string>::success(sha256_hex(digest.value));
}

}

Result<ModOverlay> build_mod_overlay(const ResolvedModSet& mods) {
    ModOverlay overlay{};
    for (const auto* mod : mods.load_order) {
        if (mod == nullptr) {
            return Result<ModOverlay>::failure(ErrorCode::invalid_argument, "resolved mod set contains a null mod");
        }
        if (mod->manifest.kind != ModKind::data) continue;

        const auto data_root = mod->root / "data";
        const auto files = collect_regular_files(data_root, data_root);
        if (!files) return Result<ModOverlay>::failure(files.error, files.detail);

        for (const auto& file : files.value) {
            if (const auto previous = overlay.files.find(file.relative_path); previous != overlay.files.end()) {
                overlay.collisions.push_back({
                    file.relative_path,
                    previous->second.mod_id,
                    mod->manifest.id,
                });
            }
            overlay.files[file.relative_path] = {mod->manifest.id, file.host_path};
        }
    }
    return Result<ModOverlay>::success(std::move(overlay));
}

Result<std::string> compute_mod_content_hash(const DiscoveredMod& mod) {
    const auto files = collect_regular_files(mod.root, mod.root);
    if (!files) return Result<std::string>::failure(files.error, files.detail);

    Sha256Hasher hasher;
    for (const auto& file : files.value) {
        auto result = hash_string(hasher, file.relative_path);
        if (!result) return Result<std::string>::failure(result.error, result.detail);
        result = hash_file_payload(hasher, file.host_path);
        if (!result) return Result<std::string>::failure(result.error, result.detail);
    }
    return finish_hex(hasher);
}

Result<ModSetHashes> compute_mod_set_hashes(const ResolvedModSet& mods) {
    Sha256Hasher full;
    Sha256Hasher gameplay;

    for (const auto* mod : mods.load_order) {
        if (mod == nullptr) {
            return Result<ModSetHashes>::failure(ErrorCode::invalid_argument, "resolved mod set contains a null mod");
        }
        const auto content_hash = compute_mod_content_hash(*mod);
        if (!content_hash) return Result<ModSetHashes>::failure(content_hash.error, content_hash.detail);

        auto result = hash_mod_tuple(full, *mod, content_hash.value);
        if (!result) return Result<ModSetHashes>::failure(result.error, result.detail);
        if (mod->manifest.gameplay) {
            result = hash_mod_tuple(gameplay, *mod, content_hash.value);
            if (!result) return Result<ModSetHashes>::failure(result.error, result.detail);
        }
    }

    const auto full_hash = finish_hex(full);
    if (!full_hash) return Result<ModSetHashes>::failure(full_hash.error, full_hash.detail);
    const auto gameplay_hash = finish_hex(gameplay);
    if (!gameplay_hash) return Result<ModSetHashes>::failure(gameplay_hash.error, gameplay_hash.detail);
    return Result<ModSetHashes>::success({full_hash.value, gameplay_hash.value});
}

}
