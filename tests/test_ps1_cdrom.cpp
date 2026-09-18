#include "core/ps1_cdrom.h"
#include "core/ps1_disc_session.h"
#include "ps1_fixture.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "jojo_ps1_cdrom_contract";
    fs::remove_all(root);
    fs::create_directories(root);

    const auto fixture = test_ps1::make_disc_fixture();
    const auto iso_path = test_ps1::write_cooked_iso(root / "jojo.iso", fixture);
    const auto before = read_all(iso_path);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture));
    auto disc = jojo::Ps1DiscSession::open(iso_path, open_options);
    CHECK(static_cast<bool>(disc));
    if (!disc) return 1;

    jojo::Ps1CdromController cd;
    cd.attach_disc(&disc.value);

    // Index/status register is bounded to two index bits.
    CHECK(cd.write8(0x1F801800u, 0x03u).status == jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801800u).value & 0x03u) == 0x03u);
    CHECK(cd.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);

    // Bank 0 register 3 is the CD request register. Clearing it is valid
    // and is used by the JoJo commercial bootstrap.
    CHECK(cd.write8(0x1F801803u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.request_register() == 0x00u);
    CHECK(cd.write8(0x1F801803u, 0x80u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.request_register() == 0x80u);
    CHECK(cd.write8(0x1F801803u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // HINTMSK reads mirror in banks 0/2 with reserved high bits set.
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801802u, 0x1Fu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x1Fu) == 0x1Fu);
    CHECK(cd.write8(0x1F801800u, 0x02u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x1Fu) == 0x1Fu);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // Init preserves HINTMSK and emits INT3 acknowledge followed by INT2.
    CHECK(cd.write8(0x1F801801u, 0x0Au).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.deferred_response_count() == 1u);
    CHECK(cd.irq_pending());
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x03u);
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    cd.step(33869u);
    CHECK(cd.deferred_response_count() == 0u);
    CHECK(cd.irq_pending());
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x02u);
    const auto init_complete_status = cd.read8(0x1F801801u);
    CHECK(init_complete_status.status == jojo::R3000aBusStatus::ok);
    CHECK((init_complete_status.value & 0x02u) != 0u);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);


    // Getstat produces a bounded response byte.
    CHECK(cd.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.response_bytes_available() == 1u);
    CHECK(cd.read8(0x1F801801u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.response_bytes_available() == 0u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // TOC commands for the supported JoJo data disc: one track,
    // track 01 starts at 00:02 and track 00 returns lead-out.
    CHECK(cd.write8(0x1F801801u, 0x13u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.read8(0x1F801801u).value == 0x02u);
    CHECK(cd.read8(0x1F801801u).value == 0x01u);
    CHECK(cd.read8(0x1F801801u).value == 0x01u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    CHECK(cd.write8(0x1F801802u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801801u, 0x14u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.read8(0x1F801801u).value == 0x02u);
    CHECK(cd.read8(0x1F801801u).value == 0x00u);
    CHECK(cd.read8(0x1F801801u).value == 0x02u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    CHECK(cd.write8(0x1F801802u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801801u, 0x14u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.read8(0x1F801801u).value == 0x02u);
    const auto leadout_minute = cd.read8(0x1F801801u).value;
    const auto leadout_second = cd.read8(0x1F801801u).value;
    CHECK(leadout_minute != 0u || leadout_second > 0x02u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // Setfilter/Setmode/Getparam preserve the PS1 CD parameter contract.
    CHECK(cd.write8(0x1F801802u, 0x03u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801802u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801801u, 0x0Du).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    CHECK(cd.write8(0x1F801802u, 0x80u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801801u, 0x0Eu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    CHECK(cd.write8(0x1F801801u, 0x0Fu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.read8(0x1F801801u).value == 0x02u);
    CHECK(cd.read8(0x1F801801u).value == 0x80u);
    CHECK(cd.read8(0x1F801801u).value == 0x00u);
    CHECK(cd.read8(0x1F801801u).value == 0x03u);
    CHECK(cd.read8(0x1F801801u).value == 0x07u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // Setloc to logical sector 25: absolute MSF is 00:02:25 (150-frame lead-in + 25).
    CHECK(cd.write8(0x1F801802u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801802u, 0x02u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801802u, 0x25u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801801u, 0x02u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.current_lba() == 25u);
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // ReadN acknowledges with INT3, then produces INT1 plus one sector.
    CHECK(cd.write8(0x1F801801u, 0x06u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x03u);
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    cd.step(451584u);
    CHECK(cd.data_bytes_available() == 2048u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x01u);
    const auto read_complete_status = cd.read8(0x1F801801u);
    CHECK(read_complete_status.status == jojo::R3000aBusStatus::ok);
    CHECK((read_complete_status.value & 0x22u) == 0x22u);
    CHECK(cd.write8(0x1F801803u, 0x07u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    std::vector<std::uint32_t> words(512u, 0u);
    CHECK(cd.read_data_words(words) == 512u);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK((words[0] & 0xFFu) == static_cast<std::uint32_t>('A'));
    CHECK(((words[0] >> 8u) & 0xFFu) == static_cast<std::uint32_t>('S'));
    CHECK(((words[0] >> 16u) & 0xFFu) == static_cast<std::uint32_t>('S'));
    CHECK(((words[0] >> 24u) & 0xFFu) == static_cast<std::uint32_t>('E'));

    const auto cd_hash_before = cd.diagnostic_state_hash();
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801802u, 0x1Fu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.diagnostic_state_hash() != cd_hash_before);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // CD host audio matrix uses banked ATV0-ATV3 registers and only
    // changes the active mix when ADPCTL.CHNGATV is written.
    CHECK(cd.write8(0x1F801800u, 0x02u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801802u, 0x70u).status ==
          jojo::R3000aBusStatus::ok); // ATV0 L->L
    CHECK(cd.write8(0x1F801803u, 0x10u).status ==
          jojo::R3000aBusStatus::ok); // ATV1 L->R
    CHECK(cd.write8(0x1F801800u, 0x03u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801801u, 0x60u).status ==
          jojo::R3000aBusStatus::ok); // ATV2 R->R
    CHECK(cd.write8(0x1F801802u, 0x20u).status ==
          jojo::R3000aBusStatus::ok); // ATV3 R->L
    const std::array<std::uint8_t, 4> expected_pending_matrix{
        0x70u, 0x10u, 0x60u, 0x20u};
    const std::array<std::uint8_t, 4> expected_default_matrix{
        0x80u, 0x00u, 0x80u, 0x00u};
    CHECK(cd.pending_audio_matrix() == expected_pending_matrix);
    CHECK(cd.active_audio_matrix() == expected_default_matrix);

    CHECK(cd.write8(0x1F801803u, 0x21u).status ==
          jojo::R3000aBusStatus::ok); // ADPMUTE + CHNGATV
    CHECK(cd.adpcm_muted());
    CHECK(cd.active_audio_matrix() == cd.pending_audio_matrix());
    CHECK(cd.write8(0x1F801803u, 0x20u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(!cd.adpcm_muted());
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // Mute/Demute are single-phase INT3 commands and preserve
    // deterministic CD audio state.
    CHECK(cd.write8(0x1F801801u, 0x0Bu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.muted());
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x03u);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    CHECK(cd.write8(0x1F801801u, 0x0Cu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(!cd.muted());
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x03u);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // Unknown commands remain explicit; they are never guessed successful.
    const auto commands_before_unknown = cd.command_count();
    CHECK(cd.write8(0x1F801801u, 0x7Fu).status == jojo::R3000aBusStatus::unsupported);
    CHECK(cd.command_count() == commands_before_unknown + 1u);
    CHECK(!cd.recent_commands().empty());
    if (!cd.recent_commands().empty()) {
        CHECK(cd.recent_commands().back().command == 0x7Fu);
        CHECK(cd.recent_commands().back().index == 0u);
    }
    CHECK(cd.last_unsupported_command().has_value());
    if (cd.last_unsupported_command()) CHECK(*cd.last_unsupported_command() == 0x7Fu);

    // The source image remains byte-for-byte unchanged.
    const auto after = read_all(iso_path);
    CHECK(before == after);

    // No attached media means data commands fail explicitly.
    jojo::Ps1CdromController detached;
    CHECK(detached.write8(0x1F801801u, 0x06u).status == jojo::R3000aBusStatus::unsupported);

    fs::remove_all(root);
    return failures ? 1 : 0;
}
