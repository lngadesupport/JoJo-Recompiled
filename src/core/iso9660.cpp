#include "core/iso9660.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

namespace jojo {
namespace {
constexpr std::uint64_t kSectorSize = 2048;
constexpr std::uint64_t kPvdSector = 16;

std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool bounded_extent(const LogicalSectorSource& source,
                    std::uint32_t lba,
                    std::uint32_t bytes) noexcept {
    if (bytes == 0) return static_cast<std::uint64_t>(lba) <= source.logical_sector_count;
    const std::uint64_t sectors = (static_cast<std::uint64_t>(bytes) + kSectorSize - 1) / kSectorSize;
    return static_cast<std::uint64_t>(lba) < source.logical_sector_count &&
           sectors <= source.logical_sector_count - lba;
}

Result<std::vector<std::uint8_t>> read_extent(const Iso9660Image& image,
                                               std::uint32_t lba,
                                               std::uint32_t bytes) {
    if (!bounded_extent(image.sectors, lba, bytes)) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_installation, "ISO9660 extent is outside the logical data track");
    }
    if (bytes == 0) return Result<std::vector<std::uint8_t>>::success({});
    const auto sectors = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(bytes) + kSectorSize - 1) / kSectorSize);
    auto data = read_logical_sectors(image.sectors, lba, sectors);
    if (!data) return Result<std::vector<std::uint8_t>>::failure(data.error, data.detail);
    data.value.resize(bytes);
    return data;
}

std::string normalize_name(std::string name) {
    const auto semicolon = name.find(';');
    if (semicolon != std::string::npos) name.resize(semicolon);
    if (!name.empty() && name.back() == '.') name.pop_back();
    return name;
}

Result<std::vector<DiscFileEntry>> parse_directory(const Iso9660Image& image,
                                                    std::uint32_t lba,
                                                    std::uint32_t size,
                                                    std::string_view parent_path) {
    auto bytes = read_extent(image, lba, size);
    if (!bytes) return Result<std::vector<DiscFileEntry>>::failure(bytes.error, bytes.detail);

    std::vector<DiscFileEntry> entries;
    std::size_t cursor = 0;
    while (cursor < bytes.value.size()) {
        const std::uint8_t record_len = bytes.value[cursor];
        if (record_len == 0) {
            const std::size_t next_sector = ((cursor / kSectorSize) + 1) * kSectorSize;
            if (next_sector <= cursor) break;
            cursor = std::min(next_sector, bytes.value.size());
            continue;
        }
        if (record_len < 34 || cursor + record_len > bytes.value.size() ||
            (cursor % kSectorSize) + record_len > kSectorSize) {
            return Result<std::vector<DiscFileEntry>>::failure(
                ErrorCode::invalid_installation, "malformed ISO9660 directory record");
        }
        const auto* record = bytes.value.data() + cursor;
        const std::uint8_t name_len = record[32];
        if (name_len == 0 || 33u + name_len > record_len) {
            return Result<std::vector<DiscFileEntry>>::failure(
                ErrorCode::invalid_installation, "malformed ISO9660 file identifier");
        }
        const bool special = name_len == 1 && (record[33] == 0 || record[33] == 1);
        if (!special) {
            std::string name(reinterpret_cast<const char*>(record + 33), name_len);
            name = normalize_name(std::move(name));
            if (name.empty()) {
                return Result<std::vector<DiscFileEntry>>::failure(
                    ErrorCode::invalid_installation, "empty ISO9660 file identifier");
            }
            const auto extent_lba = le32(record + 2);
            const auto file_size = le32(record + 10);
            if (!bounded_extent(image.sectors, extent_lba, file_size)) {
                return Result<std::vector<DiscFileEntry>>::failure(
                    ErrorCode::invalid_installation, "ISO9660 directory entry points outside the data track");
            }
            std::string full = parent_path.empty() || parent_path == "/" ? "/" + name
                                                                           : std::string(parent_path) + "/" + name;
            entries.push_back({std::move(name), std::move(full), (record[25] & 0x02u) != 0,
                               extent_lba, file_size});
        }
        cursor += record_len;
    }
    return Result<std::vector<DiscFileEntry>>::success(std::move(entries));
}

std::string ascii_upper(std::string value) {
    for (auto& ch : value) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}

Result<std::vector<std::string>> split_virtual_path(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') ++start;
        if (start == path.size()) break;
        const auto slash = path.find('/', start);
        const auto end = slash == std::string_view::npos ? path.size() : slash;
        std::string part(path.substr(start, end - start));
        if (part == "..") {
            return Result<std::vector<std::string>>::failure(
                ErrorCode::invalid_argument, "parent traversal is not allowed in ISO9660 paths");
        }
        if (!part.empty() && part != ".") parts.push_back(std::move(part));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return Result<std::vector<std::string>>::success(std::move(parts));
}

