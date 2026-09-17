#include "core/r3000a_reference_executor.h"
#include "mips_test_encode.h"
#include "r3000a_test_bus.h"

#include <array>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

constexpr std::uint32_t cop0(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd) noexcept {
    return (0x10u << 26) | (std::uint32_t(rs) << 21) |
           (std::uint32_t(rt) << 16) | (std::uint32_t(rd) << 11);
}

static jojo::R3000aState base_state() {
    jojo::R3000aState s{};
    s.pc = 0x1000u;
    s.next_pc = 0x1004u;
    return s;
}

static void put_nop(TestR3000aBus& bus, std::uint32_t pc) { bus.store32(pc, 0u); }

int main() {
    constexpr std::uint32_t kStatusWritableMask = 0xF27FFF3Fu;
    constexpr std::uint32_t kCauseSwMask = 0x00000300u;
    constexpr std::uint32_t kCauseExternalMask = 0x0000FC00u;

    // MFC0 values are delivered through the ordinary one-instruction GPR load delay.
    {
        struct ReadCase { std::uint8_t rd; std::uint32_t value; };
        const ReadCase cases[] = {
            {6u, 0x11111111u}, {8u, 0x22222222u}, {12u, 0x33333333u},
            {13u, 0x44440344u}, {14u, 0x55555555u},
        };
        for (const auto& c : cases) {
            TestR3000aBus bus;
            auto s = base_state();
            s.gpr[8] = 0xAAAAAAAAu;
            if (c.rd == 6u) s.cop0.target_address = c.value;
            if (c.rd == 8u) s.cop0.bad_vaddr = c.value;
            if (c.rd == 12u) s.cop0.status = c.value;
            if (c.rd == 13u) s.cop0.cause = c.value;
            if (c.rd == 14u) s.cop0.epc = c.value;
            bus.store32(0x1000u, cop0(0u, 8u, c.rd));
            bus.store32(0x1004u, test_mips::r(8, 0, 9, 0, 0x21));
            bus.store32(0x1008u, test_mips::r(8, 0, 10, 0, 0x21));
            CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
            CHECK(s.gpr[8] == 0xAAAAAAAAu && s.pending_load.valid);
            CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
            CHECK(s.gpr[9] == 0xAAAAAAAAu);
            CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
            CHECK(s.gpr[10] == c.value);
        }
    }

    // Status and Cause writes are masked exactly to the R3000A/PS1 writable subset.
    {
        TestR3000aBus bus;
        auto s = base_state();
        const std::uint32_t before = 0x0D8000C0u;
        s.cop0.status = before;
        s.gpr[8] = 0xFFFFFFFFu;
        bus.store32(0x1000u, cop0(4u, 8u, 12u));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.cop0.status == ((before & ~kStatusWritableMask) | kStatusWritableMask));
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        const std::uint32_t before = 0x8F00FC7Cu;
        const std::uint32_t synced_before = before & ~kCauseExternalMask;
        s.cop0.cause = before;
        s.gpr[8] = 0x00000100u;
        bus.store32(0x1000u, cop0(4u, 8u, 13u));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.cop0.cause == ((synced_before & ~kCauseSwMask) | 0x00000100u));
    }

    // TAR, BadVAddr and EPC are readable but not writable in M2.
    for (const std::uint8_t rd : {std::uint8_t{6}, std::uint8_t{8}, std::uint8_t{14}}) {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.target_address = 0x11111111u;
        s.cop0.bad_vaddr = 0x22222222u;
        s.cop0.epc = 0x33333333u;
        s.gpr[8] = 0xDEADBEEFu;
        bus.store32(0x1000u, cop0(4u, 8u, rd));
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::boundary);
        CHECK(r.diagnostic.boundary == jojo::R3000aBoundaryCode::architectural_operation_unimplemented);
        CHECK(s.cop0.target_address == 0x11111111u);
        CHECK(s.cop0.bad_vaddr == 0x22222222u);
        CHECK(s.cop0.epc == 0x33333333u);
    }

    // Architecturally absent COP0 register encodings are RI; known PS1 surfaces outside M2 are boundaries.
    for (const std::uint8_t rd : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{10}}) {
        TestR3000aBus bus;
        auto s = base_state();
        bus.store32(0x1000u, cop0(0u, 8u, rd));
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::reserved_instruction);
    }
    for (const std::uint8_t rd : {std::uint8_t{3}, std::uint8_t{5}, std::uint8_t{7}, std::uint8_t{9},
                                  std::uint8_t{11}, std::uint8_t{15}, std::uint8_t{16}, std::uint8_t{31}}) {
        TestR3000aBus bus;
        auto s = base_state();
        bus.store32(0x1000u, cop0(0u, 8u, rd));
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::boundary);
        CHECK(r.diagnostic.boundary == jojo::R3000aBoundaryCode::architectural_operation_unimplemented);
    }

    // RFE restores only the low six mode-stack bits.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status = 0xA540002Du;
        const auto before_high = s.cop0.status & ~0x3Fu;
        const auto low = s.cop0.status & 0x3Fu;
        const auto expected = (low & 0x30u) | ((low >> 2) & 0x0Fu);
        bus.store32(0x1000u, 0x42000010u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK((s.cop0.status & ~0x3Fu) == before_high);
        CHECK((s.cop0.status & 0x3Fu) == expected);
    }

    // External lines 2..7 synchronize to Cause.IP10..15 while software bits 8..9 survive.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.cause = 0x00000100u;
        s.external_interrupt_pending = 0x04u;
        s.cop0.status = 1u | 0x00000400u; // IEc + matching IP10 mask
        put_nop(bus, 0x1000u);
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::interrupt);
        CHECK((s.cop0.cause & 0x0000FF00u) == 0x00000500u);
        CHECK(s.cop0.epc == 0x1000u);
    }

    // Pending-but-masked interrupt does not fire.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.external_interrupt_pending = 0x04u;
        s.cop0.status = 1u; // IEc, no IP mask
        put_nop(bus, 0x1000u);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.pc == 0x1004u);
    }

    // An active branch delay slot cannot be split by an interrupt.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u;
        bus.store32(0x1000u, test_mips::i(0x04, 1, 1, 2));
        bus.store32(0x1004u, test_mips::i(0x09, 0, 2, 7));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        s.external_interrupt_pending = 0x04u;
        s.cop0.status = 1u | 0x00000400u;
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.gpr[2] == 7u && s.pc == 0x100Cu);
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(r.diagnostic.exception_code == jojo::R3000aExceptionCode::interrupt);
        CHECK(s.cop0.epc == 0x100Cu);
    }

    // An older pending GPR load retires before interrupt handler entry.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[4] = 0x2000u;
        s.gpr[8] = 0x11111111u;
        bus.store32(0x2000u, 0xCAFEBABEu);
        bus.store32(0x1000u, test_mips::i(0x23, 4, 8, 0));
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        CHECK(s.pending_load.valid);
        s.external_interrupt_pending = 0x04u;
        s.cop0.status = 1u | 0x00000400u;
        const auto r = jojo::step_r3000a(s, bus);
        CHECK(r.status == jojo::R3000aStepStatus::exception);
        CHECK(s.gpr[8] == 0xCAFEBABEu && !s.pending_load.valid);
    }

    return failures ? 1 : 0;
}
