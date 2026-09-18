#include "core/ps1_memory_bus.h"
#include "core/ps1_sio0.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static std::uint8_t exchange(jojo::Ps1Sio0& sio, std::uint8_t value) {
    CHECK(sio.write8(0x1F801040u, value).status == jojo::R3000aBusStatus::ok);
    const auto received = sio.read8(0x1F801040u);
    CHECK(received.status == jojo::R3000aBusStatus::ok);
    return static_cast<std::uint8_t>(received.value);
}

int main() {
    jojo::Ps1Sio0 sio;
    CHECK(sio.write16(0x1F801048u, 0x000Du).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write16(0x1F80104Eu, 0x0088u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write16(0x1F80104Au, 0x0003u).status == jojo::R3000aBusStatus::ok);

    std::uint16_t buttons = 0xFFFFu;
    buttons &= static_cast<std::uint16_t>(~(1u << 4u));  // Up
    buttons &= static_cast<std::uint16_t>(~(1u << 14u)); // Cross
    sio.set_digital_pad_buttons(0u, buttons);

    CHECK(exchange(sio, 0x01u) == 0xFFu);
    CHECK((sio.read32(0x1F801044u).value & (1u << 9u)) != 0u);
    CHECK(exchange(sio, 0x42u) == 0x41u);
    CHECK(exchange(sio, 0x00u) == 0x5Au);
    CHECK(exchange(sio, 0x00u) == 0xEFu);
    CHECK(exchange(sio, 0x00u) == 0xBFu);

    CHECK(sio.write16(0x1F80104Au, 0x0013u).status == jojo::R3000aBusStatus::ok);
    CHECK((sio.read32(0x1F801044u).value & (1u << 9u)) == 0u);

    sio.set_digital_pad_buttons(1u, 0xFFF7u); // Start pressed.
    CHECK(sio.write16(0x1F80104Au, 0x2003u).status == jojo::R3000aBusStatus::ok);
    CHECK(exchange(sio, 0x01u) == 0xFFu);
    CHECK(exchange(sio, 0x42u) == 0x41u);
    CHECK(exchange(sio, 0x00u) == 0x5Au);
    CHECK(exchange(sio, 0x00u) == 0xF7u);
    CHECK(exchange(sio, 0x00u) == 0xFFu);

    jojo::Ps1MemoryBus bus;
    bus.hardware_services().sio0().set_digital_pad_buttons(0u, 0xFFEFu);
    CHECK(bus.write16(0x1F80104Au, 0x0003u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801040u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x1F801040u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x1F801040u).value == 0xFFu);
    CHECK(bus.write8(0x1F801040u, 0x42u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x1F801040u).value == 0x41u);

    bus.hardware_services().step(1u);
    CHECK((bus.hardware_services().interrupt_status() & 0x0080u) != 0u);

    return failures ? 1 : 0;
}