Result<DiscFileEntry> find_entry(const Iso9660Image& image, std::string_view virtual_path) {
    auto parts = split_virtual_path(virtual_path);
    if (!parts) return Result<DiscFileEntry>::failure(parts.error, parts.detail);
    if (parts.value.empty()) {
        return Result<DiscFileEntry>::failure(ErrorCode::invalid_argument, "path refers to ISO9660 root");
    }

    std::uint32_t lba = image.root_extent_lba;
    std::uint32_t size = image.root_size_bytes;
    std::string parent = "/";
    DiscFileEntry found{};
    for (std::size_t i = 0; i < parts.value.size(); ++i) {
        auto entries = parse_directory(image, lba, size, parent);
        if (!entries) return Result<DiscFileEntry>::failure(entries.error, entries.detail);
        const auto wanted = ascii_upper(parts.value[i]);
        const auto it = std::find_if(entries.value.begin(), entries.value.end(), [&](const DiscFileEntry& entry) {
            return ascii_upper(entry.name) == wanted;
        });
        if (it == entries.value.end()) {
            return Result<DiscFileEntry>::failure(ErrorCode::file_not_found,
                                                   "ISO9660 path not found: " + std::string(virtual_path));
        }
        found = *it;
        if (i + 1 < parts.value.size()) {
            if (!found.is_directory) {
                return Result<DiscFileEntry>::failure(ErrorCode::invalid_argument,
                                                       "ISO9660 path component is not a directory: " + found.path);
            }
            lba = found.extent_lba;
            size = found.size_bytes;
            parent = found.path;
        }
    }
    return Result<DiscFileEntry>::success(std::move(found));
}
}

Result<Iso9660Image> open_iso9660(const LogicalSectorSource& sectors) {
    if (sectors.logical_sector_count <= kPvdSector) {
        return Result<Iso9660Image>::failure(ErrorCode::unsupported_format,
                                             "data track is too small to contain an ISO9660 PVD");
    }
    auto pvd_read = read_logical_sectors(sectors, kPvdSector, 1);
    if (!pvd_read) return Result<Iso9660Image>::failure(pvd_read.error, pvd_read.detail);
    const auto& pvd = pvd_read.value;
    if (pvd.size() != kSectorSize || pvd[0] != 1 ||
        std::memcmp(pvd.data() + 1, "CD001", 5) != 0 || pvd[6] != 1) {
        return Result<Iso9660Image>::failure(ErrorCode::unsupported_format,
                                             "invalid ISO9660 Primary Volume Descriptor");
    }
    const auto* root = pvd.data() + 156;
    if (root[0] < 34 || root[32] != 1 || root[33] != 0 || (root[25] & 0x02u) == 0) {
        return Result<Iso9660Image>::failure(ErrorCode::invalid_installation,
                                             "invalid ISO9660 root directory record");
    }
    const auto root_lba = le32(root + 2);
    const auto root_size = le32(root + 10);
    if (!bounded_extent(sectors, root_lba, root_size)) {
        return Result<Iso9660Image>::failure(ErrorCode::invalid_installation,
                                             "ISO9660 root directory is outside the data track");
    }
    return Result<Iso9660Image>::success(Iso9660Image{sectors, root_lba, root_size});
}

Result<Iso9660Image> open_iso9660(const std::filesystem::path& path) {
    auto source = open_logical_sector_source(path);
    if (!source) return Result<Iso9660Image>::failure(source.error, source.detail);
    return open_iso9660(source.value);
}

Result<std::vector<DiscFileEntry>> list_iso9660_directory(const Iso9660Image& image,
                                                          std::string_view virtual_path) {
    if (virtual_path.empty() || virtual_path == "/") {
        return parse_directory(image, image.root_extent_lba, image.root_size_bytes, "/");
    }
    auto entry = find_entry(image, virtual_path);
    if (!entry) return Result<std::vector<DiscFileEntry>>::failure(entry.error, entry.detail);
    if (!entry.value.is_directory) {
        return Result<std::vector<DiscFileEntry>>::failure(ErrorCode::invalid_argument,
                                                            "ISO9660 path is not a directory: " + entry.value.path);
    }
    return parse_directory(image, entry.value.extent_lba, entry.value.size_bytes, entry.value.path);
}

Result<std::vector<std::uint8_t>> read_iso9660_file(const Iso9660Image& image,
                                                    std::string_view virtual_path) {
    auto entry = find_entry(image, virtual_path);
    if (!entry) return Result<std::vector<std::uint8_t>>::failure(entry.error, entry.detail);
    if (entry.value.is_directory) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::invalid_argument,
                                                          "ISO9660 path is a directory: " + entry.value.path);
    }
    return read_extent(image, entry.value.extent_lba, entry.value.size_bytes);
}

}
