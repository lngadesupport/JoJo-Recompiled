#include "core/ps1_disc_session.h"
#include "core/ps1_hardware_services.h"
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
    const auto root = fs::temp_directory_path() / "jojo_ps1_cdrom_dma_contract";
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

    jojo::Ps1HardwareServices hw;
    hw.attach_disc(&disc.value);

    // Program Setloc(00:02:25) + ReadN through the actual CD MMIO window.
    CHECK(hw.write8(0x1F801800u, 0u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801802u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801802u, 0x02u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801802u, 0x25u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801801u, 0x02u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.read8(0x1F801801u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801803u, 0x07u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);

    CHECK(hw.write8(0x1F801801u, 0x06u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.read8(0x1F801801u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801803u, 0x07u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    hw.step(451584u);
    CHECK(hw.read8(0x1F801801u).status == jojo::R3000aBusStatus::ok);

    // Channel 3 CD-ROM -> RAM: 2048 bytes = 512 words.
    const std::uint32_t ch3_enable = 1u << (3u * 4u + 3u);
    CHECK(hw.write32(0x1F8010F0u, ch3_enable).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B0u, 0x00002000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B4u, 512u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B8u, 0x11000000u).status == jojo::R3000aBusStatus::ok);

    std::vector<std::uint8_t> ram(2u * 1024u * 1024u, 0u);
    CHECK(hw.execute_pending_dma(ram));
    CHECK(!hw.pending_dma_transfer().has_value());
    CHECK(hw.completed_dma_transfer_count() == 1u);
    CHECK(ram[0x2000u] == 'A');
    CHECK(ram[0x2001u] == 'S');
    CHECK(ram[0x2002u] == 'S');
    CHECK(ram[0x2003u] == 'E');
    CHECK(ram[0x2004u] == 'T');

    // Out-of-range destination must fail before RAM mutation and leave request pending.
    CHECK(hw.write32(0x1F8010B0u, 0x001FFFFCu).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B4u, 2u).status == jojo::R3000aBusStatus::ok);
    // Acknowledge prior INT1 and refill one sector through ReadN's
    // INT3 -> delayed INT1 sequence.
    CHECK(hw.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801803u, 0x07u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801801u, 0x06u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.read8(0x1F801801u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801800u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801803u, 0x07u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write8(0x1F801800u, 0x00u).status == jojo::R3000aBusStatus::ok);
    hw.step(451584u);
    CHECK(hw.read8(0x1F801801u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B8u, 0x11000000u).status == jojo::R3000aBusStatus::ok);
    const auto tail_before = ram.back();
    CHECK(!hw.execute_pending_dma(ram));
    CHECK(ram.back() == tail_before);
    CHECK(hw.pending_dma_transfer().has_value());
    hw.cancel_pending_dma_transfer();

    CHECK(read_all(iso_path) == before);
    fs::remove_all(root);
    return failures ? 1 : 0;
}
