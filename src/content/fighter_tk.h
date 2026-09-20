#pragma once

#include "core/result.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

constexpr std::uint32_t fighter_tkc_load_base = 0x8010D800u;
constexpr std::size_t fighter_tk_slot_count = 26u;

struct FighterTkcRecord {
    std::uint32_t source_offset{};
    std::array<std::uint16_t, 5> fields{};
};

struct FighterTkdRecord {
    std::uint32_t source_offset{};
    std::array<std::int16_t, 4> fields{};
};

struct FighterTkSlot {
    std::uint32_t tkc_offset{};
    std::vector<FighterTkcRecord> tkc_records;
    std::uint32_t tkd_offset{};
    std::vector<FighterTkdRecord> tkd_records;
};

struct FighterTkRoots {
    std::array<FighterTkSlot, fighter_tk_slot_count> slots{};
    std::uint32_t tkc_size{};
    std::uint32_t tkc_end_offset{};
    std::uint32_t tkd_size{};
    std::uint32_t tkd_block_size{};
};

[[nodiscard]] Result<FighterTkRoots> parse_fighter_tk_roots(
    std::span<const std::uint8_t> tkc,
    std::span<const std::uint8_t> tkd);

} // namespace jojo::content
