#include "core/ps1_hle_bios.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::R3000aState make_cpu() {
    jojo::R3000aState cpu{};
    cpu.pc = 0x000000A0u;
    cpu.next_pc = 0x000000A4u;
    cpu.gpr[31] = 0x80010040u;
    cpu.gpr[0] = 0xFFFFFFFFu;
    return cpu;
}

static void check_returned_through_ra(const jojo::R3000aState& cpu) {
    CHECK(cpu.pc == 0x80010040u);
    CHECK(cpu.next_pc == 0x80010044u);
    CHECK(!cpu.delay_slot.active);
    CHECK(cpu.gpr[0] == 0u);
}

static void test_a0_39_initheap() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    cpu.gpr[4] = 0x00004000u;
    cpu.gpr[5] = 0x00001000u;

    CHECK(bios.dispatch(cpu, 0xA0u, 0x39u) == jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.heap_state().has_value());
    if (bios.heap_state()) {
        CHECK(bios.heap_state()->base == 0x00004000u);
        CHECK(bios.heap_state()->size == 0x00001000u);
    }
    check_returned_through_ra(cpu);
}

static void check_remove_alias(std::uint32_t selector) {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    cpu.gpr[2] = 0x12345678u;

    CHECK(bios.dispatch(cpu, 0xA0u, selector) == jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.iso9660_removed());
    CHECK(cpu.gpr[2] == 0x12345678u);
    check_returned_through_ra(cpu);
}

static void test_a0_remove_iso9660_aliases() {
    check_remove_alias(0x56u);
    check_remove_alias(0x72u);
}

static void test_b0_19_hookentryint() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    cpu.gpr[4] = 0x00006000u;

    CHECK(bios.dispatch(cpu, 0xB0u, 0x19u) == jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.interrupt_hook_address().has_value());
    if (bios.interrupt_hook_address()) {
        CHECK(*bios.interrupt_hook_address() == 0x00006000u);
    }
    check_returned_through_ra(cpu);
}

static void test_b0_5b_changeclearpad() {
    jojo::Ps1HleBios bios{};

    auto disabled = make_cpu();
    disabled.gpr[4] = 0u;
    CHECK(bios.dispatch(disabled, 0xB0u, 0x5Bu) == jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.pad_card_auto_ack_enabled().has_value());
    if (bios.pad_card_auto_ack_enabled()) CHECK(!*bios.pad_card_auto_ack_enabled());
    check_returned_through_ra(disabled);

    auto enabled = make_cpu();
    enabled.gpr[4] = 9u;
    CHECK(bios.dispatch(enabled, 0xB0u, 0x5Bu) == jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.pad_card_auto_ack_enabled().has_value());
    if (bios.pad_card_auto_ack_enabled()) CHECK(*bios.pad_card_auto_ack_enabled());
    check_returned_through_ra(enabled);
}

