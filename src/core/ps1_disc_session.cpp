#include "core/ps1_disc_session.h"

#include "core/disc_image.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace jojo {
namespace {

std::string selected_source_format(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    return ext;
}

Result<GameRevisionMatch> identify_session_revision(
    const Iso9660Image& image,
    const DiscFingerprint& backing_fingerprint,
    const Ps1DiscOpenOptions& options) {
    auto revision = identify_game_revision(image, options.revision_profiles);
    if (revision || revision.error != ErrorCode::unknown_revision) return revision;

    auto observed = identify_observed_disc_revision(
        backing_fingerprint.format,
        backing_fingerprint.size_bytes,
        backing_fingerprint.hash_hex);
    if (observed) return observed;

    return Result<GameRevisionMatch>::failure(
        ErrorCode::unknown_revision,
        revision.detail + "; whole-image fingerprint is also unsupported");
}

} // namespace

Result<Ps1DiscSession> Ps1DiscSession::open(
    const std::filesystem::path& source,
    const Ps1DiscOpenOptions& options) {
    if (source.empty()) {
        return Result<Ps1DiscSession>::failure(
            ErrorCode::invalid_argument,
            "PS1 source path cannot be empty");
    }

    auto sectors = open_logical_sector_source(source);
    if (!sectors) return Result<Ps1DiscSession>::failure(sectors.error, sectors.detail);

    auto image = open_iso9660(sectors.value);
    if (!image) return Result<Ps1DiscSession>::failure(image.error, image.detail);

    auto fingerprint = fingerprint_disc_image(sectors.value.file_path);
    if (!fingerprint) {
        return Result<Ps1DiscSession>::failure(fingerprint.error, fingerprint.detail);
    }

    auto revision = identify_session_revision(image.value, fingerprint.value, options);
    if (!revision) return Result<Ps1DiscSession>::failure(revision.error, revision.detail);

    auto system = read_ps1_system_cnf(image.value);
    if (!system) return Result<Ps1DiscSession>::failure(system.error, system.detail);

    auto executable = read_ps1_executable(image.value, system.value.boot_iso_path);
    if (!executable) {
        return Result<Ps1DiscSession>::failure(executable.error, executable.detail);
    }

    std::error_code ec;
    auto absolute_source = std::filesystem::absolute(source, ec);
    if (ec) {
        return Result<Ps1DiscSession>::failure(
            ErrorCode::io_error,
            "failed to resolve PS1 source path: " + ec.message());
    }

    Ps1DiscSession session{};
    session.binding_.source_path = absolute_source.lexically_normal();
    session.binding_.source_format = selected_source_format(source);
    session.binding_.source_size = fingerprint.value.size_bytes;
    session.binding_.source_hash_fnv1a64 = fingerprint.value.hash_hex;
    session.binding_.revision_id = revision.value.revision_id;
    session.sectors_ = std::move(sectors.value);
    session.image_ = std::move(image.value);
    session.system_cnf_ = std::move(system.value);
    session.boot_executable_ = std::move(executable.value);

    return Result<Ps1DiscSession>::success(std::move(session));
}

const GameSourceBinding& Ps1DiscSession::binding() const noexcept { return binding_; }
const Ps1SystemCnf& Ps1DiscSession::system_cnf() const noexcept { return system_cnf_; }
const Ps1Executable& Ps1DiscSession::boot_executable() const noexcept { return boot_executable_; }

Result<std::vector<std::uint8_t>> Ps1DiscSession::read_file(std::string_view iso_path) const {
    return read_iso9660_file(image_, iso_path);
}

Result<std::vector<std::uint8_t>> Ps1DiscSession::read_sectors(
    std::uint64_t first_lba,
    std::uint32_t sector_count) const {
    return read_logical_sectors(sectors_, first_lba, sector_count);
}

Result<std::vector<std::uint8_t>> Ps1DiscSession::read_cdrom_sectors(
    std::uint64_t first_lba,
    std::uint32_t sector_count,
    bool whole_sector) const {
    return jojo::read_cdrom_sectors(
        sectors_, first_lba, sector_count, whole_sector);
}

std::uint64_t Ps1DiscSession::logical_sector_count() const noexcept {
    return sectors_.logical_sector_count;
}

} // namespace jojo
