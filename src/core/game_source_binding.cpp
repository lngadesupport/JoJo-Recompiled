#include "core/game_source_binding.h"

#include "core/ps1_disc_session.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <map>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace jojo {
namespace {

constexpr std::string_view kBindingFormat = "jojo-game-source-binding-v1";

std::string lower_extension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

bool supported_source_extension(const std::filesystem::path& path) {
    const auto ext = lower_extension(path);
    return ext == ".iso" || ext == ".bin" || ext == ".cue";
}

std::string path_to_utf8(const std::filesystem::path& path) {
#if defined(__cpp_lib_char8_t)
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
#else
    return path.u8string();
#endif
}

std::filesystem::path path_from_utf8(const std::string& text) {
#if defined(__cpp_lib_char8_t)
    std::u8string encoded;
    encoded.reserve(text.size());
    for (const unsigned char ch : text) encoded.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(encoded);
#else
    return std::filesystem::u8path(text);
#endif
}

bool is_lower_hex_16(std::string_view value) {
    if (value.size() != 16u) return false;
    return std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

bool single_line(std::string_view value) {
    return value.find('\n') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos;
}

Result<void> validate_binding(const GameSourceBinding& binding) {
    if (binding.source_path.empty() || !binding.source_path.is_absolute()) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "game source binding requires an absolute source path");
    }
    const auto selected_format = lower_extension(binding.source_path);
    const std::string expected_format = selected_format.empty() ? std::string{} : selected_format.substr(1);
    if ((binding.source_format != "iso" && binding.source_format != "bin" && binding.source_format != "cue") ||
        binding.source_format != expected_format) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "game source binding contains an invalid or mismatched source format");
    }
    if (binding.source_size == 0u) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "game source binding contains an invalid source size");
    }
    if (!is_lower_hex_16(binding.source_hash_fnv1a64)) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "game source binding hash must be 16 lowercase hexadecimal digits");
    }
    if (binding.revision_id.empty() || !single_line(binding.revision_id)) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "game source binding contains an invalid revision id");
    }
    if (!single_line(path_to_utf8(binding.source_path))) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "game source binding path cannot contain line breaks");
    }
    return Result<void>::success();
}

Result<void> replace_file(const std::filesystem::path& temp,
                          const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Result<void>::success();
    }
    return Result<void>::failure(
        ErrorCode::io_error,
        "failed to replace game source binding file");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "failed to replace game source binding file: " + ec.message());
    }
    return Result<void>::success();
#endif
}

Result<std::uint64_t> parse_u64(std::string_view text) {
    std::uint64_t value{};
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return Result<std::uint64_t>::failure(
            ErrorCode::invalid_installation,
            "game source binding contains an invalid unsigned integer");
    }
    return Result<std::uint64_t>::success(value);
}

} // namespace

Result<std::optional<std::filesystem::path>> discover_single_ps1_source(
    const std::filesystem::path& rom_dir) {
    if (rom_dir.empty()) {
        return Result<std::optional<std::filesystem::path>>::failure(
            ErrorCode::invalid_argument,
            "ROM directory cannot be empty");
    }

    std::error_code ec;
    if (!std::filesystem::exists(rom_dir, ec)) {
        if (ec) {
            return Result<std::optional<std::filesystem::path>>::failure(
                ErrorCode::io_error,
                "failed to inspect ROM directory: " + ec.message());
        }
        return Result<std::optional<std::filesystem::path>>::success(std::nullopt);
    }
    if (!std::filesystem::is_directory(rom_dir, ec) || ec) {
        return Result<std::optional<std::filesystem::path>>::failure(
            ErrorCode::invalid_argument,
            "ROM path is not a directory");
    }

    std::vector<std::filesystem::path> cues;
    std::vector<std::filesystem::path> isos;
    std::vector<std::filesystem::path> bins;
    for (std::filesystem::directory_iterator it(rom_dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            if (ec) break;
            continue;
        }
        if (!supported_source_extension(it->path())) continue;
        const auto ext = lower_extension(it->path());
        if (ext == ".cue") cues.push_back(it->path());
        else if (ext == ".iso") isos.push_back(it->path());
        else bins.push_back(it->path());
    }
    if (ec) {
        return Result<std::optional<std::filesystem::path>>::failure(
            ErrorCode::io_error,
            "failed while scanning ROM directory: " + ec.message());
    }

    auto normalize = [](std::filesystem::path path) {
        std::error_code absolute_error;
        auto absolute = std::filesystem::absolute(path, absolute_error);
        return absolute_error ? path.lexically_normal() : absolute.lexically_normal();
    };
    auto sort_paths = [](std::vector<std::filesystem::path>& paths) {
        std::sort(paths.begin(), paths.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.generic_string() < rhs.generic_string();
        });
    };
    sort_paths(cues);
    sort_paths(isos);
    sort_paths(bins);

    if (cues.size() == 1u && isos.empty()) {
        return Result<std::optional<std::filesystem::path>>::success(normalize(cues.front()));
    }

    std::vector<std::filesystem::path> candidates;
    candidates.insert(candidates.end(), cues.begin(), cues.end());
    candidates.insert(candidates.end(), isos.begin(), isos.end());
    if (cues.empty()) candidates.insert(candidates.end(), bins.begin(), bins.end());

    if (candidates.empty()) {
        return Result<std::optional<std::filesystem::path>>::success(std::nullopt);
    }
    if (candidates.size() != 1u) {
        return Result<std::optional<std::filesystem::path>>::failure(
            ErrorCode::invalid_argument,
            "multiple supported PS1 sources were found in Data/ROM; select the JoJo image explicitly");
    }
    return Result<std::optional<std::filesystem::path>>::success(normalize(candidates.front()));
}

