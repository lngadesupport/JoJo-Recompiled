#pragma once

#include "core/iso9660.h"
#include "core/result.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

struct Ps1ExeMetadata {
    std::uint32_t entry_pc{};
    std::uint32_t initial_gp{};
    std::uint32_t text_load_address{};
    std::uint32_t text_size{};
    std::uint32_t stack_base{};
    std::uint32_t stack_size{};
    std::string fnv1a64_hex;
};

struct Ps1Executable {
    Ps1ExeMetadata metadata;
    std::vector<std::uint8_t> file_bytes;
};

[[nodiscard]] Result<Ps1Executable> parse_ps1_executable(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<Ps1Executable> read_ps1_executable(
    const Iso9660Image& image,
    std::string_view iso_path);

}
