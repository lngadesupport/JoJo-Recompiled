#include "core/ps1_spu.h"
#include "core/ps1_hardware_services.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1Spu spu;

    CHECK(spu.write16(0x1F801C00u, 0x1234u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C02u, 0xFEDCu).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C04u, 0x1000u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C06u, 0x0010u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.read16(0x1F801C00u).value == 0x1234u);
    CHECK(spu.read16(0x1F801C02u).value == 0xFEDCu);
    CHECK(spu.read16(0x1F801C04u).value == 0x1000u);
    CHECK(spu.read16(0x1F801C06u).value == 0x0010u);

    CHECK(spu.write16(0x1F801D88u, 0x0001u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.voice(0u).keyed_on);
    CHECK(!spu.voice(0u).releasing);
    CHECK(spu.voice(0u).current_address == 0x80u);
    CHECK((spu.endx_flags() & 1u) == 0u);

    CHECK(spu.write16(0x1F801D8Cu, 0x0001u).status == jojo::R3000aBusStatus::ok);
    CHECK(!spu.voice(0u).keyed_on);
    CHECK(spu.voice(0u).releasing);

    CHECK(spu.write16(0x1F801DA6u, 0x0020u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.transfer_current_address() == 0x100u);
    CHECK(spu.write16(0x1F801DA8u, 0xA1B2u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.sound_ram_byte(0x100u) == 0xB2u);
    CHECK(spu.sound_ram_byte(0x101u) == 0xA1u);
    CHECK(spu.transfer_current_address() == 0x102u);

    jojo::Ps1HardwareServices hw;
    CHECK(hw.write32(0x1F801D80u, 0x22221111u).status == jojo::R3000aBusStatus::ok);
    const auto main_volume = hw.read32(0x1F801D80u);
    CHECK(main_volume.status == jojo::R3000aBusStatus::ok);
    CHECK(main_volume.value == 0x22221111u);

    CHECK(spu.read16(0x1F801E60u).status == jojo::R3000aBusStatus::unsupported);

    return failures ? 1 : 0;
}
