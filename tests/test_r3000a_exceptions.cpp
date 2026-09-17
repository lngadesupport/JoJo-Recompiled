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

static void check_exception(
    const jojo::R3000aStepResult& result,
    jojo::R3000aExceptionCode expected) {
    CHECK(result.status == jojo::R3000aStepStatus::exception);
    CHECK(result.diagnostic.exception_code.has_value());
    if (result.diagnostic.exception_code) {
        CHECK(*result.diagnostic.exception_code == expected);
    }
}

int main() {
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0x7fffffffu;
        s.gpr[2] = 1u;
        const auto result = run_one(s, bus, test_mips::r(1, 2, 3, 0, 0x20));
        check_exception(result, jojo::R3000aExceptionCode::overflow);
        CHECK(s.gpr[3] == 0u);
        CHECK(s.cop0.epc == 0x1000u);
        CHECK(s.pc == 0x80000080u && s.next_pc == 0x80000084u);
        CHECK(((s.cop0.cause >> 2) & 0x1fu) == 12u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0x7fffffffu;
        const auto result = run_one(s, bus, test_mips::i(0x08, 1, 2, 1u));
        check_exception(result, jojo::R3000aExceptionCode::overflow);
        CHECK(s.gpr[2] == 0u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 0x80000000u;
        s.gpr[2] = 1u;
        const auto result = run_one(s, bus, test_mips::r(1, 2, 3, 0, 0x22));
        check_exception(result, jojo::R3000aExceptionCode::overflow);
        CHECK(s.gpr[3] == 0u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status = 0x00000003u;
        const auto result = run_one(s, bus, 0x0000000Cu);
        check_exception(result, jojo::R3000aExceptionCode::syscall);
        CHECK((s.cop0.status & 0x3fu) == 0x0cu);
        CHECK(s.cop0.epc == 0x1000u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        check_exception(run_one(s, bus, 0x0000000Du), jojo::R3000aExceptionCode::breakpoint);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        const auto result = run_one(s, bus, 0x70000000u);
        check_exception(result, jojo::R3000aExceptionCode::reserved_instruction);
        CHECK(result.diagnostic.opcode && *result.diagnostic.opcode == 0x70000000u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.pc = 0x1002u;
        s.next_pc = 0x1006u;
        const auto result = jojo::step_r3000a(s, bus);
        check_exception(result, jojo::R3000aExceptionCode::adel);
        CHECK(s.cop0.bad_vaddr == 0x1002u);
        CHECK(s.cop0.epc == 0x1002u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        bus.fail_bus_error(0x1000u);
        const auto result = jojo::step_r3000a(s, bus);
        check_exception(result, jojo::R3000aExceptionCode::ibe);
        CHECK(s.cop0.epc == 0x1000u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.cop0.status = 1u << 22;
        const auto result = run_one(s, bus, 0x0000000Cu);
        check_exception(result, jojo::R3000aExceptionCode::syscall);
        CHECK(s.pc == 0xBFC00180u && s.next_pc == 0xBFC00184u);
    }
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[0] = 0xDEADBEEFu;
        check_exception(run_one(s, bus, 0x70000000u), jojo::R3000aExceptionCode::reserved_instruction);
        CHECK(s.gpr[0] == 0u);
    }

    // Taken branch followed by a faulting delay-slot instruction.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u;
        bus.store32(0x1000u, test_mips::i(0x04, 1, 1, 2)); // target 0x100C
        bus.store32(0x1004u, 0x0000000Cu); // SYSCALL in delay slot
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        const auto result = jojo::step_r3000a(s, bus);
        check_exception(result, jojo::R3000aExceptionCode::syscall);
        CHECK((s.cop0.cause & 0x80000000u) != 0u); // BD
        CHECK((s.cop0.cause & 0x40000000u) != 0u); // BT
        CHECK(s.cop0.epc == 0x1000u);
        CHECK(s.cop0.target_address == 0x100Cu);
    }

    // Not-taken branch still has a delay slot, but BT/TAR are not promoted.
    {
        TestR3000aBus bus;
        auto s = base_state();
        s.gpr[1] = 1u;
        s.gpr[2] = 2u;
        s.cop0.target_address = 0xDEADBEEFu;
        bus.store32(0x1000u, test_mips::i(0x04, 1, 2, 2));
        bus.store32(0x1004u, 0x0000000Cu);
        CHECK(jojo::step_r3000a(s, bus).status == jojo::R3000aStepStatus::retired);
        const auto result = jojo::step_r3000a(s, bus);
        check_exception(result, jojo::R3000aExceptionCode::syscall);
        CHECK((s.cop0.cause & 0x80000000u) != 0u);
        CHECK((s.cop0.cause & 0x40000000u) == 0u);
        CHECK(s.cop0.epc == 0x1000u);
        CHECK(s.cop0.target_address == 0xDEADBEEFu);
    }

    return failures ? 1 : 0;
}
