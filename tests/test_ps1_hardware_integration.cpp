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
    const auto root = fs::temp_directory_path() / "jojo_ps1_phase3_integration";
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

    // 3A: deterministic timer target IRQ routed through I_STAT/I_MASK.
    CHECK(hw.write16(0x1F801074u, static_cast<std::uint16_t>(1u << 4u)).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write16(0x1F801108u, 3u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write16(0x1F801104u, 0x0018u).status == jojo::R3000aBusStatus::ok);
    hw.step(3u);
    CHECK((hw.interrupt_status() & (1u << 4u)) != 0u);
    CHECK(hw.interrupt_pending());

    // 3C: Setloc(00:02:25) points at fixture DATA/ASSET.DAT sector 25.
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

    std::vector<std::uint8_t> ram(2u * 1024u * 1024u, 0u);

    // 3B + 3C: CD-ROM DMA channel 3 transfers one 2048-byte sector to RAM.
    const std::uint32_t ch3_enable = 1u << (3u * 4u + 3u);
    CHECK(hw.write32(0x1F8010F0u, ch3_enable).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B0u, 0x00002000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B4u, 512u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B8u, 0x11000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.execute_pending_dma(ram));
    CHECK(ram[0x2000u] == 'A');
    CHECK(ram[0x2001u] == 'S');
    CHECK(ram[0x2002u] == 'S');
    CHECK(ram[0x2003u] == 'E');
    CHECK(ram[0x2004u] == 'T');
    CHECK(hw.completed_dma_transfer_count() == 1u);

    // 3D: feed two harmless GP0 NOP words from RAM through DMA channel 2.
    ram[0x3000u] = 0u;
    ram[0x3001u] = 0u;
    ram[0x3002u] = 0u;
    ram[0x3003u] = 0u;
    ram[0x3004u] = 0u;
    ram[0x3005u] = 0u;
    ram[0x3006u] = 0u;
    ram[0x3007u] = 0u;

    const std::uint32_t ch2_enable = 1u << (2u * 4u + 3u);
    CHECK(hw.write32(0x1F8010F0u, ch2_enable | ch3_enable).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A0u, 0x00003000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A4u, 2u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A8u, 0x11000001u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.execute_pending_dma(ram));
    CHECK(hw.completed_dma_transfer_count() == 2u);
    CHECK(hw.gpu_gp0_word_count() == 2u);

    // Direct-disc invariant: hardware activity never mutates the source image.
    CHECK(read_all(iso_path) == before);

    fs::remove_all(root);
    return failures ? 1 : 0;
}
