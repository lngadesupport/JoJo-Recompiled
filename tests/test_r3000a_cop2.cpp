#include "core/r3000a_reference_executor.h"
#include "r3000a_test_bus.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

constexpr std::uint32_t cop2(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x12u << 26) | (std::uint32_t(rs) << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}

static jojo::R3000aState base_state(bool cu2 = true) {
    jojo::R3000aState s{};
    s.pc = 0x1000u;
    s.next_pc = 0x1004u;
    if (cu2) s.cop0.status |= 1u << 30u;
    return s;
}

int main() {
    // CU2-disabled access remains Coprocessor Unusable with CE=2.
    {
        TestR3000aBus bus;
        auto s = base_state(false);
        bus.store32(0x1000u, cop2(4u, 8u, 3u)); // MTC2 r8 -> data[3]
        s.gpr[8] = 0x12345678u;
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::coprocessor_unusable);
        CHECK(r.diagnostic.coprocessor && *r.diagnostic.coprocessor == 2u);
    }

    // MTC2 writes GTE data registers immediately when CU2 is enabled.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[8] = 0x12345678u;
        bus.store32(0x1000u, cop2(4u, 8u, 3u));
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::retired);
        CHECK(s.gte.data[3] == 0x12345678u);
        CHECK(s.pc == 0x1004u);
    }

    // CTC2 writes control registers immediately.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[9] = 0x89ABCDEFu;
        bus.store32(0x1000u, cop2(6u, 9u, 7u));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gte.control[7] == 0x89ABCDEFu);
    }

    // MFC2 observes the ordinary R3000A one-instruction load delay.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gte.data[5] = 0xCAFEBABEu;
        s.gpr[10] = 0x11111111u;
        bus.store32(0x1000u, cop2(0u, 10u, 5u));
        bus.store32(0x1004u, 0u);
        bus.store32(0x1008u, 0u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[10] == 0x11111111u);
        CHECK(s.pending_load.valid && s.pending_load.reg == 10u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[10] == 0xCAFEBABEu);
    }

    // CFC2 follows the same delayed-load path for control registers.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gte.control[12] = 0x0BADF00Du;
        bus.store32(0x1000u, cop2(2u, 11u, 12u));
        bus.store32(0x1004u, 0u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.pending_load.valid && s.pending_load.reg == 11u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[11] == 0x0BADF00Du);
    }

    // COP2 command execution remains an explicit boundary until 4B.
    {
        TestR3000aBus bus;
        auto s = base_state();
        bus.store32(0x1000u, (0x12u << 26) | (0x10u << 21) | 0x01u);
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::boundary);
        CHECK(r.diagnostic.boundary == jojo::R3000aBoundaryCode::cop2_unimplemented);
        CHECK(r.diagnostic.coprocessor && *r.diagnostic.coprocessor == 2u);
    }

    return failures ? 1 : 0;
}
