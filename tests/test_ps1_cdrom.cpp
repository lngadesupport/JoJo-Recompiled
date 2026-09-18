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

    // Getstat produces a bounded response byte.
    CHECK(cd.write8(0x1F801801u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.response_bytes_available() == 1u);
    CHECK(cd.read8(0x1F801801u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.response_bytes_available() == 0u);

    // Setloc to logical sector 25: absolute MSF is 00:02:25 (150-frame lead-in + 25).
    CHECK(cd.write8(0x1F801802u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801802u, 0x02u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801802u, 0x25u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801801u, 0x02u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.current_lba() == 25u);

    // ReadN fetches one logical sector directly from the live session.
    CHECK(cd.write8(0x1F801801u, 0x06u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.data_bytes_available() == 2048u);
    std::vector<std::uint32_t> words(512u, 0u);
    CHECK(cd.read_data_words(words) == 512u);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK((words[0] & 0xFFu) == static_cast<std::uint32_t>('A'));
    CHECK(((words[0] >> 8u) & 0xFFu) == static_cast<std::uint32_t>('S'));
    CHECK(((words[0] >> 16u) & 0xFFu) == static_cast<std::uint32_t>('S'));
    CHECK(((words[0] >> 24u) & 0xFFu) == static_cast<std::uint32_t>('E'));

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
