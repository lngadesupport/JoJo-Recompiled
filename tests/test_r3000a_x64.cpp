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
    }

    const std::array<std::uint32_t, 1> variable_shift{
        test_mips::r(8u, 9u, 10u, 0u, 0x04u),
    };
    const auto variable_block =
        jojo::lift_r3000a_basic_block(0x80010000u, variable_shift);
    CHECK(variable_block);
    if (variable_block) {
        const auto unsupported =
            jojo::emit_r3000a_x64_alu_block(variable_block.value);
        CHECK(!unsupported);
        CHECK(unsupported.error == jojo::ErrorCode::backend_unavailable);
    }
}

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
    CHECK(native.pc == reference.pc);
    CHECK(native.next_pc == reference.next_pc);
}
#endif
} // namespace

int main() {
    test_x64_emitter_accepts_only_v0_safe_subset();
#if defined(_WIN32) && defined(_M_X64)
    test_x64_machine_code_matches_reference_executor();
#endif
    return failures ? 1 : 0;
}
