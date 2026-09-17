#include "core/r3000a_reference_executor.h"
#include "mips_test_encode.h"
#include "r3000a_test_bus.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

constexpr std::uint32_t cop2_transfer(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x12u << 26) | (std::uint32_t(rs) << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}

constexpr std::uint32_t cop2_command(std::uint32_t command) noexcept {
    return (0x12u << 26) | (0x10u << 21) | (command & 0x01FFFFFFu);
}

static jojo::R3000aState base_state() {
    jojo::R3000aState s{};
    s.pc = 0x1000u;
    s.next_pc = 0x1004u;
    return s;
}

int main() {
    constexpr std::uint32_t kStatusCu2 = 1u << 30u;

    // CU2 disabled: COP2 access is a Coprocessor Unusable exception naming CE=2.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[8] = 0x12345678u;
        bus.store32(0x1000u, cop2_transfer(0x04u, 8u, 3u)); // MTC2 r8, D3
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::coprocessor_unusable);
        CHECK(r.diagnostic.coprocessor == 2u);
    }

    // MTC2 writes the selected GTE data register immediately when CU2 is enabled.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status = kStatusCu2;
        s.gpr[8] = 0x12345678u;
        bus.store32(0x1000u, cop2_transfer(0x04u, 8u, 3u));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gte.data[3] == 0x12345678u);
        CHECK(s.pc == 0x1004u);
    }

    // CTC2 writes the selected GTE control register immediately.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status = kStatusCu2;
        s.gpr[9] = 0x89ABCDEFu;
        bus.store32(0x1000u, cop2_transfer(0x06u, 9u, 7u));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gte.control[7] == 0x89ABCDEFu);
    }

    // MFC2 follows the ordinary R3000A one-instruction load delay.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status = kStatusCu2;
        s.gte.data[5] = 0xCAFEBABEu;
        s.gpr[8] = 0x11111111u;
        bus.store32(0x1000u, cop2_transfer(0x00u, 8u, 5u)); // MFC2 r8, D5
        bus.store32(0x1004u, test_mips::r(8, 0, 9, 0, 0x21));
        bus.store32(0x1008u, test_mips::r(8, 0, 10, 0, 0x21));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[8] == 0x11111111u && s.pending_load.valid);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[9] == 0x11111111u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[10] == 0xCAFEBABEu);
    }

    // CFC2 also uses the ordinary load delay.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status = kStatusCu2;
        s.gte.control[11] = 0x0BADF00Du;
        s.gpr[12] = 0x22222222u;
        bus.store32(0x1000u, cop2_transfer(0x02u, 12u, 11u));
        bus.store32(0x1004u, 0u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[12] == 0x22222222u && s.pending_load.valid);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[12] == 0x0BADF00Du);
    }

    // Commands remain an explicit frontier in 4A; transfer support must not hide them.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status = kStatusCu2;
        const auto raw = cop2_command(0x01u);
        bus.store32(0x1000u, raw);
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::boundary);
        CHECK(r.diagnostic.boundary == jojo::R3000aBoundaryCode::cop2_unimplemented);
        CHECK(r.diagnostic.stage == jojo::R3000aStage::cop2);
        CHECK(r.diagnostic.coprocessor == 2u);
        CHECK(r.diagnostic.opcode == raw);
    }

    return failures ? 1 : 0;
}
