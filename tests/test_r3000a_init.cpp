#include "core/ps1_exe.h"
#include "core/r3000a_reference_executor.h"
#include "mips_test_encode.h"
#include "r3000a_test_bus.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static void check_zero_cop0(const jojo::R3000aState& s) {
    CHECK(s.cop0.target_address == 0u);
    CHECK(s.cop0.bad_vaddr == 0u);
    CHECK(s.cop0.status == 0u);
    CHECK(s.cop0.cause == 0u);
    CHECK(s.cop0.epc == 0u);
}

constexpr std::uint32_t cop0(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x10u << 26) |
           (std::uint32_t(rs) << 21) |
           (std::uint32_t(rt) << 16) |
           (std::uint32_t(rd) << 11);
}

static bool same_state(const jojo::R3000aState& a, const jojo::R3000aState& b) {
    return a.gpr == b.gpr &&
           a.hi == b.hi &&
           a.lo == b.lo &&
           a.pc == b.pc &&
           a.next_pc == b.next_pc &&
           a.pending_load.valid == b.pending_load.valid &&
           a.pending_load.reg == b.pending_load.reg &&
           a.pending_load.value == b.pending_load.value &&
           a.delay_slot.active == b.delay_slot.active &&
           a.delay_slot.branch_pc == b.delay_slot.branch_pc &&
           a.delay_slot.taken == b.delay_slot.taken &&
           a.delay_slot.target == b.delay_slot.target &&
           a.cop0.target_address == b.cop0.target_address &&
           a.cop0.bad_vaddr == b.cop0.bad_vaddr &&
           a.cop0.status == b.cop0.status &&
           a.cop0.cause == b.cop0.cause &&
           a.cop0.epc == b.cop0.epc &&
           a.external_interrupt_pending == b.external_interrupt_pending;
}

static void run_deterministic_sequence(jojo::R3000aState& s, TestR3000aBus& bus) {
    for (int step = 0; step < 9; ++step) {
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::retired);
    }
}

int main() {
    {
        jojo::Ps1ExeMetadata m{};
        m.entry_pc = 0x80010000u;
        m.initial_gp = 0x80018000u;
        m.stack_base = 0x801FFF00u;
        m.stack_size = 0x100u;

        const auto s = jojo::initialize_r3000a_for_psx_exe(m);
        CHECK(s.pc == 0x80010000u);
        CHECK(s.next_pc == 0x80010004u);
        CHECK(s.gpr[28] == 0x80018000u);
        CHECK(s.gpr[29] == 0x80200000u);
        CHECK(s.gpr[0] == 0u);
        for (std::size_t i = 1; i < s.gpr.size(); ++i) {
            if (i == 28u || i == 29u) continue;
            CHECK(s.gpr[i] == 0u);
        }
        CHECK(s.hi == 0u && s.lo == 0u);
        CHECK(!s.pending_load.valid);
        CHECK(!s.delay_slot.active);
        CHECK(s.external_interrupt_pending == 0u);
        check_zero_cop0(s);
    }

    // A PS-X EXE that does not request an initial stack leaves SP at zero.
    {
        jojo::Ps1ExeMetadata m{};
        m.entry_pc = 0x80010000u;
        m.initial_gp = 0x80018000u;
        m.stack_base = 0u;
        m.stack_size = 0u;

        const auto s = jojo::initialize_r3000a_for_psx_exe(m);
        CHECK(s.gpr[29] == 0u);
        CHECK(s.gpr[28] == 0x80018000u);
        CHECK(s.pc == m.entry_pc && s.next_pc == m.entry_pc + 4u);
        CHECK(!s.pending_load.valid && !s.delay_slot.active);
        CHECK(s.hi == 0u && s.lo == 0u);
        check_zero_cop0(s);
    }

    // Identical cloned CPU/bus inputs must replay to an identical architectural state.
    // The sequence deliberately crosses load delay, ALU consumption, HI/LO, a taken
    // branch with delay slot, and a non-faulting COP0 read.
    {
        jojo::R3000aState initial{};
        initial.pc = 0x1000u;
        initial.next_pc = 0x1004u;
        initial.gpr[1] = 3u;
        initial.gpr[2] = 4u;
        initial.gpr[4] = 0x2000u;
        initial.gpr[8] = 5u;
        initial.cop0.status = 0x00000400u;
        initial.cop0.cause = 0x00000100u;

        TestR3000aBus base_bus;
        base_bus.store32(0x2000u, 0x11223344u);
        base_bus.store32(0x1000u, test_mips::i(0x23, 4, 8, 0));           // LW r8,0(r4)
        base_bus.store32(0x1004u, test_mips::r(8, 1, 9, 0, 0x21));        // ADDU r9,r8,r1 (stale r8)
        base_bus.store32(0x1008u, test_mips::r(1, 2, 0, 0, 0x19));        // MULTU r1,r2
        base_bus.store32(0x100Cu, test_mips::r(0, 0, 10, 0, 0x12));       // MFLO r10
        base_bus.store32(0x1010u, test_mips::i(0x04, 9, 9, 2));           // BEQ -> 0x101C
        base_bus.store32(0x1014u, test_mips::i(0x09, 0, 11, 7));          // delay slot
        base_bus.store32(0x1018u, test_mips::i(0x09, 0, 15, 99));         // skipped
        base_bus.store32(0x101Cu, cop0(0u, 12u, 12u));                    // MFC0 r12,Status
        base_bus.store32(0x1020u, test_mips::r(12, 1, 13, 0, 0x21));      // stale r12 consumer
        base_bus.store32(0x1024u, test_mips::r(12, 2, 14, 0, 0x21));      // visible r12 consumer

        auto state_a = initial;
        auto state_b = initial;
        auto bus_a = base_bus;
        auto bus_b = base_bus;

        run_deterministic_sequence(state_a, bus_a);
        run_deterministic_sequence(state_b, bus_b);

        CHECK(same_state(state_a, state_b));
        CHECK(state_a.gpr[8] == 0x11223344u);
        CHECK(state_a.gpr[9] == 8u);
        CHECK(state_a.lo == 12u && state_a.gpr[10] == 12u);
        CHECK(state_a.gpr[11] == 7u && state_a.gpr[15] == 0u);
        CHECK(state_a.gpr[12] == 0x00000400u);
        CHECK(state_a.gpr[13] == 3u);
        CHECK(state_a.gpr[14] == 0x00000404u);
        CHECK(state_a.pc == 0x1028u && state_a.next_pc == 0x102Cu);
        CHECK(!state_a.pending_load.valid && !state_a.delay_slot.active);
    }

    return failures ? 1 : 0;
}
