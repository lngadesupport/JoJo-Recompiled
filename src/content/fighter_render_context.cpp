#include "content/fighter_render_context.h"

#include <algorithm>
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

            const auto raw_frame =
                le16(overlay.data() + cursor + 2u);
            const auto frame =
                static_cast<std::uint16_t>(
                    raw_frame & 0x0FFFu);
            if (frame < frame_count) {
                FighterAnimationScriptRecord record{};
                record.source_offset =
                    static_cast<std::uint32_t>(cursor);
                record.command = command;
                record.record_length = length;
                if (length >= 2u) {
                    record.operand0 = overlay[cursor + 1u];
                    record.operand0_flags =
                        static_cast<std::uint8_t>(
                            record.operand0 & 0x80u);
                    if (command == 0x46u && length == 6u) {
                        record.duration_candidate_ticks =
                            static_cast<std::uint8_t>(
                                record.operand0 & 0x7Fu);
                    }
                }
                record.raw_frame_word = raw_frame;
                record.frame_index = frame;
                record.frame_flags =
                    static_cast<std::uint16_t>(
                        raw_frame & 0xF000u);
                if (length >= 6u) {
                    record.parameter_word =
                        le16(overlay.data() + cursor + 4u);
                }
                records.push_back(record);
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

        std::set<std::uint16_t> unique_frames;
        bool all_tkc_signature = true;
        for (const auto& record : records) {
            unique_frames.insert(record.frame_index);
            if (record.command == 0x46u &&
                record.record_length == 6u) {
                ++candidate.command_46_count;
            }
            if (record.command == 0x8au &&
                record.record_length == 10u) {
                ++candidate.command_8a_count;
            } else {
                all_tkc_signature = false;
            }
        }
        candidate.unique_frame_count =
            static_cast<std::uint32_t>(
                unique_frames.size());

        const auto sequence_threshold =
            (records.size() * 3u + 3u) / 4u;
        if (all_tkc_signature) {
            candidate.classification =
                FighterAnimationCandidateClass::tkc_like;
        } else if (
            candidate.command_46_count >= sequence_threshold &&
            candidate.unique_frame_count > 1u) {
            candidate.classification =
                FighterAnimationCandidateClass::frame_sequence_like;
        }

        candidate.confidence_score =
            static_cast<std::uint32_t>(
                20u +
                std::min<std::size_t>(
                    records.size(), 20u));
        if (candidate.classification ==
            FighterAnimationCandidateClass::frame_sequence_like) {
            candidate.confidence_score += 20u;
        } else if (candidate.classification ==
            FighterAnimationCandidateClass::tkc_like) {
            candidate.confidence_score = 0u;
        }

        candidate.records = std::move(records);
        result.push_back(std::move(candidate));
    }

    // Multiple overlay pointers can legitimately target interior records of
    // the same 0x46 frame-sequence table. Preserve every candidate for audit,
    // but mark only maximal non-contained sequences as canonical roots.
    for (auto& candidate : result) {
        if (candidate.classification !=
            FighterAnimationCandidateClass::frame_sequence_like ||
            candidate.records.empty()) {
            continue;
        }
        candidate.canonical_sequence_root = true;
        const auto start = candidate.target_offset;
        const auto& last = candidate.records.back();
        const auto end =
            static_cast<std::uint64_t>(last.source_offset) +
            last.record_length;

        for (const auto& other : result) {
            if (&other == &candidate ||
                other.classification !=
                    FighterAnimationCandidateClass::frame_sequence_like ||
                other.records.empty()) {
                continue;
            }
            const auto other_start = other.target_offset;
            const auto& other_last = other.records.back();
            const auto other_end =
                static_cast<std::uint64_t>(other_last.source_offset) +
                other_last.record_length;

            if (other_start <= start &&
                end <= other_end &&
                (other_start < start || end < other_end)) {
                candidate.canonical_sequence_root = false;
                break;
            }
        }
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
