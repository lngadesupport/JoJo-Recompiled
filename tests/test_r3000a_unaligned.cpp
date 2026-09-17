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
    s.gpr[4] = 0x2000u;
    s.gpr[8] = 0xAABBCCDDu;
    return s;
}

static std::uint32_t run_merge_load(std::uint8_t opcode, std::uint32_t offset) {
    TestR3000aBus bus;
    auto s = base_state();
    bus.store32(0x2000u, 0x44332211u);
    bus.store32(0x1000u, test_mips::i(opcode, 4, 8, static_cast<std::uint16_t>(offset)));
    bus.store32(0x1004u, 0u);
    CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
    CHECK(s.pending_load.valid);
    CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
    return s.gpr[8];
}

static std::uint32_t run_merge_store(std::uint8_t opcode, std::uint32_t offset) {
    TestR3000aBus bus;
    auto s = base_state();
    bus.store32(0x2000u, 0x44332211u);
    bus.store32(0x1000u, test_mips::i(opcode, 4, 8, static_cast<std::uint16_t>(offset)));
    CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
    return bus.peek32(0x2000u);
}

int main() {
    const std::uint32_t lwl[4] = {0x11BBCCDDu, 0x2211CCDDu, 0x332211DDu, 0x44332211u};
    const std::uint32_t lwr[4] = {0x44332211u, 0xAA443322u, 0xAABB4433u, 0xAABBCC44u};
    const std::uint32_t swl[4] = {0x443322AAu, 0x4433AABBu, 0x44AABBCCu, 0xAABBCCDDu};
    const std::uint32_t swr[4] = {0xAABBCCDDu, 0xBBCCDD11u, 0xCCDD2211u, 0xDD332211u};

    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        CHECK(run_merge_load(0x22, lane) == lwl[lane]);
        CHECK(run_merge_load(0x26, lane) == lwr[lane]);
        CHECK(run_merge_store(0x2A, lane) == swl[lane]);
        CHECK(run_merge_store(0x2E, lane) == swr[lane]);
    }

    // Only the second merge load receives forwarding from the first pending merge.
    // Ordinary ALU consumers still observe the visible GPR according to normal load delay.
    {
        TestR3000aBus bus;
        auto s = base_state();
        bus.store32(0x2000u, 0x44332211u);
        bus.store32(0x1000u, test_mips::i(0x22, 4, 8, 1)); // LWL -> 0x2211CCDD pending
        bus.store32(0x1004u, test_mips::i(0x26, 4, 8, 2)); // LWR merges from pending -> 0x22114433
        bus.store32(0x1008u, test_mips::r(8, 0, 9, 0, 0x21));
        bus.store32(0x100Cu, test_mips::r(8, 0, 10, 0, 0x21));

        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[8] == 0xAABBCCDDu && s.pending_load.value == 0x2211CCDDu);

        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[8] == 0x2211CCDDu);
        CHECK(s.pending_load.valid && s.pending_load.value == 0x22114433u);

        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[9] == 0x2211CCDDu);
        CHECK(s.gpr[8] == 0x22114433u);

        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[10] == 0x22114433u);
    }

    // Merge operations use an aligned 32-bit bus access, not the unaligned effective address.
    {
        TestR3000aBus bus;
        auto s = base_state();
        bus.store32(0x2000u, 0x44332211u);
        bus.store32(0x1000u, test_mips::i(0x22, 4, 8, 1));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(bus.last_read32_address() == 0x2000u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        bus.store32(0x2000u, 0x44332211u);
        bus.store32(0x1000u, test_mips::i(0x2A, 4, 8, 2));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(bus.last_read32_address() == 0x2000u);
        CHECK(bus.last_write32_address() == 0x2000u);
    }

    return failures ? 1 : 0;
}
