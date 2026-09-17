#pragma once

#include "core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace jojo {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256Hasher {
public:
    Sha256Hasher() = default;

    [[nodiscard]] Result<void> update(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] Result<Sha256Digest> finalize() noexcept;

private:
    void transform(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_{};
    std::uint64_t total_bytes_{};
    bool finalized_{};
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] Result<Sha256Digest> sha256_file(const std::filesystem::path& path);
[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);

}
