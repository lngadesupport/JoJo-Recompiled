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

    // 3E: GPU DMA2 linked-list mode. Each node starts with a header
    // whose high byte is the GP0 word count and whose low 24 bits link to
    // the next node; bit 23 terminates the list.
    const auto write_word = [&](std::size_t offset, std::uint32_t value) {
        ram[offset + 0u] = static_cast<std::uint8_t>(value);
        ram[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
        ram[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
        ram[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
    };
    write_word(0x5000u, 0x02005020u); // 2 words, next node 0x5020
    write_word(0x5004u, 0x00000000u); // GP0 NOP
    write_word(0x5008u, 0x00000000u); // GP0 NOP
    write_word(0x5020u, 0x01800000u); // 1 word, end marker
    write_word(0x5024u, 0x00000000u); // GP0 NOP

    CHECK(hw.write32(0x1F8010A0u, 0x00005000u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A8u, 0x01000401u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.pending_dma_transfer().has_value());
    if (hw.pending_dma_transfer()) {
        CHECK(hw.pending_dma_transfer()->channel == 2u);
        CHECK(hw.pending_dma_transfer()->from_ram);
        CHECK(hw.pending_dma_transfer()->sync_mode == 2u);
    }
    const auto gp0_before_linked = hw.gpu_gp0_word_count();
    CHECK(hw.execute_pending_dma(ram));
    CHECK(hw.gpu_gp0_word_count() == gp0_before_linked + 3u);
    CHECK(!hw.pending_dma_transfer().has_value());

    // Cyclic lists must fail transactionally and leave the pending request
    // intact so diagnostics can report the malformed chain.
    write_word(0x5100u, 0x01005100u);
    write_word(0x5104u, 0x00000000u);
    CHECK(hw.write32(0x1F8010A0u, 0x00005100u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A8u, 0x01000401u).status ==
          jojo::R3000aBusStatus::ok);
    const auto gp0_before_cycle = hw.gpu_gp0_word_count();
    CHECK(!hw.execute_pending_dma(ram));
    CHECK(hw.gpu_gp0_word_count() == gp0_before_cycle);
    CHECK(hw.pending_dma_transfer().has_value());
    hw.cancel_pending_dma_transfer();

    // 3F: OTC DMA6 clears a reverse ordering table. Starting at the
    // highest entry, each word points four bytes backward and the final
    // entry receives the PS1 linked-list terminator 00FFFFFFh.
    const std::uint32_t ch6_enable = 1u << (6u * 4u + 3u);
    CHECK(hw.write32(
              0x1F8010F0u,
              ch2_enable | ch3_enable | ch6_enable).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010E0u, 0x0000520Cu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010E4u, 4u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010E8u, 0x11000002u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.pending_dma_transfer().has_value());
    if (hw.pending_dma_transfer()) {
        CHECK(hw.pending_dma_transfer()->channel == 6u);
        CHECK(!hw.pending_dma_transfer()->from_ram);
        CHECK(hw.pending_dma_transfer()->sync_mode == 0u);
        CHECK(hw.pending_dma_transfer()->words == 4u);
    }
    CHECK(hw.execute_pending_dma(ram));

    const auto read_word = [&](std::size_t offset) {
        return static_cast<std::uint32_t>(ram[offset + 0u]) |
            (static_cast<std::uint32_t>(ram[offset + 1u]) << 8u) |
            (static_cast<std::uint32_t>(ram[offset + 2u]) << 16u) |
            (static_cast<std::uint32_t>(ram[offset + 3u]) << 24u);
    };
    CHECK(read_word(0x520Cu) == 0x00005208u);
    CHECK(read_word(0x5208u) == 0x00005204u);
    CHECK(read_word(0x5204u) == 0x00005200u);
    CHECK(read_word(0x5200u) == 0x00FFFFFFu);
    CHECK(!hw.pending_dma_transfer().has_value());

    // Direct-disc invariant: hardware activity never mutates the source image.
    CHECK(read_all(iso_path) == before);

    fs::remove_all(root);
    return failures ? 1 : 0;
}
