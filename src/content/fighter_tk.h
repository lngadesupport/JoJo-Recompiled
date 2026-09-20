#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>
#include <span>

namespace jojo::content {

constexpr std::uint32_t fighter_tkc_load_base = 0x8010D800u;
constexpr std::size_t fighter_tk_slot_count = 27u;

struct FighterTkSlot {
    bool tkc_null{};
    std::uint32_t tkc_offset{};
    std::uint32_t tkd_value{};
};

struct FighterTkRoots {
    std::array<FighterTkSlot, fighter_tk_slot_count> slots{};
    std::uint32_t tkc_size{};
    std::uint32_t tkd_size{};
};

[[nodiscard]] Result<FighterTkRoots> parse_fighter_tk_roots(
    std::span<const std::uint8_t> tkc,
    std::span<const std::uint8_t> tkd);

} // namespace jojo::content
