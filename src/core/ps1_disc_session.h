#pragma once

#include "core/disc_media.h"
#include "core/game_source_binding.h"
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
    [[nodiscard]] std::uint64_t logical_sector_count() const noexcept;

private:
    GameSourceBinding binding_{};
    LogicalSectorSource sectors_{};
    Iso9660Image image_{};
    Ps1SystemCnf system_cnf_{};
    Ps1Executable boot_executable_{};
};

} // namespace jojo
