#pragma once
#include "core/result.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace jojo {

struct DiscFingerprint {
    std::uint64_t size_bytes{};
    std::uint64_t fnv1a64{};
    std::string hash_hex;
    std::string format;
};

[[nodiscard]] bool supported_disc_extension(std::string_view filename);
[[nodiscard]] Result<DiscFingerprint> fingerprint_disc_image(const std::filesystem::path& path);

}
