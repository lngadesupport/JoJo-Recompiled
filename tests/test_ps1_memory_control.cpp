#include "core/ps1_hardware_services.h"

#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1HardwareServices hw{};

    CHECK(hw.read32(0x1F801000u).value == 0x1F000000u);
    CHECK(hw.read32(0x1F801004u).value == 0x1F802000u);
    CHECK(hw.read32(0x1F801008u).value == 0x0013243Fu);
    CHECK(hw.read32(0x1F801010u).value == 0x0013243Fu);
    CHECK(hw.read32(0x1F801014u).value == 0x200931E1u);
    CHECK(hw.read32(0x1F801018u).value == 0x00020843u);
    CHECK(hw.read32(0x1F80101Cu).value == 0x00070777u);

    const auto baseline = hw.diagnostic_state_hash();
    CHECK(hw.write32(0x1F801020u, 0x00001325u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.read32(0x1F801020u).value == 0x00001325u);
    CHECK(hw.diagnostic_state_hash() != baseline);

    CHECK(hw.read32(0x1F801060u).value == 0x00000B88u);
    CHECK(hw.write16(0x1F801060u, 0x0888u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.read16(0x1F801060u).value == 0x0888u);
    CHECK(hw.write32(0x1F801060u, 0x00000B88u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.read32(0x1F801060u).value == 0x00000B88u);

    CHECK(hw.read32(0x1F801024u).status ==
          jojo::R3000aBusStatus::unsupported);

    return failures ? 1 : 0;
}
