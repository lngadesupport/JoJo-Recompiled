#include "core/r3000a_reference_executor.h"
#include "core/r3000a_x64.h"
#include "mips_test_encode.h"
#include "r3000a_test_bus.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)


void test_direct_branch_instruction_is_lowerable() {
    const auto decoded =
        jojo::decode_mips(test_mips::i(0x04u, 8u, 9u, 1u));
    CHECK(jojo::r3000a_op_is_x64_direct_lowerable(decoded.op));
    const auto code =
        jojo::emit_r3000a_x64_instruction(0x80010000u, decoded);
    CHECK(code);
    if (code) CHECK(code.value.instruction_count == 1u);
}


#if defined(_WIN32) && defined(_M_X64)
void test_x64_direct_branch_not_taken_matches_reference() {
    const auto raw = test_mips::i(0x05u, 8u, 9u, 2u); // BNE not taken
    const auto decoded = jojo::decode_mips(raw);
    const auto code =
        jojo::emit_r3000a_x64_instruction(0x80010000u, decoded);
    CHECK(code);
    if (!code) return;

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    native.gpr[8] = 5u;
    native.gpr[9] = 5u;
    auto reference = native;

    const auto executed =
        jojo::execute_r3000a_x64_block(code.value, native);
    CHECK(executed.status == jojo::R3000aX64ExecutionStatus::executed);

    TestR3000aBus bus;
    bus.store32(0x80010000u, raw);
    CHECK(jojo::step_r3000a(reference, bus).status ==
          jojo::R3000aStepStatus::retired);

    CHECK(native.pc == reference.pc);
    CHECK(native.next_pc == reference.next_pc);
    CHECK(native.delay_slot.taken == reference.delay_slot.taken);
    CHECK(native.delay_slot.target == reference.delay_slot.target);
}

void test_x64_direct_jal_matches_reference() {
    const auto raw = test_mips::j(0x03u, 0x80012000u >> 2u);
    const auto decoded = jojo::decode_mips(raw);
    const auto code =
        jojo::emit_r3000a_x64_instruction(0x80010000u, decoded);
    CHECK(code);
    if (!code) return;

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    auto reference = native;

    CHECK(jojo::execute_r3000a_x64_block(code.value, native).status ==
          jojo::R3000aX64ExecutionStatus::executed);

    TestR3000aBus bus;
    bus.store32(0x80010000u, raw);
    CHECK(jojo::step_r3000a(reference, bus).status ==
          jojo::R3000aStepStatus::retired);

    CHECK(native.gpr == reference.gpr);
    CHECK(native.pc == reference.pc);
    CHECK(native.next_pc == reference.next_pc);
    CHECK(native.delay_slot.active == reference.delay_slot.active);
    CHECK(native.delay_slot.taken == reference.delay_slot.taken);
    CHECK(native.delay_slot.target == reference.delay_slot.target);
}
#endif

#if defined(_WIN32) && defined(_M_X64)
void test_x64_direct_branch_matches_reference() {
    const auto raw = test_mips::i(0x04u, 8u, 9u, 1u); // BEQ -> +8
    const auto decoded = jojo::decode_mips(raw);
    const auto code =
        jojo::emit_r3000a_x64_instruction(0x80010000u, decoded);
    CHECK(code);
    if (!code) return;

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    native.gpr[8] = 7u;
    native.gpr[9] = 7u;
    auto reference = native;

    const auto executed =
        jojo::execute_r3000a_x64_block(code.value, native);
    CHECK(executed.status == jojo::R3000aX64ExecutionStatus::executed);
    CHECK(executed.instructions_retired == 1u);

    TestR3000aBus bus;
    bus.store32(0x80010000u, raw);
    CHECK(jojo::step_r3000a(reference, bus).status ==
          jojo::R3000aStepStatus::retired);

    CHECK(native.gpr == reference.gpr);
    CHECK(native.pc == reference.pc);
    CHECK(native.next_pc == reference.next_pc);
    CHECK(native.delay_slot.active == reference.delay_slot.active);
    CHECK(native.delay_slot.branch_pc == reference.delay_slot.branch_pc);
    CHECK(native.delay_slot.taken == reference.delay_slot.taken);
    CHECK(native.delay_slot.target == reference.delay_slot.target);
}

