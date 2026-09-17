#pragma once
#include "core/disc_media.h"
#include "core/result.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

struct DiscFileEntry {
    std::string name;
    std::string path;
    bool is_directory{};
    std::uint32_t extent_lba{};
    std::uint32_t size_bytes{};
};

struct Iso9660Image {
    LogicalSectorSource sectors;
    std::uint32_t root_extent_lba{};
    std::uint32_t root_size_bytes{};
};

[[nodiscard]] Result<Iso9660Image> open_iso9660(const std::filesystem::path& path);
[[nodiscard]] Result<Iso9660Image> open_iso9660(const LogicalSectorSource& sectors);
[[nodiscard]] Result<std::vector<DiscFileEntry>> list_iso9660_directory(
    const Iso9660Image& image,
    std::string_view virtual_path);
[[nodiscard]] Result<std::vector<std::uint8_t>> read_iso9660_file(
    const Iso9660Image& image,
    std::string_view virtual_path);

}
