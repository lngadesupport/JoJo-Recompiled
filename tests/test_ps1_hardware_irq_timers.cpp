#include "core/ps1_hardware_services.h"

#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1HardwareServices hw;

    const auto imask_reset = hw.read16(0x1F801074u);
    CHECK(imask_reset.status == jojo::R3000aBusStatus::ok);
    CHECK(imask_reset.value == 0u);

    CHECK(hw.write16(0x1F801074u, 0xFFFFu).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.interrupt_mask() == 0x07FFu);
    CHECK(hw.read16(0x1F801074u).value == 0x07FFu);

    CHECK(hw.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.interrupt_status() == 0u);

    CHECK(hw.write16(0x1F801100u, 0u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write16(0x1F801108u, 3u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write16(0x1F801104u, 0x0058u).status == jojo::R3000aBusStatus::ok);
    hw.step(2u);
    CHECK(hw.timer_counter(0u) == 2u);
    CHECK(hw.interrupt_status() == 0u);
    hw.step(1u);
    CHECK(hw.timer_counter(0u) == 0u);
    CHECK((hw.interrupt_status() & 0x0010u) != 0u);

    CHECK(hw.write16(0x1F801070u, static_cast<std::uint16_t>(~0x0010u)).status == jojo::R3000aBusStatus::ok);
    CHECK((hw.interrupt_status() & 0x0010u) == 0u);

    CHECK(hw.write16(0x1F801114u, 0x0100u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.timer_mode(1u) == 0x0100u);
    CHECK(hw.timer_counter(1u) == 0u);

    CHECK(hw.vblank_count() == 0u);
    hw.signal_vblank();
    CHECK(hw.vblank_count() == 1u);
    CHECK((hw.interrupt_status() & 0x0001u) != 0u);
    CHECK(hw.interrupt_pending());
    CHECK(hw.write16(0x1F801070u, 0x07FEu).status == jojo::R3000aBusStatus::ok);
    CHECK((hw.interrupt_status() & 0x0001u) == 0u);

    CHECK(hw.write16(0x1F801104u, 0x8000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(hw.read32(0x1F801100u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(hw.read16(0x1F801180u).status == jojo::R3000aBusStatus::unsupported);

    return failures ? 1 : 0;
}
