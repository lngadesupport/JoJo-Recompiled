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


static void test_observation_counters_do_not_change_guest_state_hash() {
    jojo::Ps1Sio0 observed;
    jojo::Ps1Sio0 pristine;

    CHECK(observed.write16(0x1F80104Au, 0x0003u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(exchange(observed, 0x01u) == 0xFFu);
    CHECK(exchange(observed, 0x42u) == 0x41u);
    CHECK(exchange(observed, 0x00u) == 0x5Au);
    CHECK(exchange(observed, 0x00u) == 0xFFu);
    CHECK(exchange(observed, 0x00u) == 0xFFu);
    CHECK(observed.digital_pad_poll_count(0u) == 1u);

    // Reset guest-visible SIO state; observation history intentionally survives.
    CHECK(observed.write16(0x1F80104Au, 0x0040u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(observed.diagnostic_state_hash() == pristine.diagnostic_state_hash());
}

int main() {
    test_observation_counters_do_not_change_guest_state_hash();
    jojo::Ps1Sio0 sio;
    CHECK(sio.write16(0x1F801048u, 0x000Du).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write16(0x1F80104Eu, 0x0088u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write16(0x1F80104Au, 0x0003u).status == jojo::R3000aBusStatus::ok);

    std::uint16_t buttons = 0xFFFFu;
    buttons &= static_cast<std::uint16_t>(~(1u << 4u));  // Up
    buttons &= static_cast<std::uint16_t>(~(1u << 14u)); // Cross
    sio.set_digital_pad_buttons(0u, buttons);

    CHECK(exchange(sio, 0x01u) == 0xFFu);
    const auto status16 = sio.read16(0x1F801044u);
    CHECK(status16.status == jojo::R3000aBusStatus::ok);
    CHECK(status16.value ==
          (sio.read32(0x1F801044u).value & 0xFFFFu));
    CHECK((sio.read32(0x1F801044u).value & (1u << 7u)) != 0u);
    CHECK((sio.read32(0x1F801044u).value & (1u << 9u)) == 0u);

    CHECK(sio.write16(0x1F80104Au, 0x1003u).status == jojo::R3000aBusStatus::ok);
    CHECK((sio.read32(0x1F801044u).value & (1u << 9u)) != 0u);
    // Acknowledge while /ACK (DSR) is still asserted: IRQ must remain set.
    CHECK(sio.write16(0x1F80104Au, 0x1013u).status == jojo::R3000aBusStatus::ok);
    CHECK((sio.read32(0x1F801044u).value & (1u << 9u)) != 0u);
    CHECK(exchange(sio, 0x42u) == 0x41u);
    CHECK(exchange(sio, 0x00u) == 0x5Au);
    CHECK(exchange(sio, 0x00u) == 0xEFu);
    CHECK(exchange(sio, 0x00u) == 0xBFu);
    CHECK(sio.digital_pad_poll_count(0u) == 1u);
    CHECK(sio.raw_data_write_count() >= 5u);
    CHECK(sio.controller_address_byte_count() >= 1u);
    CHECK(sio.controller_command_byte_count() >= 1u);
    CHECK(sio.digital_pad_pressed_poll_count(0u) == 1u);
    CHECK(sio.digital_pad_poll_count(1u) == 0u);
    CHECK(sio.digital_pad_pressed_poll_count(1u) == 0u);

    // Once the final byte deasserts DSR, acknowledge clears the sticky SIO IRQ.
    CHECK(sio.write16(0x1F80104Au, 0x1013u).status == jojo::R3000aBusStatus::ok);
    CHECK((sio.read32(0x1F801044u).value & (1u << 9u)) == 0u);
    CHECK(sio.write16(0x1F80104Au, 0x1001u).status == jojo::R3000aBusStatus::ok);

    sio.set_digital_pad_buttons(1u, 0xFFF7u); // Start pressed.
    CHECK(sio.write16(0x1F80104Au, 0x2003u).status == jojo::R3000aBusStatus::ok);
    CHECK(exchange(sio, 0x01u) == 0xFFu);
    CHECK(exchange(sio, 0x42u) == 0x41u);
    CHECK(exchange(sio, 0x00u) == 0x5Au);
    CHECK(exchange(sio, 0x00u) == 0xF7u);
    CHECK(exchange(sio, 0x00u) == 0xFFu);


    // JOY_DATA wide reads preview the RX FIFO with hardware-specific pop rules.
    CHECK(sio.write16(0x1F80104Au, 0x1001u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write16(0x1F80104Au, 0x1003u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write8(0x1F801040u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write8(0x1F801040u, 0x42u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write8(0x1F801040u, 0x00u).status == jojo::R3000aBusStatus::ok);
    const auto wide16 = sio.read16(0x1F801040u);
    CHECK(wide16.status == jojo::R3000aBusStatus::ok);
    CHECK(wide16.value == 0x41FFu);
    CHECK(sio.read8(0x1F801040u).value == 0x41u);
    CHECK(sio.read8(0x1F801040u).value == 0x5Au);

    CHECK(sio.write16(0x1F80104Au, 0x1001u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write16(0x1F80104Au, 0x1003u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write8(0x1F801040u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write8(0x1F801040u, 0x42u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write8(0x1F801040u, 0x00u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write8(0x1F801040u, 0x00u).status == jojo::R3000aBusStatus::ok);
    const auto wide32 = sio.read32(0x1F801040u);
    CHECK(wide32.status == jojo::R3000aBusStatus::ok);
    CHECK(wide32.value == 0xEF5A41FFu);
    CHECK((sio.read32(0x1F801044u).value & (1u << 1u)) == 0u);

    jojo::Ps1MemoryBus bus;
    bus.hardware_services().sio0().set_digital_pad_buttons(0u, 0xFFEFu);
    CHECK(bus.write16(0x1F80104Au, 0x1003u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801040u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x1F801040u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x1F801040u).value == 0xFFu);
    CHECK(bus.write8(0x1F801040u, 0x42u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read8(0x1F801040u).value == 0x41u);

    CHECK((bus.hardware_services().interrupt_status() & 0x0080u) != 0u);
    CHECK(bus.write16(0x1F801070u, 0x077Fu).status == jojo::R3000aBusStatus::ok);
    CHECK((bus.hardware_services().interrupt_status() & 0x0080u) == 0u);
    bus.hardware_services().step(1u);
    CHECK((bus.hardware_services().interrupt_status() & 0x0080u) == 0u);
    CHECK(bus.write16(0x1F80104Au, 0x1001u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write16(0x1F80104Au, 0x1011u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write16(0x1F80104Au, 0x1003u).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.write8(0x1F801040u, 0x01u).status == jojo::R3000aBusStatus::ok);
    CHECK((bus.hardware_services().interrupt_status() & 0x0080u) != 0u);

    return failures ? 1 : 0;
}
