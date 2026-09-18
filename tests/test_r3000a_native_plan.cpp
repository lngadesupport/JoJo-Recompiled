#include "core/r3000a_native_plan.h"
#include "mips_test_encode.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

jojo::R3000aCfg make_single_block(std::initializer_list<std::uint32_t> words) {
    const std::vector<std::uint32_t> program(words);
    const auto cfg = jojo::build_r3000a_cfg(
        0x80010000u,
        program,
        0x80010000u);
    CHECK(cfg);
    return cfg ? cfg.value : jojo::R3000aCfg{};
}

void test_pure_nontrapping_alu_block_is_native_candidate() {
    const auto cfg = make_single_block({
        test_mips::i(0x0Fu, 0u, 8u, 0x1234u),
        test_mips::i(0x0Du, 8u, 8u, 0x5678u),
        test_mips::i(0x09u, 8u, 9u, 4u),
    });
    const auto plan = jojo::plan_r3000a_native_lowering(cfg);
    CHECK(plan.blocks.size() == 1u);
    CHECK(plan.native_block_count == 1u);
    CHECK(plan.reference_fallback_block_count == 0u);
    if (!plan.blocks.empty()) {
        CHECK(plan.blocks[0].mode == jojo::R3000aNativeLoweringMode::native_alu);
        CHECK(plan.blocks[0].instruction_count == 3u);
    }
}

void test_overflow_trapping_addi_stays_reference_fallback() {
    const auto cfg = make_single_block({
        test_mips::i(0x08u, 8u, 8u, 1u),
    });
    const auto plan = jojo::plan_r3000a_native_lowering(cfg);
    CHECK(plan.native_block_count == 0u);
    CHECK(plan.reference_fallback_block_count == 1u);
}

void test_memory_access_stays_reference_fallback() {
    const auto cfg = make_single_block({
        test_mips::i(0x23u, 8u, 9u, 0u),
    });
    const auto plan = jojo::plan_r3000a_native_lowering(cfg);
    CHECK(plan.native_block_count == 0u);
    CHECK(plan.reference_fallback_block_count == 1u);
}

void test_control_transfer_stays_reference_until_native_pc_semantics_exist() {
    const auto cfg = make_single_block({
        test_mips::i(0x04u, 8u, 9u, 1u),
        0x00000000u,
        0x00000000u,
    });
    const auto plan = jojo::plan_r3000a_native_lowering(cfg);
    CHECK(plan.native_block_count == 0u);
    CHECK(plan.reference_fallback_block_count >= 1u);
}
} // namespace

int main() {
    test_pure_nontrapping_alu_block_is_native_candidate();
    test_overflow_trapping_addi_stays_reference_fallback();
    test_memory_access_stays_reference_fallback();
    test_control_transfer_stays_reference_until_native_pc_semantics_exist();
    return failures ? 1 : 0;
}
