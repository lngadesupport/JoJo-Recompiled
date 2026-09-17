#pragma once

#include "core/disc_media.h"
#include "core/iso9660.h"
#include "core/ps1_exe.h"
#include "core/ps1_system_cnf.h"
#include "core/result.h"
#include "core/revision.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

struct GameSourceBinding {
    std::filesystem::path source_path;
    std::string source_format;
    std::uint64_t source_size{};
    std::string source_hash_fnv1a64;
    std::string revision_id;
};

struct Ps1DiscOpenOptions {
    std::vector<GameRevisionProfile> revision_profiles;
};

class Ps1DiscSession {
public:
    [[nodiscard]] static Result<Ps1DiscSession> open(
        const std::filesystem::path& source,
        const Ps1DiscOpenOptions& options = {});

    [[nodiscard]] const GameSourceBinding& binding() const noexcept;
    [[nodiscard]] const Ps1SystemCnf& system_cnf() const noexcept;
    [[nodiscard]] const Ps1Executable& boot_executable() const noexcept;
    [[nodiscard]] Result<std::vector<std::uint8_t>> read_file(std::string_view iso_path) const;
    [[nodiscard]] Result<std::vector<std::uint8_t>> read_sectors(
        std::uint64_t first_lba,
        std::uint32_t sector_count) const;

private:
    GameSourceBinding binding_{};
    LogicalSectorSource sectors_{};
    Iso9660Image image_{};
    Ps1SystemCnf system_cnf_{};
    Ps1Executable boot_executable_{};
};

} // namespace jojo
