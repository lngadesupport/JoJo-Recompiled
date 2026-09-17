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

static void store_delay_nop(TestR3000aBus& bus) {
    bus.store32(0x1004u, 0u); // SLL $zero,$zero,0
}

static void check_scheduled(
    jojo::R3000aState& s,
    TestR3000aBus& bus,
    std::uint32_t instruction,
    bool taken,
    bool link = false) {
    bus.store32(0x1000u, instruction);
    store_delay_nop(bus);
    const auto result = jojo::step_r3000a(s, bus);
    CHECK(result.status == jojo::R3000aStepStatus::retired);
    CHECK(s.pc == 0x1004u);
    CHECK(s.next_pc == (taken ? 0x100Cu : 0x1008u));
    CHECK(s.delay_slot.active);
    CHECK(s.delay_slot.branch_pc == 0x1000u);
    CHECK(s.delay_slot.taken == taken);
    CHECK(s.delay_slot.target == 0x100Cu);
    if (link) CHECK(s.gpr[31] == 0x1008u);
}

int main() {
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 7u;
        s.gpr[2] = 10u;
        bus.store32(0x1000u, test_mips::i(0x04, 1, 1, 2)); // BEQ taken
        bus.store32(0x1004u, test_mips::i(0x09, 2, 2, 1)); // delay: ADDIU $2,$2,1
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.pc == 0x1004u && s.next_pc == 0x100Cu && s.delay_slot.active);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[2] == 11u);
        CHECK(s.pc == 0x100Cu && s.next_pc == 0x1010u);
        CHECK(!s.delay_slot.active);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u;
        s.gpr[2] = 2u;
        check_scheduled(s, bus, test_mips::i(0x04, 1, 2, 2), false); // BEQ not taken
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.pc == 0x1008u && !s.delay_slot.active);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u; s.gpr[2] = 2u;
        check_scheduled(s, bus, test_mips::i(0x05, 1, 2, 2), true); // BNE
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFFu;
        check_scheduled(s, bus, test_mips::i(0x06, 1, 0, 2), true); // BLEZ
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u;
        check_scheduled(s, bus, test_mips::i(0x07, 1, 0, 2), true); // BGTZ
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFFu;
        check_scheduled(s, bus, test_mips::i(0x01, 1, 0x00, 2), true); // BLTZ
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0u;
        check_scheduled(s, bus, test_mips::i(0x01, 1, 0x01, 2), true); // BGEZ
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u; // BLTZAL condition false; link still occurs
        check_scheduled(s, bus, test_mips::i(0x01, 1, 0x10, 2), false, true);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0xFFFFFFFFu; // BGEZAL condition false; link still occurs
        check_scheduled(s, bus, test_mips::i(0x01, 1, 0x11, 2), false, true);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        bus.store32(0x1000u, test_mips::j(0x02, 0x2000u >> 2)); // J 0x2000
        store_delay_nop(bus);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.pc == 0x1004u && s.next_pc == 0x2000u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.pc == 0x2000u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        bus.store32(0x1000u, test_mips::j(0x03, 0x2000u >> 2)); // JAL
        store_delay_nop(bus);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[31] == 0x1008u);
        CHECK(s.next_pc == 0x2000u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[5] = 0x3000u;
        bus.store32(0x1000u, test_mips::r(5, 0, 0, 0, 0x08)); // JR $5
        store_delay_nop(bus);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.next_pc == 0x3000u);
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[5] = 0x3000u;
        bus.store32(0x1000u, test_mips::r(5, 0, 5, 0, 0x09)); // JALR $5,$5
        store_delay_nop(bus);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.next_pc == 0x3000u); // pre-write target
        CHECK(s.gpr[5] == 0x1008u); // link write
    }

    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u;
        bus.store32(0x1000u, test_mips::i(0x04, 1, 1, 2));
        bus.store32(0x1004u, test_mips::j(0x02, 0x2000u >> 2));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        const auto nested = jojo::step_r3000a(s, bus);
        CHECK(nested.status == jojo::R3000aStepStatus::boundary);
        CHECK(nested.diagnostic.boundary == jojo::R3000aBoundaryCode::unpredictable_delay_slot_control_transfer);
        CHECK(s.pc == 0x1004u);
    }

    return failures ? 1 : 0;
}
