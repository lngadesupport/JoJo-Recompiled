#include "core/r3000a_dispatch.h"
#include "core/r3000a_reference_executor.h"
#include "mips_test_encode.h"
#include "r3000a_test_bus.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

template <std::size_t N>
jojo::R3000aIrBlock lift(
    const std::array<std::uint32_t, N>& words,
    std::uint32_t pc = 0x80010000u) {
    const auto block = jojo::lift_r3000a_basic_block(pc, words);
    CHECK(block);
    return block ? block.value : jojo::R3000aIrBlock{};
}

template <std::size_t N>
void store(
    TestR3000aBus& bus,
    const std::array<std::uint32_t, N>& words,
    std::uint32_t pc = 0x80010000u) {
    for (std::size_t i = 0; i < words.size(); ++i) {
        bus.store32(pc + static_cast<std::uint32_t>(i * 4u), words[i]);
    }
}

void test_safe_alu_dispatch_matches_reference() {
    const std::array<std::uint32_t, 3> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1234u),
        test_mips::i(0x0Du, 8u, 8u, 0x00F0u),
        test_mips::i(0x09u, 8u, 9u, 0xFFFFu),
    };
    const auto block = lift(words);

    jojo::R3000aState dispatched{};
    dispatched.pc = 0x80010000u;
    dispatched.next_pc = 0x80010004u;
    auto reference = dispatched;

    TestR3000aBus dispatch_bus;
    TestR3000aBus reference_bus;
    store(dispatch_bus, words);
    store(reference_bus, words);

    jojo::R3000aX64BlockCache cache;
    const auto result = jojo::dispatch_r3000a_block(
        block, dispatched, dispatch_bus, cache);
    CHECK(result.status == jojo::R3000aDispatchStatus::completed);
#if (defined(_WIN32) && defined(_M_X64)) || \
    (defined(__linux__) && defined(__x86_64__))
    CHECK(result.mode == jojo::R3000aDispatchMode::native_x64);
#else
    CHECK(result.mode == jojo::R3000aDispatchMode::reference_fallback);
#endif
    CHECK(result.instructions_retired == words.size());

    for (std::size_t i = 0; i < words.size(); ++i) {
        CHECK(jojo::step_r3000a(reference, reference_bus).status ==
              jojo::R3000aStepStatus::retired);
    }
    CHECK(dispatched.gpr == reference.gpr);
    CHECK(dispatched.pc == reference.pc);
    CHECK(dispatched.next_pc == reference.next_pc);
}

void test_branch_block_uses_reference_fallback_and_executes_delay_slot() {
    const std::array<std::uint32_t, 2> words{
        test_mips::i(0x04u, 0u, 0u, 1u),
        test_mips::i(0x09u, 0u, 8u, 7u),
    };
    const auto block = lift(words);
    jojo::R3000aState state{};
    state.pc = 0x80010000u;
    state.next_pc = 0x80010004u;

    TestR3000aBus bus;
    store(bus, words);
    jojo::R3000aX64BlockCache cache;
    const auto result =
        jojo::dispatch_r3000a_block(block, state, bus, cache);
    CHECK(result.status == jojo::R3000aDispatchStatus::completed);
    CHECK(result.mode == jojo::R3000aDispatchMode::reference_fallback);
    CHECK(result.instructions_retired == 2u);
    CHECK(state.gpr[8] == 7u);
    CHECK(state.pc == 0x80010008u);
    CHECK(state.next_pc == 0x8001000Cu);
}

void test_reference_fallback_preserves_boundary() {
    const std::array<std::uint32_t, 1> words{
        test_mips::i(0x23u, 8u, 9u, 0u),
    };
    const auto block = lift(words);
    jojo::R3000aState state{};
    state.pc = 0x80010000u;
    state.next_pc = 0x80010004u;
    state.gpr[8] = 0x1F900000u;

    TestR3000aBus bus;
    store(bus, words);
    bus.fail_unsupported(0x1F900000u);

    jojo::R3000aX64BlockCache cache;
    const auto result =
        jojo::dispatch_r3000a_block(block, state, bus, cache);
    CHECK(result.status == jojo::R3000aDispatchStatus::stopped);
    CHECK(result.mode == jojo::R3000aDispatchMode::reference_fallback);
    CHECK(result.instructions_retired == 0u);
    CHECK(result.stop_result.has_value());
    if (result.stop_result) {
        CHECK(result.stop_result->status == jojo::R3000aStepStatus::boundary);
    }
}
} // namespace

int main() {
    test_safe_alu_dispatch_matches_reference();
    test_branch_block_uses_reference_fallback_and_executes_delay_slot();
    test_reference_fallback_preserves_boundary();
    return failures ? 1 : 0;
}
