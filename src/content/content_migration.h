#pragma once

#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace jojo::content {

enum class ContentKind : std::uint8_t {
    graphics_pack,
    character_data,
    hitbox_data,
    script_data,
    ui_data,
    audio_xa,
    palette,
    color_metadata,
    binary_data,
};

struct ContentEntry {
    std::string source_path;
    std::string output_path;
    ContentKind kind{ContentKind::binary_data};
    std::uint64_t size_bytes{};
    std::uint64_t fnv1a64{};
    std::string derived_path;
};

struct ContentImportSummary {
    std::string source_format;
    std::uint64_t logical_sector_count{};
    std::uint64_t files_imported{};
    std::uint64_t bytes_imported{};
    std::uint64_t files_excluded{};
    std::vector<ContentEntry> entries;
};

[[nodiscard]] std::string content_kind_name(ContentKind kind);
[[nodiscard]] ContentKind classify_content_path(const std::string& iso_path);
[[nodiscard]] bool is_runtime_only_source(const std::string& iso_path);
[[nodiscard]] Result<ContentImportSummary> import_game_content(
    const std::filesystem::path& source,
    const std::filesystem::path& output_root);

} // namespace jojo::content
