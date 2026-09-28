#include "content/fighter_render_context.h"

#include <set>

namespace jojo::content {
namespace {

std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8u) |
        (static_cast<std::uint32_t>(p[2]) << 16u) |
        (static_cast<std::uint32_t>(p[3]) << 24u);
}

std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8u));
}

std::uint8_t orientation_from_flags(
    std::uint8_t flip_a,
    std::uint8_t flip_b) noexcept {
    return static_cast<std::uint8_t>(
        ((((flip_a & 1u) ^ (flip_b & 1u)) * 2u) +
         ((flip_b & 2u) >> 1u)) &
        3u);
}

bool valid_clut_row(std::uint16_t row) noexcept {
    return row >= 0x01e0u && row < 0x01f8u;
}

} // namespace

std::vector<FighterAnimationScriptCandidate>
scan_animation_script_candidates(
    std::span<const std::uint8_t> overlay,
    std::uint32_t frame_count) {
    constexpr std::size_t kMinimumRecords = 3u;
    constexpr std::size_t kMaximumRecords = 256u;

    std::vector<FighterAnimationScriptCandidate> result;
    if (frame_count == 0u || overlay.size() < 4u) {
        return result;
    }

    std::set<std::uint32_t> seen_targets;
    const auto overlay_end =
        static_cast<std::uint64_t>(
            fighter_overlay_primary_base) +
        overlay.size();

    for (std::size_t pointer_offset = 0u;
         pointer_offset + 4u <= overlay.size();
         pointer_offset += 4u) {
        const auto pointer =
            le32(overlay.data() + pointer_offset);
        if (pointer < fighter_overlay_primary_base ||
            static_cast<std::uint64_t>(pointer) >=
                overlay_end) {
            continue;
        }

        const auto target =
            pointer - fighter_overlay_primary_base;
        if (!seen_targets.insert(target).second) {
            continue;
        }

        std::vector<FighterAnimationScriptRecord> records;
        std::size_t cursor = target;
        for (std::size_t visited = 0u;
             visited < kMaximumRecords &&
             cursor + 4u <= overlay.size();
             ++visited) {
            const auto command = overlay[cursor];
            const auto length =
                static_cast<std::uint8_t>(
                    command & 0x2Fu);
            if (length < 4u ||
                cursor + length > overlay.size()) {
                break;
            }

            const auto frame =
                static_cast<std::uint16_t>(
                    le16(overlay.data() + cursor + 2u) &
                    0x0FFFu);
            if (frame < frame_count) {
                records.push_back({
                    static_cast<std::uint32_t>(cursor),
                    command,
                    length,
                    frame,
                });
            }

            cursor += length;
        }

        if (records.size() < kMinimumRecords) {
            continue;
        }

        FighterAnimationScriptCandidate candidate{};
        candidate.source_pointer_offset =
            static_cast<std::uint32_t>(pointer_offset);
        candidate.target_offset = target;
        candidate.confidence_score =
            static_cast<std::uint32_t>(
                20u +
                std::min<std::size_t>(
                    records.size(), 20u));
        candidate.records = std::move(records);
        result.push_back(std::move(candidate));
    }

    return result;
}

std::vector<FighterRenderContextCandidate>
scan_compact_render_context_candidates(
    std::span<const std::uint8_t> overlay,
    std::uint32_t frame_count,
    std::uint32_t side) {
    constexpr std::size_t kRecordBytes = 0x28u;
    std::vector<FighterRenderContextCandidate> result;
    if (side > 1u || frame_count == 0u ||
        overlay.size() < kRecordBytes) {
        return result;
    }

    for (std::size_t offset = 0u;
         offset + kRecordBytes <= overlay.size();
         ++offset) {
        // This is deliberately a conservative candidate scanner rather than
        // a declaration that every matching 0x28-byte window is a typed
        // gameplay record. The two leading non-zero bytes are part of the
        // retail compact-record evidence and discard large zero regions.
        if (overlay[offset] == 0u ||
            overlay[offset + 1u] == 0u) {
            continue;
        }

        const auto frame =
            static_cast<std::uint16_t>(
                le16(overlay.data() + offset + 0x0eu) &
                0x0FFFu);
        if (frame >= frame_count) {
            continue;
        }

        const auto clut_row =
            le16(overlay.data() + offset + 0x0cu);
        if (!valid_clut_row(clut_row)) {
            continue;
        }

        const auto asset_slot =
            overlay[offset + 0x1du];
        if (asset_slot != side &&
            asset_slot != side + 2u) {
            continue;
        }

        FighterRenderContextCandidate candidate{};
        candidate.source_offset =
            static_cast<std::uint32_t>(offset);
        candidate.frame_index = frame;
        candidate.clut_mode =
            static_cast<std::int8_t>(
                overlay[offset + 0x06u]);
        candidate.clut_base =
            overlay[offset + 0x07u];
        candidate.clut_row_base = clut_row;
        candidate.asset_slot = asset_slot;
        candidate.flip_a =
            overlay[offset + 0x1eu];
        candidate.flip_b =
            overlay[offset + 0x1fu];
        candidate.orientation =
            orientation_from_flags(
                candidate.flip_a,
                candidate.flip_b);

        // Score is diagnostic only. It gives exact-side records a modest
        // preference while keeping all matching candidates available for
        // visual validation.
        candidate.confidence_score =
            30u +
            (asset_slot == side ? 4u : 2u) +
            2u;
        result.push_back(candidate);
    }

    return result;
}

} // namespace jojo::content
