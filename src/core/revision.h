#pragma once
#include "core/iso9660.h"
#include "core/result.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace jojo {
struct RevisionFileSignature { std::string path; std::uint64_t size_bytes{}; std::uint64_t fnv1a64{}; };
struct GameRevisionProfile { std::string revision_id; std::vector<RevisionFileSignature> files; };
struct GameRevisionMatch { std::string revision_id; };
[[nodiscard]] Result<GameRevisionMatch> identify_game_revision(const Iso9660Image& image,const std::vector<GameRevisionProfile>& profiles);
[[nodiscard]] Result<GameRevisionMatch> identify_observed_disc_revision(std::string_view source_format,std::uint64_t source_size,std::string_view hash_hex);
}