void test_x64_direct_jalr_reads_target_before_link_write() {
    const auto raw = test_mips::r(8u, 0u, 8u, 0u, 0x09u); // JALR $8,$8
    const auto decoded = jojo::decode_mips(raw);
    const auto code =
        jojo::emit_r3000a_x64_instruction(0x80010000u, decoded);
    CHECK(code);
    if (!code) return;

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    native.gpr[8] = 0x80012000u;
    auto reference = native;

    const auto executed =
        jojo::execute_r3000a_x64_block(code.value, native);
    CHECK(executed.status == jojo::R3000aX64ExecutionStatus::executed);

    TestR3000aBus bus;
    bus.store32(0x80010000u, raw);
    CHECK(jojo::step_r3000a(reference, bus).status ==
          jojo::R3000aStepStatus::retired);

    CHECK(native.gpr == reference.gpr);
    CHECK(native.pc == reference.pc);
    CHECK(native.next_pc == reference.next_pc);
    CHECK(native.delay_slot.target == reference.delay_slot.target);
}
#endif

void test_x64_emitter_accepts_only_v0_safe_subset() {
    const std::array<std::uint32_t, 4> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1234u),
        test_mips::i(0x0Du, 8u, 8u, 0x00F0u),
        test_mips::i(0x09u, 8u, 9u, 0xFFFFu),
        test_mips::r(8u, 9u, 10u, 0u, 0x26u),
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    const auto code = jojo::emit_r3000a_x64_alu_block(block.value);
    CHECK(code);
    if (code) {
        CHECK(!code.value.bytes.empty());
        CHECK(code.value.instruction_count == words.size());
        CHECK(code.value.bytes.back() == 0xC3u);
#if defined(_WIN32) && defined(_M_X64)
        CHECK(code.value.executable_owner != nullptr);
        CHECK(code.value.executable_entry != nullptr);
#else
        CHECK(code.value.executable_owner == nullptr);
        CHECK(code.value.executable_entry == nullptr);
#endif
    }

    const std::array<std::uint32_t, 1> variable_shift{
        test_mips::r(8u, 9u, 10u, 0u, 0x04u),
    };
    const auto variable_block =
        jojo::lift_r3000a_basic_block(0x80010000u, variable_shift);
    CHECK(variable_block);
    if (variable_block) {
        const auto supported =
            jojo::emit_r3000a_x64_alu_block(variable_block.value);
        CHECK(supported);
    }
}



