#include "core/ps1_hardware_services.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1HardwareServices hw;
    std::vector<std::uint8_t> ram(2u * 1024u * 1024u, 0u);

    ram[0x1000u] = 0x11u;
    ram[0x1001u] = 0x22u;
    ram[0x1002u] = 0x33u;
    ram[0x1003u] = 0x44u;
    ram[0x1004u] = 0x55u;
    ram[0x1005u] = 0x66u;
    ram[0x1006u] = 0x77u;
    ram[0x1007u] = 0x88u;

    CHECK(hw.write16(0x1F801DA6u, 0x0040u).status == jojo::R3000aBusStatus::ok);

    const std::uint32_t ch4_enable = 1u << (4u * 4u + 3u);
    CHECK(hw.write32(0x1F8010F0u, ch4_enable).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C0u, 0x00001000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C4u, 2u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C8u, 0x11000001u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.execute_pending_dma(ram));
    CHECK(hw.completed_dma_transfer_count() == 1u);

    for (std::uint32_t i = 0; i < 8u; ++i) {
        CHECK(hw.spu().sound_ram_byte(0x200u + i) == ram[0x1000u + i]);
    }

    CHECK(hw.write16(0x1F801DA6u, 0x0040u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C0u, 0x00001100u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C4u, 2u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C8u, 0x11000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.execute_pending_dma(ram));
    CHECK(hw.completed_dma_transfer_count() == 2u);

    for (std::uint32_t i = 0; i < 8u; ++i) {
        CHECK(ram[0x1100u + i] == ram[0x1000u + i]);
    }

    return failures ? 1 : 0;
}
