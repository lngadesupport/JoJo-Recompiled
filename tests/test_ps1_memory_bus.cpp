#include "core/ps1_memory_bus.h"
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1MemoryBus bus;

    CHECK(bus.write32(0x00000100u, 0x44332211u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x80000100u).value == 0x11u);
    CHECK(bus.read8(0xA0000101u).value == 0x22u);
    CHECK(bus.read16(0x00000102u).value == 0x4433u);
    CHECK(bus.read32(0x80000100u).value == 0x44332211u);

    CHECK(bus.write16(0x1F800010u, 0xBBAAu).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read16(0x9F800010u).value == 0xBBAAu);
    CHECK(bus.read16(0xBF800010u).value == 0xBBAAu);
    CHECK(bus.read16(0x00000010u).value != 0xBBAAu);

    CHECK(bus.write16(0x1F801074u, 0xFFFFu).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.interrupt_mask() == 0x07FFu);
    const auto imask_enabled = bus.read16(0x1F801074u);
    CHECK(imask_enabled.status == jojo::R3000aBusStatus::ok);
    CHECK(imask_enabled.value == 0x07FFu);

    CHECK(bus.write16(0x1F801074u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.interrupt_mask() == 0x0000u);
    const auto imask_disabled = bus.read16(0x1F801074u);
    CHECK(imask_disabled.status == jojo::R3000aBusStatus::ok);
    CHECK(imask_disabled.value == 0x0000u);

    CHECK(bus.read32(0x1F801074u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.write16(0x1F801070u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read16(0x1F801070u).status == jojo::R3000aBusStatus::unsupported);

    const auto dpcr_reset = bus.read32(0x1F8010F0u);
    CHECK(dpcr_reset.status == jojo::R3000aBusStatus::ok);
    CHECK(dpcr_reset.value == 0x07654321u);
    CHECK(bus.write32(0x1F8010F0u, 0x33333333u).status == jojo::R3000aBusStatus::ok);
    const auto dpcr_written = bus.read32(0x1F8010F0u);
    CHECK(dpcr_written.status == jojo::R3000aBusStatus::ok);
    CHECK(dpcr_written.value == 0x33333333u);

    CHECK(bus.dma_interrupt() == 0u);
    const auto dicr_reset = bus.read32(0x1F8010F4u);
    CHECK(dicr_reset.status == jojo::R3000aBusStatus::ok);
    CHECK(dicr_reset.value == 0u);
    bus.set_diagnostic_mmio_probe_enabled(true);
    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F8010F4u, 0x00FF807Fu).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    const auto dicr_written = bus.read32(0x1F8010F4u);
    CHECK(dicr_written.status == jojo::R3000aBusStatus::ok);
    CHECK(dicr_written.value == 0x80FF807Fu);
    CHECK(bus.dma_interrupt() == 0x80FF807Fu);
    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F8010F4u, 0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(!bus.last_diagnostic_mmio_probe().has_value());
    CHECK(bus.read32(0x1F8010F4u).value == 0u);
    bus.set_diagnostic_mmio_probe_enabled(false);

    CHECK(bus.write32(0x1F801114u, 0xABCD0100u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.timer1_mode() == 0x0100u);
    CHECK(bus.timer1_counter() == 0x0000u);
    CHECK(bus.read32(0x1F801114u).status == jojo::R3000aBusStatus::unsupported);

    bus.clear_last_unsupported_access();
    bus.set_diagnostic_mmio_probe_enabled(true);
    const auto probe_zero = bus.read32(0x1F801080u);
    CHECK(probe_zero.status == jojo::R3000aBusStatus::ok);
    CHECK(probe_zero.value == 0u);
    CHECK(bus.last_diagnostic_mmio_probe().has_value());
    if (bus.last_diagnostic_mmio_probe()) {
        CHECK(bus.last_diagnostic_mmio_probe()->guest_address == 0x1F801080u);
        CHECK(bus.last_diagnostic_mmio_probe()->width == 4u);
        CHECK(!bus.last_diagnostic_mmio_probe()->write);
        CHECK(bus.last_diagnostic_mmio_probe()->value == 0u);
    }

    bus.clear_last_diagnostic_mmio_probe();
    CHECK(bus.write32(0x1F801080u, 0x12345678u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.last_diagnostic_mmio_probe().has_value());
    if (bus.last_diagnostic_mmio_probe()) {
        CHECK(bus.last_diagnostic_mmio_probe()->write);
        CHECK(bus.last_diagnostic_mmio_probe()->value == 0x12345678u);
    }
    bus.clear_last_diagnostic_mmio_probe();
    const auto probe_shadow = bus.read32(0x1F801080u);
    CHECK(probe_shadow.status == jojo::R3000aBusStatus::ok);
    CHECK(probe_shadow.value == 0x12345678u);

    bus.set_diagnostic_mmio_probe_enabled(false);
    CHECK(bus.read32(0x1F801080u).status == jojo::R3000aBusStatus::unsupported);

    const auto unsupported = bus.read32(0x1F801070u);
    CHECK(unsupported.status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.last_unsupported_access().has_value());
    if (bus.last_unsupported_access()) {
        CHECK(bus.last_unsupported_access()->guest_address == 0x1F801070u);
        CHECK(bus.last_unsupported_access()->physical_address == 0x1F801070u);
        CHECK(bus.last_unsupported_access()->width == 4u);
        CHECK(!bus.last_unsupported_access()->write);
    }

    CHECK(bus.read8(0x00200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read8(0x80200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read8(0xA0200000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(bus.read32(0xC0000000u).status == jojo::R3000aBusStatus::unsupported);

    return failures ? 1 : 0;
}
