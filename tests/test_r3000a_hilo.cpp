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

struct DivCase {
    std::uint32_t lhs;
    std::uint32_t rhs;
    std::uint32_t expected_hi;
    std::uint32_t expected_lo;
    bool is_signed;
};

int main() {
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0x12345678u;
        CHECK(run_one(s, bus, test_mips::r(1, 0, 0, 0, 0x11)).status == jojo::R3000aStepStatus::retired); // MTHI
        CHECK(s.hi == 0x12345678u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0x89ABCDEFu;
        CHECK(run_one(s, bus, test_mips::r(1, 0, 0, 0, 0x13)).status == jojo::R3000aStepStatus::retired); // MTLO
        CHECK(s.lo == 0x89ABCDEFu);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.hi = 0xCAFEBABEu;
        CHECK(run_one(s, bus, test_mips::r(0, 0, 3, 0, 0x10)).status == jojo::R3000aStepStatus::retired); // MFHI
        CHECK(s.gpr[3] == 0xCAFEBABEu);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.lo = 0x0BADF00Du;
        CHECK(run_one(s, bus, test_mips::r(0, 0, 3, 0, 0x12)).status == jojo::R3000aStepStatus::retired); // MFLO
        CHECK(s.gpr[3] == 0x0BADF00Du);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFEu; // -2
        s.gpr[2] = 3u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 0, 0, 0x18)).status == jojo::R3000aStepStatus::retired); // MULT
        CHECK(s.hi == 0xFFFFFFFFu);
        CHECK(s.lo == 0xFFFFFFFAu);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFFu;
        s.gpr[2] = 2u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 0, 0, 0x19)).status == jojo::R3000aStepStatus::retired); // MULTU
        CHECK(s.hi == 1u);
        CHECK(s.lo == 0xFFFFFFFEu);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFF9u; // -7
        s.gpr[2] = 3u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 0, 0, 0x1A)).status == jojo::R3000aStepStatus::retired); // DIV
        CHECK(s.hi == 0xFFFFFFFFu); // -1 remainder
        CHECK(s.lo == 0xFFFFFFFEu); // -2 quotient
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 7u;
        s.gpr[2] = 3u;
        CHECK(run_one(s, bus, test_mips::r(1, 2, 0, 0, 0x1B)).status == jojo::R3000aStepStatus::retired); // DIVU
        CHECK(s.hi == 1u);
        CHECK(s.lo == 2u);
    }

    const DivCase cases[] = {
        {7u, 0u, 7u, 0xFFFFFFFFu, false},
        {7u, 0u, 7u, 0xFFFFFFFFu, true},
        {0xFFFFFFF9u, 0u, 0xFFFFFFF9u, 1u, true},
        {0x80000000u, 0xFFFFFFFFu, 0u, 0x80000000u, true},
    };

    for (const auto& c : cases) {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = c.lhs;
        s.gpr[2] = c.rhs;
        const auto funct = static_cast<std::uint8_t>(c.is_signed ? 0x1A : 0x1B);
        const auto result = run_one(s, bus, test_mips::r(1, 2, 0, 0, funct));
        CHECK(result.status == jojo::R3000aStepStatus::retired);
        CHECK(s.hi == c.expected_hi);
        CHECK(s.lo == c.expected_lo);
    }

    return failures ? 1 : 0;
}
