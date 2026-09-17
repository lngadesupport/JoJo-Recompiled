#include "core/disc_media.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>

namespace jojo {
namespace {
constexpr std::uint64_t kLogicalSectorSize = 2048;
constexpr std::uint64_t kPvdLba = 16;

std::string lower_extension(std::filesystem::path path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

bool add_mul_fits(std::uint64_t base, std::uint64_t a, std::uint64_t b,
                  std::uint64_t extra, std::uint64_t limit) noexcept {
    if (a != 0 && b > (std::numeric_limits<std::uint64_t>::max() - base) / a) return false;
    const auto product = a * b;
    if (base > std::numeric_limits<std::uint64_t>::max() - product) return false;
    const auto offset = base + product;
    return extra <= limit && offset <= limit - extra;
}

Result<LogicalSectorSource> make_candidate(const std::filesystem::path& path,
                                           std::uint64_t file_offset,
                                           std::uint32_t physical_sector_size,
                                           std::uint32_t user_data_offset,
                                           std::string format) {
    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        return Result<LogicalSectorSource>::failure(ErrorCode::file_not_found,
                                                     "media track not found: " + path.string());
    }
    if (physical_sector_size < kLogicalSectorSize ||
        user_data_offset > physical_sector_size - kLogicalSectorSize ||
        file_offset > file_size) {
        return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format,
                                                     "invalid physical sector layout");
    }
    const auto count = (file_size - file_offset) / physical_sector_size;
    if (count <= kPvdLba) {
        return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format,
                                                     "media track is too small for ISO9660");
    }
    LogicalSectorSource source{path, file_offset, file_size, physical_sector_size,
                               user_data_offset, count, std::move(format)};
    return Result<LogicalSectorSource>::success(std::move(source));
}

bool has_iso9660_pvd(const LogicalSectorSource& source) {
    auto sector = read_logical_sectors(source, kPvdLba, 1);
    return sector && sector.value.size() >= 7 && sector.value[0] == 1 &&
           std::memcmp(sector.value.data() + 1, "CD001", 5) == 0 && sector.value[6] == 1;
}

Result<LogicalSectorSource> cooked_candidate(const std::filesystem::path& path,
                                             std::string format) {
    auto candidate = make_candidate(path, 0, 2048, 0, std::move(format));
    if (candidate && has_iso9660_pvd(candidate.value)) return candidate;
    return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format,
                                                 "no cooked ISO9660 PVD found at sector 16");
}

Result<LogicalSectorSource> raw_candidate(const std::filesystem::path& path,
                                          std::uint32_t user_offset,
                                          std::string format) {
    auto candidate = make_candidate(path, 0, 2352, user_offset, std::move(format));
    if (candidate && has_iso9660_pvd(candidate.value)) return candidate;
    return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format,
                                                 "no raw ISO9660 PVD found at sector 16");
}

std::vector<std::string> split_descriptor_tokens(std::string_view line) {
    std::vector<std::string> tokens;
    std::string token;
    bool quoted = false;
    for (char ch : line) {
        if (ch == '"') { quoted = !quoted; continue; }
        if (!quoted && std::isspace(static_cast<unsigned char>(ch))) {
            if (!token.empty()) { tokens.push_back(std::move(token)); token.clear(); }
        } else {
            token.push_back(ch);
        }
    }
    if (!token.empty()) tokens.push_back(std::move(token));
    return tokens;
}

template <typename T>
bool parse_unsigned(std::string_view text, T& value) {
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    return ec == std::errc{} && ptr == end;
}

Result<std::filesystem::path> safe_descriptor_track_path(const std::filesystem::path& descriptor,
                                                         const std::string& name) {
    std::string normalized = name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::filesystem::path relative{normalized};
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        return Result<std::filesystem::path>::failure(ErrorCode::invalid_argument,
                                                       "descriptor track path must be relative");
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return Result<std::filesystem::path>::failure(ErrorCode::invalid_argument,
                                                           "descriptor track path cannot escape its directory");
        }
    }
    return Result<std::filesystem::path>::success((descriptor.parent_path() / relative).lexically_normal());
}

std::string ascii_upper_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool parse_msf(std::string_view text, std::uint64_t& frames) {
    const auto first = text.find(':');
    const auto second = first == std::string_view::npos ? std::string_view::npos : text.find(':', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos || text.find(':', second + 1) != std::string_view::npos) return false;
    std::uint32_t mm{}, ss{}, ff{};
    if (!parse_unsigned(text.substr(0, first), mm) ||
        !parse_unsigned(text.substr(first + 1, second - first - 1), ss) ||
        !parse_unsigned(text.substr(second + 1), ff) || ss >= 60 || ff >= 75) return false;
    frames = (static_cast<std::uint64_t>(mm) * 60 + ss) * 75 + ff;
    return true;
}

