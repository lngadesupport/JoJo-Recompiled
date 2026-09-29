#pragma once

#include "core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

constexpr std::uint32_t fighter_overlay_primary_base = 0x800DF000u;
constexpr std::uint32_t fighter_overlay_mirror_delta = 0x00015800u;

struct FighterOverlayRelocation {
    std::uint32_t field_offset{};
    std::uint32_t target_offset{};
};

struct FighterOverlayPair {
    std::uint32_t size_bytes{};
    std::uint32_t relocation_count{};
    std::uint32_t residual_difference_bytes{};
    std::vector<FighterOverlayRelocation> relocations;
};

[[nodiscard]] Result<FighterOverlayPair> analyze_fighter_overlay_pair(
    std::span<const std::uint8_t> primary,
    std::span<const std::uint8_t> mirror);

} // namespace jojo::content
