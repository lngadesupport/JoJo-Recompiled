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

static jojo::R3000aStepResult run_one(
    jojo::R3000aState& s,
    TestR3000aBus& bus,
    std::uint32_t instruction) {
    bus.store32(s.pc, instruction);
    return jojo::step_r3000a(s, bus);
}

int main() {
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 7u;
        s.gpr[2] = 9u;
        s.gpr[0] = 0xDEADBEEFu;
        const auto result = run_one(s, bus, test_mips::r(1, 2, 3, 0, 0x21)); // ADDU
        CHECK(result.status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[3] == 16u);
        CHECK(s.gpr[0] == 0u);
        CHECK(s.pc == 0x1004u && s.next_pc == 0x1008u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[2] = 0x12345678u;
        CHECK(run_one(s, bus, test_mips::r(0, 2, 3, 4, 0x00)).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[3] == 0x23456780u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[2] = 0x80000000u;
        CHECK(run_one(s, bus, test_mips::r(0, 2, 3, 1, 0x02)).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[3] == 0x40000000u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[2] = 0x80000000u;
        CHECK(run_one(s, bus, test_mips::r(0, 2, 3, 1, 0x03)).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[3] == 0xC0000000u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 33u;
        s.gpr[2] = 3u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 3, 0, 0x04)).status == jojo::R3000aStepStatus::retired); // SLLV
        CHECK(s.gpr[3] == 6u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xF0F0AA55u;
        s.gpr[2] = 0x0FF00FF0u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 3, 0, 0x24)).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[3] == 0x00F00A50u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFFu;
        s.gpr[2] = 1u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 3, 0, 0x2A)).status == jojo::R3000aStepStatus::retired); // SLT
        CHECK(s.gpr[3] == 1u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFFu;
        s.gpr[2] = 1u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 3, 0, 0x2B)).status == jojo::R3000aStepStatus::retired); // SLTU
        CHECK(s.gpr[3] == 0u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u;
        CHECK(run_one(s, bus, test_mips::i(0x09, 1, 2, 0xFFFFu)).status == jojo::R3000aStepStatus::retired); // ADDIU -1
        CHECK(s.gpr[2] == 0u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFF1234u;
        CHECK(run_one(s, bus, test_mips::i(0x0C, 1, 2, 0x00FFu)).status == jojo::R3000aStepStatus::retired); // ANDI
        CHECK(s.gpr[2] == 0x34u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0x00001000u;
        CHECK(run_one(s, bus, test_mips::i(0x0D, 1, 2, 0x00F0u)).status == jojo::R3000aStepStatus::retired); // ORI
        CHECK(s.gpr[2] == 0x000010F0u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0x000010F0u;
        CHECK(run_one(s, bus, test_mips::i(0x0E, 1, 2, 0x00FFu)).status == jojo::R3000aStepStatus::retired); // XORI
        CHECK(s.gpr[2] == 0x0000100Fu);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        CHECK(run_one(s, bus, test_mips::i(0x0F, 0, 2, 0x1234u)).status == jojo::R3000aStepStatus::retired); // LUI
        CHECK(s.gpr[2] == 0x12340000u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFFu;
        CHECK(run_one(s, bus, test_mips::i(0x0A, 1, 2, 1u)).status == jojo::R3000aStepStatus::retired); // SLTI
        CHECK(s.gpr[2] == 1u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFEu;
        CHECK(run_one(s, bus, test_mips::i(0x0B, 1, 2, 0xFFFFu)).status == jojo::R3000aStepStatus::retired); // SLTIU with sign-extended -1
        CHECK(s.gpr[2] == 1u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 5u;
        s.gpr[2] = 8u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 0, 0, 0x21)).status == jojo::R3000aStepStatus::retired); // write $zero
        CHECK(s.gpr[0] == 0u);
    }

    return failures ? 1 : 0;
}