Result<LogicalSectorSource> open_cue_source(const std::filesystem::path& descriptor) {
    std::ifstream in(descriptor);
    if (!in) {
        return Result<LogicalSectorSource>::failure(ErrorCode::file_not_found,
                                                     "CUE descriptor not found: " + descriptor.string());
    }

    std::filesystem::path current_file;
    std::string current_mode;
    bool have_file = false;
    bool have_track = false;
    std::vector<LogicalSectorSource> candidates;
    std::string line;
    while (std::getline(in, line)) {
        const auto tokens = split_descriptor_tokens(line);
        if (tokens.empty()) continue;
        const auto command = ascii_upper_copy(tokens[0]);
        if (command == "REM") continue;
        if (command == "FILE") {
            if (tokens.size() < 3) {
                return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format, "malformed CUE FILE line");
            }
            auto path = safe_descriptor_track_path(descriptor, tokens[1]);
            if (!path) return Result<LogicalSectorSource>::failure(path.error, path.detail);
            current_file = std::move(path.value);
            have_file = true;
            have_track = false;
            current_mode.clear();
        } else if (command == "TRACK") {
            if (tokens.size() < 3 || !have_file) {
                return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format, "malformed CUE TRACK line");
            }
            current_mode = ascii_upper_copy(tokens[2]);
            have_track = true;
        } else if (command == "INDEX" && tokens.size() >= 3 && tokens[1] == "01" && have_file && have_track) {
            std::uint64_t frames{};
            if (!parse_msf(tokens[2], frames)) {
                return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format, "invalid CUE INDEX timestamp");
            }
            std::uint32_t sector_size{};
            std::uint32_t user_offset{};
            if (current_mode == "MODE1/2048") { sector_size = 2048; user_offset = 0; }
            else if (current_mode == "MODE1/2352") { sector_size = 2352; user_offset = 16; }
            else if (current_mode == "MODE2/2352") { sector_size = 2352; user_offset = 24; }
            else continue;
            if (frames > std::numeric_limits<std::uint64_t>::max() / sector_size) {
                return Result<LogicalSectorSource>::failure(ErrorCode::invalid_argument, "CUE INDEX offset overflows");
            }
            const auto file_offset = frames * sector_size;
            auto candidate = make_candidate(current_file, file_offset, sector_size, user_offset, "cue");
            if (candidate && has_iso9660_pvd(candidate.value)) candidates.push_back(std::move(candidate.value));
        }
    }
    if (candidates.empty()) {
        return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format, "CUE has no supported ISO9660 data track");
    }
    return Result<LogicalSectorSource>::success(std::move(candidates.back()));
}
}

Result<std::vector<std::uint8_t>> read_logical_sectors(const LogicalSectorSource& source,
                                                        std::uint64_t first_lba,
                                                        std::uint32_t sector_count) {
    if (sector_count == 0) return Result<std::vector<std::uint8_t>>::success({});
    if (first_lba >= source.logical_sector_count ||
        static_cast<std::uint64_t>(sector_count) > source.logical_sector_count - first_lba) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::invalid_argument,
                                                           "logical sector read is outside the data track");
    }
    if (source.physical_sector_size < kLogicalSectorSize ||
        source.user_data_offset > source.physical_sector_size - kLogicalSectorSize) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::invalid_argument,
                                                           "invalid logical sector source layout");
    }
    if (static_cast<std::uint64_t>(sector_count) >
        std::numeric_limits<std::size_t>::max() / kLogicalSectorSize) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::invalid_argument,
                                                           "logical sector read is too large");
    }

    std::ifstream in(source.file_path, std::ios::binary);
    if (!in) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::io_error,
                                                           "cannot open media track");
    }
    std::vector<std::uint8_t> result(static_cast<std::size_t>(sector_count) * kLogicalSectorSize);
    for (std::uint32_t i = 0; i < sector_count; ++i) {
        const auto lba = first_lba + i;
        const auto extra = static_cast<std::uint64_t>(source.user_data_offset) + kLogicalSectorSize;
        if (!add_mul_fits(source.file_offset, lba, source.physical_sector_size, extra, source.file_size)) {
            return Result<std::vector<std::uint8_t>>::failure(ErrorCode::invalid_argument,
                                                               "physical sector read is outside the track file");
        }
        const auto physical = source.file_offset + lba * source.physical_sector_size + source.user_data_offset;
        in.seekg(static_cast<std::streamoff>(physical));
        if (!in) {
            return Result<std::vector<std::uint8_t>>::failure(ErrorCode::io_error,
                                                               "cannot seek media track sector");
        }
        auto* out = result.data() + static_cast<std::size_t>(i) * kLogicalSectorSize;
        in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(kLogicalSectorSize));
        if (in.gcount() != static_cast<std::streamsize>(kLogicalSectorSize)) {
            return Result<std::vector<std::uint8_t>>::failure(ErrorCode::io_error,
                                                               "short read from media track sector");
        }
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(result));
}

Result<LogicalSectorSource> open_logical_sector_source(const std::filesystem::path& source_path) {
    const auto ext = lower_extension(source_path);
    if (ext == ".iso") return cooked_candidate(source_path, "iso");
    if (ext == ".cue") return open_cue_source(source_path);
    if (ext == ".bin") {
        if (auto cooked = cooked_candidate(source_path, "bin-cooked"); cooked) return cooked;
        if (auto mode1 = raw_candidate(source_path, 16, "bin-mode1-2352"); mode1) return mode1;
        if (auto mode2 = raw_candidate(source_path, 24, "bin-mode2-2352"); mode2) return mode2;
        return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format,
                                                     "BIN does not contain a supported ISO9660 data layout");
    }
    return Result<LogicalSectorSource>::failure(ErrorCode::unsupported_format,
                                                 "unsupported PS1 media extension " + ext + "; expected .iso, .bin, or .cue");
}

}