Result<void> save_game_source_binding_atomic(
    const std::filesystem::path& path,
    const GameSourceBinding& binding) {
    if (path.empty()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "game source binding path cannot be empty");
    }
    auto valid = validate_binding(binding);
    if (!valid) return valid;

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "failed to create game source binding directory: " + ec.message());
    }

    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed to create temporary game source binding file");
        }
        out << "format=" << kBindingFormat << '\n';
        out << "source_path=" << path_to_utf8(binding.source_path) << '\n';
        out << "source_format=" << binding.source_format << '\n';
        out << "source_size=" << binding.source_size << '\n';
        out << "source_hash_fnv1a64=" << binding.source_hash_fnv1a64 << '\n';
        out << "revision_id=" << binding.revision_id << '\n';
        out.flush();
        if (!out) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed while writing game source binding file");
        }
    }
    return replace_file(temp, path);
}

Result<GameSourceBinding> load_game_source_binding(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<GameSourceBinding>::failure(
            ErrorCode::file_not_found,
            "game source binding file not found: " + path.string());
    }

    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0u) {
            return Result<GameSourceBinding>::failure(
                ErrorCode::invalid_installation,
                "game source binding contains a malformed line");
        }
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1u);
        if (!values.emplace(key, value).second) {
            return Result<GameSourceBinding>::failure(
                ErrorCode::invalid_installation,
                "game source binding contains a duplicate key: " + key);
        }
    }
    if (in.bad()) {
        return Result<GameSourceBinding>::failure(
            ErrorCode::io_error,
            "failed while reading game source binding file");
    }

    const auto require = [&](std::string_view key) -> Result<std::string> {
        const auto it = values.find(std::string(key));
        if (it == values.end() || it->second.empty()) {
            return Result<std::string>::failure(
                ErrorCode::invalid_installation,
                "game source binding is missing required key: " + std::string(key));
        }
        return Result<std::string>::success(it->second);
    };

    auto format = require("format");
    auto source_path = require("source_path");
    auto source_format = require("source_format");
    auto source_size = require("source_size");
    auto source_hash = require("source_hash_fnv1a64");
    auto revision = require("revision_id");
    if (!format) return Result<GameSourceBinding>::failure(format.error, format.detail);
    if (!source_path) return Result<GameSourceBinding>::failure(source_path.error, source_path.detail);
    if (!source_format) return Result<GameSourceBinding>::failure(source_format.error, source_format.detail);
    if (!source_size) return Result<GameSourceBinding>::failure(source_size.error, source_size.detail);
    if (!source_hash) return Result<GameSourceBinding>::failure(source_hash.error, source_hash.detail);
    if (!revision) return Result<GameSourceBinding>::failure(revision.error, revision.detail);
    if (format.value != kBindingFormat) {
        return Result<GameSourceBinding>::failure(
            ErrorCode::invalid_installation,
            "unsupported game source binding format: " + format.value);
    }

    auto parsed_size = parse_u64(source_size.value);
    if (!parsed_size) {
        return Result<GameSourceBinding>::failure(parsed_size.error, parsed_size.detail);
    }

    GameSourceBinding binding{};
    binding.source_path = path_from_utf8(source_path.value);
    binding.source_format = std::move(source_format.value);
    binding.source_size = parsed_size.value;
    binding.source_hash_fnv1a64 = std::move(source_hash.value);
    binding.revision_id = std::move(revision.value);
    auto valid = validate_binding(binding);
    if (!valid) {
        return Result<GameSourceBinding>::failure(valid.error, valid.detail);
    }
    return Result<GameSourceBinding>::success(std::move(binding));
}

Result<Ps1DiscSession> reopen_bound_source(
    const GameSourceBinding& binding,
    const Ps1DiscOpenOptions& options) {
    auto valid = validate_binding(binding);
    if (!valid) {
        return Result<Ps1DiscSession>::failure(valid.error, valid.detail);
    }

    auto opened = Ps1DiscSession::open(binding.source_path, options);
    if (!opened) {
        return Result<Ps1DiscSession>::failure(
            ErrorCode::invalid_installation,
            "bound source changed or no longer validates: " + opened.detail);
    }

    const auto& actual = opened.value.binding();
    if (actual.source_path != binding.source_path ||
        actual.source_format != binding.source_format ||
        actual.source_size != binding.source_size ||
        actual.source_hash_fnv1a64 != binding.source_hash_fnv1a64 ||
        actual.revision_id != binding.revision_id) {
        return Result<Ps1DiscSession>::failure(
            ErrorCode::invalid_installation,
            "bound source changed since it was saved; select or revalidate the JoJo image");
    }

    return opened;
}

} // namespace jojo
