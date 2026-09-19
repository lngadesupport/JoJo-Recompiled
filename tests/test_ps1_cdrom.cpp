#include "core/ps1_cdrom.h"
#include "core/ps1_disc_session.h"
#include "ps1_fixture.h"

#include <algorithm>
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
    // GetTD returns only MM:SS, so the synthetic 28-sector lead-out
    // (00:02:28 absolute) is represented as 00:02.
    CHECK(leadout_minute == 0x00u);
    CHECK(leadout_second == 0x02u);
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

    // ReadN acknowledges with INT3, then streams INT1 + sector data until
    // Pause/Stop. Setmode=80h above selects the real PS1 double-speed cadence.
    CHECK(cd.write8(0x1F801801u, 0x06u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x03u);
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x07u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    cd.step(451584u);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x01u);
    const auto read_complete_status = cd.read8(0x1F801801u);
    CHECK(read_complete_status.status == jojo::R3000aBusStatus::ok);
    CHECK((read_complete_status.value & 0x22u) == 0x22u);
    CHECK(cd.write8(0x1F801803u, 0x07u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x80u).status == jojo::R3000aBusStatus::ok);
    CHECK(cd.data_bytes_available() == 2048u);
    std::vector<std::uint32_t> words(512u, 0u);
    CHECK(cd.read_data_words(words) == 512u);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK((words[0] & 0xFFu) == static_cast<std::uint32_t>('A'));
    CHECK(((words[0] >> 8u) & 0xFFu) == static_cast<std::uint32_t>('S'));
    CHECK(((words[0] >> 16u) & 0xFFu) == static_cast<std::uint32_t>('S'));
    CHECK(((words[0] >> 24u) & 0xFFu) == static_cast<std::uint32_t>('E'));
    CHECK(cd.current_lba() == 26u);
    CHECK(cd.write8(0x1F801803u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // A second sector must arrive automatically without another ReadN.
    // In mode 80h this is one 150 Hz sector interval (225792 CPU cycles).
    cd.step(225792u);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x01u);
    const auto second_read_status = cd.read8(0x1F801801u);
    CHECK(second_read_status.status == jojo::R3000aBusStatus::ok);
    CHECK((second_read_status.value & 0x22u) == 0x22u);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801803u, 0x80u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.data_bytes_available() == 2048u);
    std::fill(words.begin(), words.end(), 0xFFFFFFFFu);
    CHECK(cd.read_data_words(words) == 512u);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK(cd.current_lba() == 27u);

    // Pause terminates the stream. Once its INT2 completion is acknowledged,
    // no further sector may appear even if multiple sector intervals elapse.
    CHECK(cd.write8(0x1F801801u, 0x09u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x03u);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    cd.step(33869u);
    CHECK(cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((cd.read8(0x1F801803u).value & 0x07u) == 0x02u);
    CHECK(cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    cd.step(451584u * 2u);
    CHECK(cd.data_bytes_available() == 0u);
    CHECK(cd.current_lba() == 27u);

    // The physical drive must continue advancing while the ReadN command
    // acknowledge/INT3 is still pending. Buffer several sectors without host
    // acknowledgement, then expose them through serialized INT1 delivery.
    jojo::Ps1CdromController buffered_cd;
    buffered_cd.attach_disc(&disc.value);
    CHECK(buffered_cd.write8(0x1F801802u, 0x80u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801801u, 0x0Eu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801802u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801802u, 0x02u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801802u, 0x20u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801801u, 0x02u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    CHECK(buffered_cd.write8(0x1F801801u, 0x06u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.current_lba() == 20u);
    buffered_cd.step(225792u * 3u);
    CHECK(buffered_cd.current_lba() == 23u);
    CHECK(buffered_cd.data_bytes_available() == 0u);

    // Ack ReadN's INT3; the first already-buffered sector is then delivered
    // immediately as INT1 without waiting another sector interval.
    CHECK(buffered_cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    buffered_cd.step(0u);
    CHECK(buffered_cd.current_lba() == 23u);
    CHECK(buffered_cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((buffered_cd.read8(0x1F801803u).value & 0x07u) == 0x01u);
    CHECK(buffered_cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801803u, 0x80u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.data_bytes_available() == 2048u);
    std::vector<std::uint32_t> buffered_words(512u, 0u);
    CHECK(buffered_cd.read_data_words(buffered_words) == 512u);
    CHECK(buffered_cd.write8(0x1F801803u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // The second queued sector is likewise ready for INT1 immediately.
    buffered_cd.step(0u);
    CHECK(buffered_cd.write8(0x1F801800u, 0x01u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((buffered_cd.read8(0x1F801803u).value & 0x07u) == 0x01u);
    CHECK(buffered_cd.read8(0x1F801801u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801803u, 0x07u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(buffered_cd.write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);

    // Mode bit 5 selects the PS1 924h-byte transfer window:
    // bytes 12..2351 of a raw 2352-byte sector. This is the mode used by
    // JoJo's P/MOJI.PAC loader (Setmode=A0h).
    const auto raw_iso_path = root / "jojo-raw-source.iso";
    const auto raw_bin_path = root / "jojo-mode2.bin";
    test_ps1::write_mode2_bin(
        raw_iso_path, raw_bin_path, fixture);
    auto raw_disc = jojo::Ps1DiscSession::open(
        raw_bin_path, open_options);
    CHECK(static_cast<bool>(raw_disc));
    if (raw_disc) {
        jojo::Ps1CdromController raw_cd;
        raw_cd.attach_disc(&raw_disc.value);

        CHECK(raw_cd.write8(0x1F801802u, 0xA0u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801801u, 0x0Eu).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.read8(0x1F801801u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x01u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801803u, 0x07u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x00u).status ==
              jojo::R3000aBusStatus::ok);

        CHECK(raw_cd.write8(0x1F801802u, 0x00u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801802u, 0x02u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801802u, 0x25u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801801u, 0x02u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.read8(0x1F801801u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x01u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801803u, 0x07u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x00u).status ==
              jojo::R3000aBusStatus::ok);

        CHECK(raw_cd.write8(0x1F801801u, 0x06u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.read8(0x1F801801u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x01u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801803u, 0x07u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x00u).status ==
              jojo::R3000aBusStatus::ok);

        // BFRD may be armed before the incoming INT1/datablock. The real
        // controller latches that request and asserts DRQSTS when data lands.
        CHECK(raw_cd.write8(0x1F801803u, 0x80u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.data_bytes_available() == 0u);
        raw_cd.step(225792u);
        CHECK(raw_cd.data_bytes_available() == 2340u);
        CHECK(raw_cd.write8(0x1F801800u, 0x01u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK((raw_cd.read8(0x1F801803u).value & 0x07u) == 0x01u);
        CHECK(raw_cd.read8(0x1F801801u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801803u, 0x07u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x00u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.data_bytes_available() == 2340u);

        std::vector<std::uint32_t> raw_words(512u, 0u);
        CHECK(raw_cd.read_data_words(raw_words) == 512u);
        CHECK(raw_cd.data_bytes_available() == 292u);
        CHECK((raw_words[0] >> 24u) == 0x02u);
        CHECK((raw_words[3] & 0xFFu) ==
              static_cast<std::uint32_t>('A'));
        CHECK(((raw_words[3] >> 8u) & 0xFFu) ==
              static_cast<std::uint32_t>('S'));
        CHECK(((raw_words[3] >> 16u) & 0xFFu) ==
              static_cast<std::uint32_t>('S'));
        CHECK(((raw_words[3] >> 24u) & 0xFFu) ==
              static_cast<std::uint32_t>('E'));

        // A final Pause must not discard the unread tail of the host FIFO.
        CHECK(raw_cd.data_bytes_available() == 292u);
        CHECK(raw_cd.write8(0x1F801801u, 0x09u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.data_bytes_available() == 292u);
        CHECK(raw_cd.read8(0x1F801801u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x01u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801803u, 0x07u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.write8(0x1F801800u, 0x00u).status ==
              jojo::R3000aBusStatus::ok);
        raw_cd.step(33869u);
        CHECK(raw_cd.data_bytes_available() == 292u);

        CHECK(raw_cd.write8(0x1F801803u, 0x00u).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(raw_cd.data_bytes_available() == 0u);
    }

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
