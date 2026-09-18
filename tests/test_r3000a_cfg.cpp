#include "core/r3000a_cfg.h"
#include "mips_test_encode.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

const jojo::R3000aIrBlock* find_block(
    const jojo::R3000aCfg& cfg,
    std::uint32_t pc) {
    for (const auto& block : cfg.blocks) {
        if (block.entry_pc == pc) return &block;
    }
    return nullptr;
}

void test_cfg_follows_conditional_successors_without_decoding_gap_as_one_block() {
    const std::array<std::uint32_t, 6> words{
        test_mips::i(0x09u, 0u, 8u, 1u),       // 00
        test_mips::i(0x04u, 8u, 0u, 2u),       // 04 -> 10
        0x00000000u,                            // 08 delay
        test_mips::i(0x09u, 9u, 9u, 1u),       // 0C fallthrough block
        test_mips::i(0x09u, 10u, 10u, 1u),     // 10 target block
        test_mips::r(31u, 0u, 0u, 0u, 0x08u),  // 14 jr ra
    };
    const auto cfg = jojo::build_r3000a_cfg(
        0x80010000u, words, 0x80010000u);
    CHECK(cfg);
    if (!cfg) return;
    CHECK(find_block(cfg.value, 0x80010000u) != nullptr);
    const auto* fallthrough = find_block(cfg.value, 0x8001000Cu);
    const auto* taken = find_block(cfg.value, 0x80010010u);
    CHECK(fallthrough != nullptr);
    CHECK(taken != nullptr);
    if (fallthrough) CHECK(fallthrough->instructions.size() == 1u);
}

void test_cfg_does_not_follow_unreachable_words_after_direct_jump() {
    const std::array<std::uint32_t, 6> words{
        test_mips::j(0x02u, 0x80010010u >> 2u),
        0x00000000u,
        0x7C000000u,
        0x7C000000u,
        test_mips::r(31u, 0u, 0u, 0u, 0x08u),
        0x00000000u,
    };
    const auto cfg = jojo::build_r3000a_cfg(
        0x80010000u, words, 0x80010000u);
    CHECK(cfg);
    if (!cfg) return;
    CHECK(find_block(cfg.value, 0x80010000u) != nullptr);
    CHECK(find_block(cfg.value, 0x80010010u) != nullptr);
    CHECK(find_block(cfg.value, 0x80010008u) == nullptr);
}

void test_cfg_rejects_entry_outside_program() {
    const std::array<std::uint32_t, 2> words{0u, 0u};
    const auto cfg = jojo::build_r3000a_cfg(
        0x80010000u, words, 0x80020000u);
    CHECK(!cfg);
    CHECK(cfg.error == jojo::ErrorCode::invalid_argument);
}

void test_cfg_enforces_block_budget() {
    const std::array<std::uint32_t, 6> words{
        test_mips::i(0x04u, 0u, 0u, 1u),
        0x00000000u,
        test_mips::i(0x04u, 0u, 0u, 1u),
        0x00000000u,
        test_mips::r(31u, 0u, 0u, 0u, 0x08u),
        0x00000000u,
    };
    const auto cfg = jojo::build_r3000a_cfg(
        0x80010000u, words, 0x80010000u, 1u);
    CHECK(!cfg);
    CHECK(cfg.error == jojo::ErrorCode::backend_unavailable);
}
} // namespace

int main() {
    test_cfg_follows_conditional_successors_without_decoding_gap_as_one_block();
    test_cfg_does_not_follow_unreachable_words_after_direct_jump();
    test_cfg_rejects_entry_outside_program();
    test_cfg_enforces_block_budget();
    return failures ? 1 : 0;
}
