#include "core/r3000a_native_alu.h"
#include "core/r3000a_reference_executor.h"
#include "mips_test_encode.h"
#include "r3000a_test_bus.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

jojo::R3000aIrBlock lift(const std::array<std::uint32_t, 5>& words) {
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    return block ? block.value : jojo::R3000aIrBlock{};
}

void store_program(TestR3000aBus& bus, const std::array<std::uint32_t, 5>& words) {
    for (std::size_t i = 0; i < words.size(); ++i) {
        bus.store32(0x80010000u + static_cast<std::uint32_t>(i * 4u), words[i]);
    }
}

void check_core_state_equal(
    const jojo::R3000aState& lhs,
    const jojo::R3000aState& rhs) {
    CHECK(lhs.gpr == rhs.gpr);
    CHECK(lhs.hi == rhs.hi);
    CHECK(lhs.lo == rhs.lo);
    CHECK(lhs.pc == rhs.pc);
    CHECK(lhs.next_pc == rhs.next_pc);
    CHECK(lhs.pending_load.valid == rhs.pending_load.valid);
    CHECK(lhs.pending_load.reg == rhs.pending_load.reg);
    CHECK(lhs.pending_load.value == rhs.pending_load.value);
}

void test_native_alu_matches_reference_for_safe_block() {
    const std::array<std::uint32_t, 5> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1234u),
        test_mips::i(0x0Du, 8u, 8u, 0x00F0u),
        test_mips::i(0x09u, 8u, 9u, 0xFFFFu),
        test_mips::r(8u, 9u, 10u, 0u, 0x26u),
        test_mips::i(0x0Au, 9u, 11u, 0u),
    };
    const auto block = lift(words);

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    auto reference = native;

    const auto native_result =
        jojo::execute_r3000a_native_alu_block(block, native);
    CHECK(native_result.status == jojo::R3000aNativeAluStatus::executed);
    CHECK(native_result.instructions_retired == words.size());

    TestR3000aBus bus;
    store_program(bus, words);
    for (std::size_t i = 0; i < words.size(); ++i) {
        CHECK(jojo::step_r3000a(reference, bus).status ==
              jojo::R3000aStepStatus::retired);
    }

    check_core_state_equal(native, reference);
}

void test_native_alu_preserves_r3000a_load_delay_semantics() {
    const std::array<std::uint32_t, 5> words{
        test_mips::i(0x09u, 8u, 9u, 1u),
        test_mips::r(8u, 0u, 10u, 0u, 0x21u),
        0x00000000u,
        0x00000000u,
        0x00000000u,
    };
    const auto block = lift(words);

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    native.gpr[8] = 5u;
    native.pending_load = {true, 8u, 100u};
    auto reference = native;

    const auto native_result =
        jojo::execute_r3000a_native_alu_block(block, native);
    CHECK(native_result.status == jojo::R3000aNativeAluStatus::executed);

    TestR3000aBus bus;
    store_program(bus, words);
    for (std::size_t i = 0; i < words.size(); ++i) {
        CHECK(jojo::step_r3000a(reference, bus).status ==
              jojo::R3000aStepStatus::retired);
    }

    CHECK(native.gpr[9] == 6u);
    CHECK(native.gpr[10] == 100u);
    check_core_state_equal(native, reference);
}

void test_native_alu_falls_back_for_interrupt_or_control_state() {
    const std::array<std::uint32_t, 5> words{
        0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    };
    const auto block = lift(words);

    jojo::R3000aState state{};
    state.pc = 0x80010000u;
    state.next_pc = 0x80010004u;
    state.delay_slot.active = true;
    CHECK(jojo::execute_r3000a_native_alu_block(block, state).status ==
          jojo::R3000aNativeAluStatus::reference_required);

    state.delay_slot = {};
    state.external_interrupt_pending = 0x04u;
    state.cop0.status = 0x00000401u;
    CHECK(jojo::execute_r3000a_native_alu_block(block, state).status ==
          jojo::R3000aNativeAluStatus::reference_required);
}
} // namespace

int main() {
    test_native_alu_matches_reference_for_safe_block();
    test_native_alu_preserves_r3000a_load_delay_semantics();
    test_native_alu_falls_back_for_interrupt_or_control_state();
    return failures ? 1 : 0;
}
