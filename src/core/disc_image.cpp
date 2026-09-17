#include "core/disc_image.h"
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace jojo {
namespace {
std::string lower_ext(std::string_view filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos) return {};
    std::string ext(filename.substr(dot + 1));
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}
}

bool supported_disc_extension(std::string_view filename) {
    const auto ext = lower_ext(filename);
    return ext == "iso" || ext == "bin" || ext == "cue";
}

Result<DiscFingerprint> fingerprint_disc_image(const std::filesystem::path& path) {
    if (!supported_disc_extension(path.filename().string())) {
        return Result<DiscFingerprint>::failure(ErrorCode::unsupported_format,
                                                "supported PS1 formats: .iso, .bin, .cue");
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return Result<DiscFingerprint>::failure(ErrorCode::file_not_found,
                                                "disc image not found: " + path.string());
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<DiscFingerprint>::failure(ErrorCode::io_error,
                                                "cannot open disc image: " + path.string());
    }

    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    std::uint64_t size = 0;
    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= prime;
        }
        size += static_cast<std::uint64_t>(count);
    }
    if (!in.eof()) {
        return Result<DiscFingerprint>::failure(ErrorCode::io_error, "error while reading disc image");
    }

    std::ostringstream hex;
    hex << std::hex << std::setfill('0') << std::setw(16) << hash;
    return Result<DiscFingerprint>::success(DiscFingerprint{size, hash, hex.str(), lower_ext(path.filename().string())});
}

}
