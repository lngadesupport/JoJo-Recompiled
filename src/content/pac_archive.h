#pragma once

#include "core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct PacChunk {
    std::uint32_t type{};
    std::vector<std::uint8_t> bytes;
};

[[nodiscard]] Result<std::vector<PacChunk>> parse_pac_archive(
    std::span<const std::uint8_t> bytes);

} // namespace jojo::content
