#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct HitTableRecord {
    std::int16_t a{};
    std::int16_t b{};
    std::int16_t c{};
    std::int16_t d{};

    [[nodiscard]] bool empty() const noexcept {
        return a == 0 && b == 0 && c == 0 && d == 0;
    }
};

struct HitTable {
    std::array<HitTableRecord, 512> records{};
    std::uint32_t nonzero_records{};
};

[[nodiscard]] Result<HitTable> parse_hit_table(
    std::span<const std::uint8_t> bytes);

} // namespace jojo::content
