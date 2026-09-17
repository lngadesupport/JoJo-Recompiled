#pragma once
#include "core/result.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace jojo {

struct LogicalSectorSource {
    std::filesystem::path file_path;
    std::uint64_t file_offset{};
    std::uint64_t file_size{};
    std::uint32_t physical_sector_size{2048};
    std::uint32_t user_data_offset{};
    std::uint64_t logical_sector_count{};
    std::string source_format;
};

[[nodiscard]] Result<LogicalSectorSource> open_logical_sector_source(
    const std::filesystem::path& source_path);

[[nodiscard]] Result<std::vector<std::uint8_t>> read_logical_sectors(
    const LogicalSectorSource& source,
    std::uint64_t first_lba,
    std::uint32_t sector_count);

}
