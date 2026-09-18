#include "core/r3000a_x64_cache.h"
#include "mips_test_encode.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

jojo::R3000aIrBlock lift(std::uint32_t immediate) {
    const std::array<std::uint32_t, 2> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1234u),
        test_mips::i(0x0Du, 8u, 8u, static_cast<std::uint16_t>(immediate)),
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    return block ? block.value : jojo::R3000aIrBlock{};
}

void test_cache_reuses_identical_block() {
    jojo::R3000aX64BlockCache cache;
    const auto block = lift(0x00F0u);
    const auto first = cache.get_or_compile(block);
    const auto second = cache.get_or_compile(block);
    CHECK(first);
    CHECK(second);
    if (first && second) {
        CHECK(first.value == second.value);
#if defined(_WIN32) && defined(_M_X64)
        CHECK(first.value->executable_entry != nullptr);
        CHECK(first.value->executable_entry == second.value->executable_entry);
#endif
    }
    const auto stats = cache.stats();
    CHECK(stats.entries == 1u);
    CHECK(stats.compilations == 1u);
    CHECK(stats.reuses == 1u);
    CHECK(stats.invalidations == 0u);
}

void test_cache_invalidates_same_entry_when_guest_code_changes() {
    jojo::R3000aX64BlockCache cache;
    const auto first_block = lift(0x00F0u);
    const auto changed_block = lift(0x00F1u);
    CHECK(jojo::r3000a_x64_block_fingerprint(first_block) !=
          jojo::r3000a_x64_block_fingerprint(changed_block));

    CHECK(cache.get_or_compile(first_block));
    CHECK(cache.get_or_compile(changed_block));
    const auto stats = cache.stats();
    CHECK(stats.entries == 1u);
    CHECK(stats.compilations == 2u);
    CHECK(stats.reuses == 0u);
    CHECK(stats.invalidations == 1u);
}


void test_direct_instruction_cache_reuses_pc_and_opcode() {
    jojo::R3000aX64BlockCache cache;
    const auto opcode = test_mips::i(0x0Fu, 0u, 8u, 0x1234u);
    const auto first =
        cache.get_or_compile_instruction(0x80010000u, opcode);
    const auto second =
        cache.get_or_compile_instruction(0x80010000u, opcode);
    CHECK(first);
    CHECK(second);
    if (first && second) {
        CHECK(first.value == second.value);
#if defined(_WIN32) && defined(_M_X64)
        CHECK(first.value->executable_entry != nullptr);
        CHECK(first.value->executable_entry == second.value->executable_entry);
#endif
    }
    const auto stats = cache.stats();
    CHECK(stats.entries == 1u);
    CHECK(stats.compilations == 1u);
    CHECK(stats.reuses == 1u);
}

void test_direct_instruction_cache_invalidates_changed_opcode() {
    jojo::R3000aX64BlockCache cache;
    const auto first_opcode = test_mips::i(0x0Fu, 0u, 8u, 0x1234u);
    const auto changed_opcode = test_mips::i(0x0Fu, 0u, 8u, 0x5678u);
    CHECK(cache.get_or_compile_instruction(0x80010000u, first_opcode));
    CHECK(cache.get_or_compile_instruction(0x80010000u, changed_opcode));
    const auto stats = cache.stats();
    CHECK(stats.entries == 1u);
    CHECK(stats.compilations == 2u);
    CHECK(stats.invalidations == 1u);
}

void test_direct_instruction_cache_rejects_unsupported_opcode() {
    jojo::R3000aX64BlockCache cache;
    const auto load = test_mips::i(0x23u, 8u, 9u, 0u);
    const auto result =
        cache.get_or_compile_instruction(0x80010000u, load);
    CHECK(!result);
    CHECK(result.error == jojo::ErrorCode::backend_unavailable);
    CHECK(cache.stats().entries == 0u);
}


void test_resident_cache_evicts_when_capacity_is_reached() {
    jojo::R3000aX64BlockCache cache(2u);
    const auto op0 = test_mips::i(0x0Fu, 0u, 8u, 0x1111u);
    const auto op1 = test_mips::i(0x0Fu, 0u, 8u, 0x2222u);
    const auto op2 = test_mips::i(0x0Fu, 0u, 8u, 0x3333u);
    CHECK(cache.get_or_compile_instruction(0x80010000u, op0));
    CHECK(cache.get_or_compile_instruction(0x80010004u, op1));
    CHECK(cache.get_or_compile_instruction(0x80010008u, op2));
    const auto stats = cache.stats();
    CHECK(stats.entries == 2u);
    CHECK(stats.compilations == 3u);
    CHECK(stats.evictions == 1u);
}

void test_unsupported_block_is_not_cached() {
    jojo::R3000aX64BlockCache cache;
    const std::array<std::uint32_t, 1> words{
        test_mips::i(0x23u, 8u, 9u, 0u),
    };
    const auto block = jojo::lift_r3000a_basic_block(0x80010000u, words);
    CHECK(block);
    if (!block) return;
    const auto compiled = cache.get_or_compile(block.value);
    CHECK(!compiled);
    CHECK(cache.stats().entries == 0u);
    CHECK(cache.stats().compilations == 0u);
}
} // namespace

int main() {
    test_cache_reuses_identical_block();
    test_cache_invalidates_same_entry_when_guest_code_changes();
    test_direct_instruction_cache_reuses_pc_and_opcode();
    test_direct_instruction_cache_invalidates_changed_opcode();
    test_direct_instruction_cache_rejects_unsupported_opcode();
    test_resident_cache_evicts_when_capacity_is_reached();
    test_unsupported_block_is_not_cached();
    return failures ? 1 : 0;
}
