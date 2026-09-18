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

    // Retail software commonly uses SW/LW on the 16-bit I_STAT/I_MASK
    // registers. The upper halfword is ignored by the hardware-facing model.
    CHECK(hw.write32(0x1F801074u, 0xFFFF0001u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.interrupt_mask() == 0x0001u);
    CHECK(hw.read32(0x1F801074u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.read32(0x1F801074u).value == 0x0001u);

    hw.signal_vblank();
    CHECK((hw.interrupt_status() & 0x0001u) != 0u);
    CHECK(hw.write32(0x1F801070u, 0xFFFFFFFEu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK((hw.interrupt_status() & 0x0001u) == 0u);
    CHECK(hw.read32(0x1F801070u).value == 0u);

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

    CHECK(hw.write32(0x1F801110u, 0x12345678u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.timer_counter(1u) == 0x5678u);
    CHECK(hw.read32(0x1F801110u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.read32(0x1F801110u).value == 0x5678u);

    CHECK(hw.write32(0x1F801118u, 0xABCD0020u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.timer_target(1u) == 0x0020u);
    CHECK(hw.read32(0x1F801118u).value == 0x0020u);

    CHECK(hw.write32(0x1F801114u, 0x00000100u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.read32(0x1F801114u).value == 0x0100u);


    // Timer1 source 1 is HBlank: it must not tick every CPU step.
    CHECK(hw.write16(0x1F801114u, 0x0100u).status == jojo::R3000aBusStatus::ok);
    hw.step(2152u);
    CHECK(hw.timer_counter(1u) == 0u);
    hw.step(1u);
    CHECK(hw.timer_counter(1u) == 1u);

    // Timer2 source 2 is system clock divided by eight.
    CHECK(hw.write16(0x1F801124u, 0x0200u).status == jojo::R3000aBusStatus::ok);
    hw.step(7u);
    CHECK(hw.timer_counter(2u) == 0u);
    hw.step(1u);
    CHECK(hw.timer_counter(2u) == 1u);

    CHECK(hw.vblank_count() == 1u);
    hw.signal_vblank();
    CHECK(hw.vblank_count() == 2u);
    CHECK((hw.interrupt_status() & 0x0001u) != 0u);
    CHECK(hw.interrupt_pending());
    CHECK(hw.write16(0x1F801070u, 0x07FEu).status == jojo::R3000aBusStatus::ok);
    CHECK((hw.interrupt_status() & 0x0001u) == 0u);

    CHECK(hw.write16(0x1F801104u, 0x8000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(hw.read32(0x1F801100u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.read16(0x1F801180u).status == jojo::R3000aBusStatus::unsupported);

    return failures ? 1 : 0;
}
