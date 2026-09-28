#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct HitTableRecord {
    std::int16_t x_offset{};
    std::int16_t width{};
    std::int16_t y_offset{};
    std::int16_t height{};

    [[nodiscard]] bool empty() const noexcept {
        return x_offset == 0 &&
            width == 0 &&
            y_offset == 0 &&
            height == 0;
    }
};

struct HitTable {
    std::array<HitTableRecord, 512> records{};
    std::uint32_t nonzero_records{};
};

[[nodiscard]] Result<HitTable> parse_hit_table(
    std::span<const std::uint8_t> bytes);

} // namespace jojo::content
