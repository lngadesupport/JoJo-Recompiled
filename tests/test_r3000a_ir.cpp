#include "core/r3000a_ir.h"
#include "mips_test_encode.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

void test_conditional_branch_includes_delay_slot() {
    const std::array<std::uint32_t, 4> words{
        test_mips::i(0x09u, 0u, 8u, 1u),
        test_mips::i(0x04u, 8u, 0u, 2u),
        test_mips::i(0x09u, 9u, 9u, 1u),
        test_mips::i(0x09u, 10u, 10u, 1u),
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    CHECK(block.value.instructions.size() == 3u);
    CHECK(!block.value.instructions[0].delay_slot);
    CHECK(!block.value.instructions[1].delay_slot);
    CHECK(block.value.instructions[2].delay_slot);
    CHECK(block.value.terminator == jojo::R3000aIrTerminatorKind::conditional_branch);
    CHECK(block.value.taken_target == 0x80010010u);
    CHECK(block.value.fallthrough_target == 0x8001000Cu);
    CHECK(block.value.has_delay_slot);
}

void test_direct_jump_target_and_delay_slot() {
    const std::array<std::uint32_t, 3> words{
        test_mips::j(0x02u, 0x00012000u >> 2u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 2u, 7u),
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    CHECK(block.value.instructions.size() == 2u);
    CHECK(block.value.instructions[1].delay_slot);
    CHECK(block.value.terminator == jojo::R3000aIrTerminatorKind::direct_jump);
    CHECK(block.value.taken_target == 0x80012000u);
    CHECK(!block.value.fallthrough_target.has_value());
}

void test_indirect_jump_is_explicit() {
    const std::array<std::uint32_t, 2> words{
        test_mips::r(31u, 0u, 0u, 0u, 0x08u),
        0x00000000u,
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    CHECK(block.value.terminator == jojo::R3000aIrTerminatorKind::indirect_jump);
    CHECK(!block.value.taken_target.has_value());
    CHECK(!block.value.fallthrough_target.has_value());
    CHECK(block.value.has_delay_slot);
}

void test_non_control_block_falls_through() {
    const std::array<std::uint32_t, 2> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1234u),
        test_mips::i(0x0Du, 8u, 8u, 0x5678u),
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    CHECK(block.value.instructions.size() == 2u);
    CHECK(block.value.terminator == jojo::R3000aIrTerminatorKind::fallthrough);
    CHECK(block.value.fallthrough_target == 0x80010008u);
    CHECK(!block.value.has_delay_slot);
}

void test_reserved_instruction_is_rejected() {
    const std::array<std::uint32_t, 1> words{0x7C000000u};
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(!block);
    CHECK(block.error == jojo::ErrorCode::backend_unavailable);
}
} // namespace

int main() {
    test_conditional_branch_includes_delay_slot();
    test_direct_jump_target_and_delay_slot();
    test_indirect_jump_is_explicit();
    test_non_control_block_falls_through();
    test_reserved_instruction_is_rejected();
    return failures ? 1 : 0;
}
