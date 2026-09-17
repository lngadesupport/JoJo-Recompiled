#include "core/revision.h"

#include <array>
#include <string_view>
#include <utility>

namespace jojo {
namespace {
struct ObservedDiscRevision {
    std::string_view source_format;
    std::uint64_t source_size;
    std::string_view hash_hex;
    std::string_view revision_id;
};
constexpr std::array<ObservedDiscRevision, 1> observed_disc_revisions{{
    {"bin", 666806112ull, "b8b5dbf79cdb9fcf", "jojo-usa-observed-b8b5dbf79cdb9fcf"},
}};
std::uint64_t fnv1a64(const std::vector<std::uint8_t>& data) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : data) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
} // namespace

Result<GameRevisionMatch> identify_game_revision(
    const Iso9660Image& image,
    const std::vector<GameRevisionProfile>& profiles) {
    if (profiles.empty()) {
        return Result<GameRevisionMatch>::failure(
            ErrorCode::unknown_revision,
            "no verified revision profiles are registered yet for this game");
    }
    std::string first_mismatch;
    for (const auto& profile : profiles) {
        if (profile.revision_id.empty() || profile.files.empty()) continue;
        bool matches = true;
        for (const auto& expected : profile.files) {
            auto file = read_iso9660_file(image, expected.path);
            if (!file) {
                if (file.error == ErrorCode::file_not_found) {
                    if (first_mismatch.empty()) first_mismatch = "profile '" + profile.revision_id + "' is missing " + expected.path;
                    matches = false;
                    break;
                }
                return Result<GameRevisionMatch>::failure(file.error, file.detail);
            }
            if (file.value.size() != expected.size_bytes || fnv1a64(file.value) != expected.fnv1a64) {
                if (first_mismatch.empty()) first_mismatch = "profile '" + profile.revision_id + "' fingerprint mismatch for " + expected.path;
                matches = false;
                break;
            }
        }
        if (matches) return Result<GameRevisionMatch>::success(GameRevisionMatch{profile.revision_id});
    }
    std::string detail = "disc image does not match any supported game revision profile";
    if (!first_mismatch.empty()) detail += "; " + first_mismatch;
    return Result<GameRevisionMatch>::failure(ErrorCode::unknown_revision, std::move(detail));
}

Result<GameRevisionMatch> identify_observed_disc_revision(
    std::string_view source_format,
    std::uint64_t source_size,
    std::string_view hash_hex) {
    for (const auto& observed : observed_disc_revisions) {
        if (observed.source_format == source_format && observed.source_size == source_size && observed.hash_hex == hash_hex) {
            return Result<GameRevisionMatch>::success(GameRevisionMatch{std::string(observed.revision_id)});
        }
    }
    return Result<GameRevisionMatch>::failure(
        ErrorCode::unknown_revision,
        "disc fingerprint does not match any observed game revision");
}

} // namespace jojo