#if defined(_WIN32) && defined(_M_X64)
void test_x64_mult_and_multu_match_reference() {
    const std::array<std::uint32_t, 6> words{
        test_mips::r(8u, 9u, 0u, 0u, 0x18u),   // MULT
        test_mips::r(0u, 0u, 10u, 0u, 0x12u),  // MFLO
        test_mips::r(0u, 0u, 11u, 0u, 0x10u),  // MFHI
        test_mips::r(8u, 9u, 0u, 0u, 0x19u),   // MULTU
        test_mips::r(0u, 0u, 12u, 0u, 0x12u),  // MFLO
        test_mips::r(0u, 0u, 13u, 0u, 0x10u),  // MFHI
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    const auto code = jojo::emit_r3000a_x64_alu_block(block.value);
    CHECK(code);
    if (!code) return;

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    native.gpr[8] = 0xFFFFFFFEu;
    native.gpr[9] = 3u;
    auto reference = native;

    const auto executed =
        jojo::execute_r3000a_x64_block(code.value, native);
    CHECK(executed.status == jojo::R3000aX64ExecutionStatus::executed);
    CHECK(executed.instructions_retired == words.size());

    TestR3000aBus bus;
    for (std::size_t i = 0; i < words.size(); ++i) {
        bus.store32(
            0x80010000u + static_cast<std::uint32_t>(i * 4u),
            words[i]);
        CHECK(jojo::step_r3000a(reference, bus).status ==
              jojo::R3000aStepStatus::retired);
    }

    CHECK(native.gpr == reference.gpr);
    CHECK(native.hi == reference.hi);
    CHECK(native.lo == reference.lo);
    CHECK(native.pc == reference.pc);
    CHECK(native.next_pc == reference.next_pc);
}
#endif

#if defined(_WIN32) && defined(_M_X64)
void test_x64_variable_shifts_and_hilo_match_reference() {
    const std::array<std::uint32_t, 7> words{
        test_mips::r(8u, 9u, 10u, 0u, 0x04u),  // SLLV
        test_mips::r(8u, 9u, 11u, 0u, 0x06u),  // SRLV
        test_mips::r(8u, 12u, 13u, 0u, 0x07u), // SRAV
        test_mips::r(10u, 0u, 0u, 0u, 0x11u),  // MTHI
        test_mips::r(0u, 0u, 14u, 0u, 0x10u),  // MFHI
        test_mips::r(11u, 0u, 0u, 0u, 0x13u),  // MTLO
        test_mips::r(0u, 0u, 15u, 0u, 0x12u),  // MFLO
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    const auto code = jojo::emit_r3000a_x64_alu_block(block.value);
    CHECK(code);
    if (!code) return;

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    native.gpr[8] = 3u;
    native.gpr[9] = 0x80000010u;
    native.gpr[12] = 0x80000000u;
    auto reference = native;

    const auto executed =
        jojo::execute_r3000a_x64_block(code.value, native);
    CHECK(executed.status == jojo::R3000aX64ExecutionStatus::executed);
    CHECK(executed.instructions_retired == words.size());

    TestR3000aBus bus;
    for (std::size_t i = 0; i < words.size(); ++i) {
        bus.store32(
            0x80010000u + static_cast<std::uint32_t>(i * 4u),
            words[i]);
        CHECK(jojo::step_r3000a(reference, bus).status ==
              jojo::R3000aStepStatus::retired);
    }

    CHECK(native.gpr == reference.gpr);
    CHECK(native.hi == reference.hi);
    CHECK(native.lo == reference.lo);
    CHECK(native.pc == reference.pc);
    CHECK(native.next_pc == reference.next_pc);
}
#endif

#if defined(_WIN32) && defined(_M_X64)
void test_x64_machine_code_matches_reference_executor() {
    const std::array<std::uint32_t, 4> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1234u),
        test_mips::i(0x0Du, 8u, 8u, 0x00F0u),
        test_mips::i(0x09u, 8u, 9u, 0xFFFFu),
        test_mips::r(8u, 9u, 10u, 0u, 0x26u),
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    const auto code = jojo::emit_r3000a_x64_alu_block(block.value);
    CHECK(code);
    if (!code) return;

    jojo::R3000aState native{};
    native.pc = 0x80010000u;
    native.next_pc = 0x80010004u;
    auto reference = native;

    const auto entry_before = code.value.executable_entry;
    const auto executed =
        jojo::execute_r3000a_x64_block(code.value, native);
    CHECK(executed.status == jojo::R3000aX64ExecutionStatus::executed);
    CHECK(executed.instructions_retired == words.size());
    CHECK(code.value.executable_entry == entry_before);

    jojo::R3000aState second_native{};
    second_native.pc = 0x80010000u;
    second_native.next_pc = 0x80010004u;
    const auto second_executed =
        jojo::execute_r3000a_x64_block(code.value, second_native);
    CHECK(second_executed.status == jojo::R3000aX64ExecutionStatus::executed);
    CHECK(code.value.executable_entry == entry_before);
    CHECK(second_native.gpr == native.gpr);

    TestR3000aBus bus;
    for (std::size_t i = 0; i < words.size(); ++i) {
        bus.store32(
            0x80010000u + static_cast<std::uint32_t>(i * 4u),
            words[i]);
        CHECK(jojo::step_r3000a(reference, bus).status ==
              jojo::R3000aStepStatus::retired);
    }

    CHECK(native.gpr == reference.gpr);
    CHECK(native.pc == reference.pc);
    CHECK(native.next_pc == reference.next_pc);
}
#endif
} // namespace

int main() {
    test_direct_branch_instruction_is_lowerable();
    test_x64_emitter_accepts_only_v0_safe_subset();
#if defined(_WIN32) && defined(_M_X64)
    test_x64_direct_branch_not_taken_matches_reference();
    test_x64_direct_jal_matches_reference();
    test_x64_direct_branch_matches_reference();
    test_x64_direct_jalr_reads_target_before_link_write();
    test_x64_mult_and_multu_match_reference();
    test_x64_variable_shifts_and_hilo_match_reference();
    test_x64_machine_code_matches_reference_executor();
#endif
    return failures ? 1 : 0;
}