static void test_c0_0a_changeclearrcnt_returns_previous_state() {
    jojo::Ps1HleBios bios{};

    auto enable = make_cpu();
    enable.gpr[4] = 3u;
    enable.gpr[5] = 1u;
    enable.gpr[2] = 0xFFFFFFFFu;
    CHECK(bios.dispatch(enable, 0xC0u, 0x0Au) == jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(enable.gpr[2] == 0u);
    CHECK(bios.root_counter_auto_ack_enabled(3u).has_value());
    if (bios.root_counter_auto_ack_enabled(3u)) CHECK(*bios.root_counter_auto_ack_enabled(3u));
    check_returned_through_ra(enable);

    auto disable = make_cpu();
    disable.gpr[4] = 3u;
    disable.gpr[5] = 0u;
    disable.gpr[2] = 0u;
    CHECK(bios.dispatch(disable, 0xC0u, 0x0Au) == jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(disable.gpr[2] == 1u);
    CHECK(bios.root_counter_auto_ack_enabled(3u).has_value());
    if (bios.root_counter_auto_ack_enabled(3u)) CHECK(!*bios.root_counter_auto_ack_enabled(3u));
    check_returned_through_ra(disable);
}

static void test_unknown_selector_is_non_mutating() {
    jojo::Ps1HleBios bios{};
    auto seed = make_cpu();
    seed.gpr[4] = 0x4000u;
    seed.gpr[5] = 0x1000u;
    CHECK(bios.dispatch(seed, 0xA0u, 0x39u) == jojo::Ps1HleBiosDispatchStatus::handled);

    auto cpu = make_cpu();
    cpu.gpr[2] = 0x12345678u;
    cpu.gpr[4] = 0x11111111u;
    const auto before = cpu;
    const auto hash_before = bios.diagnostic_state_hash();

    CHECK(bios.dispatch(cpu, 0xA0u, 0x33u) == jojo::Ps1HleBiosDispatchStatus::unimplemented);
    CHECK(cpu.gpr == before.gpr);
    CHECK(cpu.pc == before.pc);
    CHECK(cpu.next_pc == before.next_pc);
    CHECK(cpu.delay_slot.active == before.delay_slot.active);
    CHECK(cpu.delay_slot.branch_pc == before.delay_slot.branch_pc);
    CHECK(cpu.delay_slot.taken == before.delay_slot.taken);
    CHECK(cpu.delay_slot.target == before.delay_slot.target);
    CHECK(bios.diagnostic_state_hash() == hash_before);
}

static void test_unknown_table_is_non_mutating() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    cpu.gpr[2] = 0xCAFEBABEu;
    const auto before = cpu;
    const auto hash_before = bios.diagnostic_state_hash();

    CHECK(bios.dispatch(cpu, 0xD0u, 0x39u) == jojo::Ps1HleBiosDispatchStatus::unimplemented);
    CHECK(cpu.gpr == before.gpr);
    CHECK(cpu.pc == before.pc);
    CHECK(cpu.next_pc == before.next_pc);
    CHECK(bios.diagnostic_state_hash() == hash_before);
}

static void test_changeclearrcnt_out_of_range_is_non_mutating() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    cpu.gpr[4] = 4u;
    cpu.gpr[5] = 1u;
    cpu.gpr[2] = 0x13572468u;
    const auto before = cpu;
    const auto hash_before = bios.diagnostic_state_hash();

    CHECK(bios.dispatch(cpu, 0xC0u, 0x0Au) == jojo::Ps1HleBiosDispatchStatus::unimplemented);
    CHECK(cpu.gpr == before.gpr);
    CHECK(cpu.pc == before.pc);
    CHECK(cpu.next_pc == before.next_pc);
    CHECK(!bios.root_counter_auto_ack_enabled(4u).has_value());
    CHECK(bios.diagnostic_state_hash() == hash_before);
}

static std::uint64_t hash_after(std::uint32_t table,
                                std::uint32_t selector,
                                std::uint32_t a0,
                                std::uint32_t a1) {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    cpu.gpr[4] = a0;
    cpu.gpr[5] = a1;
    CHECK(bios.dispatch(cpu, table, selector) == jojo::Ps1HleBiosDispatchStatus::handled);
    return bios.diagnostic_state_hash();
}

static void test_hash_is_deterministic_and_tracks_all_hle_state() {
    jojo::Ps1HleBios empty_a{};
    jojo::Ps1HleBios empty_b{};
    const auto baseline = empty_a.diagnostic_state_hash();
    CHECK(baseline == empty_b.diagnostic_state_hash());

    const auto heap = hash_after(0xA0u, 0x39u, 0x4000u, 0x1000u);
    const auto hook = hash_after(0xB0u, 0x19u, 0x6000u, 0u);
    const auto pad = hash_after(0xB0u, 0x5Bu, 1u, 0u);
    const auto rcnt0 = hash_after(0xC0u, 0x0Au, 0u, 1u);
    const auto rcnt3 = hash_after(0xC0u, 0x0Au, 3u, 1u);
    const auto removed = hash_after(0xA0u, 0x56u, 0u, 0u);

    CHECK(heap != baseline);
    CHECK(hook != baseline);
    CHECK(pad != baseline);
    CHECK(rcnt0 != baseline);
    CHECK(rcnt3 != baseline);
    CHECK(removed != baseline);
    CHECK(rcnt0 != rcnt3);

    CHECK(hash_after(0xA0u, 0x39u, 0x4000u, 0x1000u) == heap);
    CHECK(hash_after(0xB0u, 0x19u, 0x6000u, 0u) == hook);
}

int main() {
    test_a0_39_initheap();
    test_a0_remove_iso9660_aliases();
    test_b0_19_hookentryint();
    test_b0_5b_changeclearpad();
    test_c0_0a_changeclearrcnt_returns_previous_state();
    test_unknown_selector_is_non_mutating();
    test_unknown_table_is_non_mutating();
    test_changeclearrcnt_out_of_range_is_non_mutating();
    test_hash_is_deterministic_and_tracks_all_hle_state();
    return failures ? 1 : 0;
}
