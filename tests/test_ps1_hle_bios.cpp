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


static jojo::R3000aState make_syscall_cpu() {
    jojo::R3000aState cpu{};
    cpu.pc = 0x80010004u;
    cpu.next_pc = 0x80010008u;
    return cpu;
}

static void test_sys_01_entercriticalsection() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_syscall_cpu();
    cpu.cop0.status = (1u << 0u) | (1u << 10u) | (1u << 22u);
    cpu.pending_load = {true, 8u, 0x12345678u};

    CHECK(bios.dispatch_syscall(cpu, 1u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(cpu.gpr[2] == 1u);
    CHECK(cpu.gpr[8] == 0x12345678u);
    CHECK(!cpu.pending_load.valid);
    CHECK((cpu.cop0.status & ((1u << 0u) | (1u << 10u))) == 0u);
    CHECK((cpu.cop0.status & (1u << 22u)) != 0u);
    CHECK(cpu.pc == 0x80010008u);
    CHECK(cpu.next_pc == 0x8001000Cu);

    auto already_disabled = make_syscall_cpu();
    already_disabled.cop0.status = 1u << 10u;
    CHECK(bios.dispatch_syscall(already_disabled, 1u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(already_disabled.gpr[2] == 0u);
}

static void test_sys_02_exitcriticalsection() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_syscall_cpu();
    cpu.gpr[2] = 0xA5A5A5A5u;

    CHECK(bios.dispatch_syscall(cpu, 2u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK((cpu.cop0.status & ((1u << 0u) | (1u << 10u))) ==
          ((1u << 0u) | (1u << 10u)));
    CHECK(cpu.gpr[2] == 0xA5A5A5A5u);
    CHECK(cpu.pc == 0x80010008u);
    CHECK(cpu.next_pc == 0x8001000Cu);
}

static void test_unknown_syscall_is_non_mutating() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_syscall_cpu();
    cpu.gpr[2] = 0x12345678u;
    const auto before = cpu;
    CHECK(bios.dispatch_syscall(cpu, 3u) ==
          jojo::Ps1HleBiosDispatchStatus::unimplemented);
    CHECK(cpu.gpr == before.gpr);
    CHECK(cpu.pc == before.pc);
    CHECK(cpu.next_pc == before.next_pc);
    CHECK(cpu.cop0.status == before.cop0.status);
}





static void test_backup_unit_init_aliases_mark_card_filesystem_ready() {
    for (const auto selector : {0x55u, 0x70u}) {
        jojo::Ps1HleBios bios{};
        auto cpu = make_cpu();
        CHECK(!bios.backup_unit_initialized());
        CHECK(bios.dispatch(cpu, 0xA0u, selector) ==
              jojo::Ps1HleBiosDispatchStatus::handled);
        CHECK(bios.backup_unit_initialized());
        check_returned_through_ra(cpu);
    }
}

static void test_card2_lifecycle_tracks_pad_enable_and_start_stop() {
    jojo::Ps1HleBios bios{};

    auto init = make_cpu();
    init.gpr[4] = 0u;
    CHECK(bios.dispatch(init, 0xB0u, 0x4Au) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.card_initialized());
    CHECK(!bios.card_started());
    CHECK(!bios.card_pad_enabled());
    check_returned_through_ra(init);

    auto start = make_cpu();
    CHECK(bios.dispatch(start, 0xB0u, 0x4Bu) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.card_started());
    check_returned_through_ra(start);

    auto stop = make_cpu();
    CHECK(bios.dispatch(stop, 0xB0u, 0x4Cu) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(!bios.card_started());
    check_returned_through_ra(stop);

    jojo::Ps1HleBios uninitialized{};
    auto invalid_start = make_cpu();
    const auto before = invalid_start;
    CHECK(uninitialized.dispatch(invalid_start, 0xB0u, 0x4Bu) ==
          jojo::Ps1HleBiosDispatchStatus::unimplemented);
    CHECK(invalid_start.pc == before.pc);
    CHECK(invalid_start.gpr == before.gpr);
}

static void test_stdout_write_aliases_return_requested_length() {
    for (const auto call : std::array<std::pair<std::uint32_t, std::uint32_t>, 2>{{
             {0xA0u, 0x03u},
             {0xB0u, 0x35u},
         }}) {
        jojo::Ps1HleBios bios{};
        auto cpu = make_cpu();
        cpu.gpr[4] = 1u;
        cpu.gpr[5] = 0x800973A8u;
        cpu.gpr[6] = 0x20u;
        CHECK(bios.dispatch(cpu, call.first, call.second) ==
              jojo::Ps1HleBiosDispatchStatus::handled);
        CHECK(cpu.gpr[2] == 0x20u);
        check_returned_through_ra(cpu);
    }

    jojo::Ps1HleBios bios{};
    auto file = make_cpu();
    file.gpr[4] = 3u;
    file.gpr[6] = 0x20u;
    const auto before = file;
    CHECK(bios.dispatch(file, 0xB0u, 0x35u) ==
          jojo::Ps1HleBiosDispatchStatus::unimplemented);
    CHECK(file.gpr == before.gpr);
    CHECK(file.pc == before.pc);
}

static void test_a0_44_flushcache_returns_without_mutating_result() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    cpu.gpr[2] = 0x13572468u;
    CHECK(bios.dispatch(cpu, 0xA0u, 0x44u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(cpu.gpr[2] == 0x13572468u);
    check_returned_through_ra(cpu);
}


static void test_b0_57_getb0table_returns_clean_room_table() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    CHECK(bios.dispatch(cpu, 0xB0u, 0x57u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(cpu.gpr[2] == jojo::kPs1HleB0TableAddress);
    check_returned_through_ra(cpu);
}

static void test_b0_56_getc0table_returns_clean_room_table() {
    jojo::Ps1HleBios bios{};
    auto cpu = make_cpu();
    CHECK(bios.dispatch(cpu, 0xB0u, 0x56u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(cpu.gpr[2] == jojo::kPs1HleC0TableAddress);
    check_returned_through_ra(cpu);
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


static void test_b0_18_resetentryint_clears_custom_hook() {
    jojo::Ps1HleBios bios{};

    auto hooked = make_cpu();
    hooked.gpr[4] = 0x00006000u;
    CHECK(bios.dispatch(hooked, 0xB0u, 0x19u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.interrupt_hook_address().has_value());

    auto reset = make_cpu();
    reset.gpr[2] = 0x12345678u;
    CHECK(bios.dispatch(reset, 0xB0u, 0x18u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(!bios.interrupt_hook_address().has_value());
    CHECK(reset.gpr[2] == 0x12345678u);
    check_returned_through_ra(reset);
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
    test_sys_01_entercriticalsection();
    test_sys_02_exitcriticalsection();
    test_unknown_syscall_is_non_mutating();
    test_backup_unit_init_aliases_mark_card_filesystem_ready();
    test_card2_lifecycle_tracks_pad_enable_and_start_stop();
    test_stdout_write_aliases_return_requested_length();
    test_a0_44_flushcache_returns_without_mutating_result();
    test_b0_56_getc0table_returns_clean_room_table();
    test_b0_57_getb0table_returns_clean_room_table();
    test_a0_39_initheap();
    test_a0_remove_iso9660_aliases();
    test_b0_18_resetentryint_clears_custom_hook();
    test_b0_19_hookentryint();
    test_b0_5b_changeclearpad();
    test_c0_0a_changeclearrcnt_returns_previous_state();
    test_unknown_selector_is_non_mutating();
    test_unknown_table_is_non_mutating();
    test_changeclearrcnt_out_of_range_is_non_mutating();
    test_hash_is_deterministic_and_tracks_all_hle_state();
    return failures ? 1 : 0;
}
