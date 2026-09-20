#include "content/fighter_overlay.h"

#include <algorithm>
#include <limits>

namespace jojo::content {
namespace {

std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8u) |
        (static_cast<std::uint32_t>(p[2]) << 16u) |
        (static_cast<std::uint32_t>(p[3]) << 24u);
}

void put32(
    std::uint8_t* p,
    std::uint32_t value) noexcept {
    p[0] = static_cast<std::uint8_t>(value);
    p[1] = static_cast<std::uint8_t>(value >> 8u);
    p[2] = static_cast<std::uint8_t>(value >> 16u);
    p[3] = static_cast<std::uint8_t>(value >> 24u);
}

} // namespace

Result<FighterOverlayPair> analyze_fighter_overlay_pair(
    std::span<const std::uint8_t> primary,
    std::span<const std::uint8_t> mirror) {
    if (primary.size() != mirror.size()) {
        return Result<FighterOverlayPair>::failure(
            ErrorCode::invalid_installation,
            "PL/PLX overlay pair sizes differ");
    }
    if (primary.size() >
        std::numeric_limits<std::uint32_t>::max()) {
        return Result<FighterOverlayPair>::failure(
            ErrorCode::unsupported_format,
            "fighter overlay exceeds 32-bit retail address space");
    }

    FighterOverlayPair result{};
    result.size_bytes =
        static_cast<std::uint32_t>(primary.size());

    std::vector<std::uint8_t> normalized(
        mirror.begin(), mirror.end());

    const auto end_address =
        static_cast<std::uint64_t>(
            fighter_overlay_primary_base) +
        primary.size();

    for (std::size_t offset = 0u;
         offset + 4u <= primary.size();
         offset += 4u) {
        const auto original =
            le32(primary.data() + offset);
        const auto mirrored =
            le32(mirror.data() + offset);

        if (original <
                fighter_overlay_primary_base ||
            static_cast<std::uint64_t>(original) >
                end_address) {
            continue;
        }

        const auto expected =
            static_cast<std::uint64_t>(original) +
            fighter_overlay_mirror_delta;
        if (expected >
                std::numeric_limits<std::uint32_t>::max() ||
            mirrored !=
                static_cast<std::uint32_t>(expected)) {
            continue;
        }

        result.relocations.push_back({
            static_cast<std::uint32_t>(offset),
            original - fighter_overlay_primary_base,
        });
        put32(normalized.data() + offset, original);
    }

    result.relocation_count =
        static_cast<std::uint32_t>(
            result.relocations.size());

    std::uint32_t residual = 0u;
    for (std::size_t i = 0u;
         i < primary.size();
         ++i) {
        if (primary[i] != normalized[i]) {
            ++residual;
        }
    }
    result.residual_difference_bytes = residual;

    if (result.relocations.empty()) {
        return Result<FighterOverlayPair>::failure(
            ErrorCode::invalid_installation,
            "PL/PLX pair contains no verified retail relocations");
    }

    return Result<FighterOverlayPair>::success(
        std::move(result));
}

} // namespace jojo::content
