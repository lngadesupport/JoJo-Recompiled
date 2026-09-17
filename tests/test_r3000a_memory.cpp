#include "core/r3000a_reference_executor.h"
#include "mips_test_encode.h"
#include "r3000a_test_bus.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::R3000aState base_state() {
    jojo::R3000aState s{};
    s.pc = 0x1000u;
    s.next_pc = 0x1004u;
    return s;
}

static void retire_nop(jojo::R3000aState& s, TestR3000aBus& bus) {
    bus.store32(s.pc, 0u);
    CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
}

int main() {
    // LW exposes the loaded value only after one intervening instruction.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2000u;
        s.gpr[8] = 0x11111111u;
        bus.store32(0x2000u, 0xAABBCCDDu);
        bus.store32(0x1000u, test_mips::i(0x23, 4, 8, 0));
        bus.store32(0x1004u, test_mips::r(8, 0, 9, 0, 0x21));
        bus.store32(0x1008u, test_mips::r(8, 0, 10, 0, 0x21));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[8] == 0x11111111u && s.pending_load.valid);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[9] == 0x11111111u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[10] == 0xAABBCCDDu);
    }

    // A direct write by the following instruction wins over the retiring load.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2000u;
        s.gpr[8] = 0x11111111u;
        bus.store32(0x2000u, 0xDEADBEEFu);
        bus.store32(0x1000u, test_mips::i(0x23, 4, 8, 0));
        bus.store32(0x1004u, test_mips::i(0x09, 0, 8, 5));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[8] == 5u && !s.pending_load.valid);
    }

    // Signed/unsigned loads.
    {
        struct LoadCase { std::uint8_t opcode; std::uint32_t address; std::uint32_t expected; };
        const LoadCase cases[] = {
            {0x20, 0x2000u, 0xFFFFFF80u}, // LB
            {0x24, 0x2000u, 0x00000080u}, // LBU
            {0x21, 0x2002u, 0xFFFF8001u}, // LH
            {0x25, 0x2002u, 0x00008001u}, // LHU
            {0x23, 0x2004u, 0x89ABCDEFu}, // LW
        };
        for (const auto& c : cases) {
            TestR3000aBus bus;
            auto s = base_state();
            s.gpr[4] = c.address;
            bus.store8(0x2000u, 0x80u);
            bus.store16(0x2002u, 0x8001u);
            bus.store32(0x2004u, 0x89ABCDEFu);
            bus.store32(0x1000u, test_mips::i(c.opcode, 4, 8, 0));
            CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
            retire_nop(s, bus);
            CHECK(s.gpr[8] == c.expected);
        }
    }

    // Stores are immediate and little-endian through the bus interface.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2000u;
        s.gpr[8] = 0xA1B2C3D4u;
        bus.store32(0x1000u, test_mips::i(0x28, 4, 8, 0)); // SB
        bus.store32(0x1004u, test_mips::i(0x29, 4, 8, 2)); // SH
        bus.store32(0x1008u, test_mips::i(0x2B, 4, 8, 4)); // SW
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK((bus.peek32(0x2000u) & 0xFFu) == 0xD4u);
        CHECK(((bus.peek32(0x2000u) >> 16) & 0xFFFFu) == 0xC3D4u);
        CHECK(bus.peek32(0x2004u) == 0xA1B2C3D4u);
    }

    // Alignment faults precede bus classification, even when destination is $zero.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2001u;
        bus.fail_unsupported(0x2001u);
        bus.store32(0x1000u, test_mips::i(0x23, 4, 0, 0)); // LW $zero,misaligned
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::adel);
        CHECK(s.cop0.bad_vaddr == 0x2001u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2001u;
        bus.fail_unsupported(0x2001u);
        bus.store32(0x1000u, test_mips::i(0x29, 4, 8, 0)); // SH misaligned
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::ades);
        CHECK(s.cop0.bad_vaddr == 0x2001u);
    }

    // Data bus error is architectural DBE; unsupported address space remains a boundary.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2000u;
        bus.fail_bus_error(0x2000u);
        bus.store32(0x1000u, test_mips::i(0x23, 4, 8, 0));
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::dbe);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2000u;
        bus.fail_unsupported(0x2000u);
        bus.store32(0x1000u, test_mips::i(0x23, 4, 8, 0));
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::boundary);
        CHECK(r.diagnostic.boundary == jojo::R3000aBoundaryCode::unsupported_address_space);
    }

    // A prior pending load retires before a following instruction enters an exception.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2000u;
        s.gpr[8] = 1u;
        bus.store32(0x2000u, 0x12345678u);
        bus.store32(0x1000u, test_mips::i(0x23, 4, 8, 0));
        bus.store32(0x1004u, 0x0000000Cu); // SYSCALL
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(s.gpr[8] == 0x12345678u && !s.pending_load.valid);
    }

    return failures ? 1 : 0;
}
