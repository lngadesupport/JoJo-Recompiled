#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"

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






static void test_bios_event_lifecycle_and_ready_delivery() {
    jojo::Ps1HleBios bios{};

    auto open = make_cpu();
    open.gpr[4] = 0xF4000001u;
    open.gpr[5] = 4u;
    open.gpr[6] = 0x2000u;
    open.gpr[7] = 0u;
    CHECK(bios.dispatch(open, 0xB0u, 0x08u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(open.gpr[2] == 0xF1000000u);
    CHECK(bios.events()[0].allocated);
    CHECK(!bios.events()[0].enabled);

    auto enable = make_cpu();
    enable.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(enable, 0xB0u, 0x0Cu) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(enable.gpr[2] == 1u);
    CHECK(bios.events()[0].enabled);

    auto deliver = make_cpu();
    deliver.gpr[4] = 0xF4000001u;
    deliver.gpr[5] = 4u;
    CHECK(bios.dispatch(deliver, 0xB0u, 0x07u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.events()[0].ready);

    auto test = make_cpu();
    test.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(test, 0xB0u, 0x0Bu) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(test.gpr[2] == 1u);
    CHECK(!bios.events()[0].ready);

    auto disable = make_cpu();
    disable.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(disable, 0xB0u, 0x0Du) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(disable.gpr[2] == 1u);
    CHECK(!bios.events()[0].enabled);

    auto close = make_cpu();
    close.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(close, 0xB0u, 0x09u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(close.gpr[2] == 1u);
    CHECK(!bios.events()[0].allocated);
}

static void test_ready_event_can_be_delivered_by_hardware_pump() {
    jojo::Ps1HleBios bios{};

    auto open = make_cpu();
    open.gpr[4] = 0xF0000009u;
    open.gpr[5] = 0x20u;
    open.gpr[6] = 0x2000u;
    open.gpr[7] = 0u;
    CHECK(bios.dispatch(open, 0xB0u, 0x08u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);

    auto enable = make_cpu();
    enable.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(enable, 0xB0u, 0x0Cu) ==
          jojo::Ps1HleBiosDispatchStatus::handled);

    bios.deliver_event(0xF0000009u, 0x20u);
    CHECK(bios.events()[0].ready);

    auto wait = make_cpu();
    wait.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(wait, 0xB0u, 0x0Au) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(wait.gpr[2] == 1u);
    CHECK(!bios.events()[0].ready);

    auto disable = make_cpu();
    disable.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(disable, 0xB0u, 0x0Du) ==
          jojo::Ps1HleBiosDispatchStatus::handled);

    auto disabled_wait = make_cpu();
    disabled_wait.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(disabled_wait, 0xB0u, 0x0Au) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(disabled_wait.gpr[2] == 0u);
}

static void test_callback_event_preserves_callback_without_fake_delivery() {
    jojo::Ps1HleBios bios{};
    auto open = make_cpu();
    open.gpr[4] = 0xF4000001u;
    open.gpr[5] = 4u;
    open.gpr[6] = 0x1000u;
    open.gpr[7] = 0x80047AD0u;
    CHECK(bios.dispatch(open, 0xB0u, 0x08u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.events()[0].function == 0x80047AD0u);

    auto enable = make_cpu();
    enable.gpr[4] = open.gpr[2];
    CHECK(bios.dispatch(enable, 0xB0u, 0x0Cu) ==
          jojo::Ps1HleBiosDispatchStatus::handled);

    auto deliver = make_cpu();
    deliver.gpr[4] = 0xF4000001u;
    deliver.gpr[5] = 4u;
    CHECK(bios.dispatch(deliver, 0xB0u, 0x07u) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(!bios.events()[0].ready);
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

static void test_hookentryint_restores_jmpbuf_and_returnfromexception() {
    jojo::Ps1HleBios bios{};
    jojo::Ps1MemoryBus bus{};

    constexpr std::uint32_t hook_buffer = 0x80006000u;
    constexpr std::uint32_t hook_ra = 0x80012340u;

    auto install = make_cpu();
    install.gpr[4] = hook_buffer;
    CHECK(bios.dispatch(
              install, 0xB0u, 0x19u, &bus) ==
          jojo::Ps1HleBiosDispatchStatus::handled);

    const std::array<std::uint32_t, 12> jump_buffer{
        hook_ra,
        0x801FF000u,
        0x801FE000u,
        0x11111111u,
        0x22222222u,
        0x33333333u,
        0x44444444u,
        0x55555555u,
        0x66666666u,
        0x77777777u,
        0x88888888u,
        0x12345678u,
    };
    for (std::size_t i = 0u; i < jump_buffer.size(); ++i) {
        CHECK(bus.write32(
                  hook_buffer +
                      static_cast<std::uint32_t>(i * 4u),
                  jump_buffer[i]).status ==
              jojo::R3000aBusStatus::ok);
    }

    auto exception_cpu = make_cpu();
    exception_cpu.pc = 0x80000080u;
    exception_cpu.next_pc = 0x80000084u;
    exception_cpu.cop0.status = 0x00000404u;
    exception_cpu.cop0.cause = 0x00000400u;
    exception_cpu.cop0.epc = 0x80020000u;
    exception_cpu.external_interrupt_pending = 0x04u;

    auto resume = exception_cpu;
    resume.pc = 0x80020000u;
    resume.next_pc = 0x80020004u;
    resume.cop0.status = 0x00000401u;
    resume.gpr[8] = 0xCAFEBABEu;

    const auto hash_before = bios.diagnostic_state_hash();
    CHECK(bios.begin_interrupt_hook(
              exception_cpu, resume, bus) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.diagnostic_state_hash() != hash_before);
    CHECK(exception_cpu.pc == hook_ra);
    CHECK(exception_cpu.next_pc == hook_ra + 4u);
    CHECK(exception_cpu.gpr[2] == 1u);
    CHECK(exception_cpu.gpr[31] == jump_buffer[0]);
    CHECK(exception_cpu.gpr[29] == jump_buffer[1]);
    CHECK(exception_cpu.gpr[30] == jump_buffer[2]);
    for (std::size_t i = 0u; i < 8u; ++i) {
        CHECK(exception_cpu.gpr[16u + i] ==
              jump_buffer[3u + i]);
    }
    CHECK(exception_cpu.gpr[28] == jump_buffer[11]);

    // Model guest IRQ acknowledgement before B(17h).
    exception_cpu.external_interrupt_pending = 0u;
    const auto cause_before_return = exception_cpu.cop0.cause;
    const auto epc_before_return = exception_cpu.cop0.epc;
    CHECK(bios.dispatch(
              exception_cpu, 0xB0u, 0x17u, &bus) ==
          jojo::Ps1HleBiosDispatchStatus::handled);

    CHECK(exception_cpu.pc == resume.pc);
    CHECK(exception_cpu.next_pc == resume.next_pc);
    CHECK(exception_cpu.cop0.status == resume.cop0.status);
    CHECK(exception_cpu.cop0.cause == cause_before_return);
    CHECK(exception_cpu.cop0.epc == epc_before_return);
    CHECK(exception_cpu.external_interrupt_pending == 0u);
    CHECK(exception_cpu.gpr[8] == 0xCAFEBABEu);
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

static void test_c0_irq_priority_chain_enqueue_and_dequeue() {
    jojo::Ps1HleBios bios{};
    jojo::Ps1MemoryBus bus{};

    constexpr std::uint32_t first = 0x80006000u;
    constexpr std::uint32_t second = 0x80006020u;

    auto enqueue_first = make_cpu();
    enqueue_first.gpr[4] = 2u;
    enqueue_first.gpr[5] = first;
    enqueue_first.gpr[2] = 0x13572468u;
    CHECK(bios.dispatch(
              enqueue_first, 0xC0u, 0x02u, &bus) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.interrupt_priority_head(2u) ==
          std::optional<std::uint32_t>{first});
    CHECK(bus.read32(first).status == jojo::R3000aBusStatus::ok);
    CHECK(bus.read32(first).value == 0u);
    CHECK(enqueue_first.gpr[2] == 0x13572468u);
    check_returned_through_ra(enqueue_first);

    auto enqueue_second = make_cpu();
    enqueue_second.gpr[4] = 2u;
    enqueue_second.gpr[5] = second;
    CHECK(bios.dispatch(
              enqueue_second, 0xC0u, 0x02u, &bus) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.interrupt_priority_head(2u) ==
          std::optional<std::uint32_t>{second});
    CHECK(bus.read32(second).value == first);

    auto dequeue_second = make_cpu();
    dequeue_second.gpr[4] = 2u;
    dequeue_second.gpr[5] = second;
    CHECK(bios.dispatch(
              dequeue_second, 0xC0u, 0x03u, &bus) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(bios.interrupt_priority_head(2u) ==
          std::optional<std::uint32_t>{first});

    auto dequeue_first = make_cpu();
    dequeue_first.gpr[4] = 2u;
    dequeue_first.gpr[5] = first;
    CHECK(bios.dispatch(
              dequeue_first, 0xC0u, 0x03u, &bus) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(!bios.interrupt_priority_head(2u).has_value());
}

static void test_c0_03_removes_unmodeled_bios_chain_element() {
    jojo::Ps1HleBios bios{};
    jojo::Ps1MemoryBus bus{};
    auto cpu = make_cpu();
    cpu.gpr[4] = 2u;
    cpu.gpr[5] = 0x8006364Cu;
    cpu.gpr[2] = 0xCAFEBABEu;

    CHECK(bios.dispatch(cpu, 0xC0u, 0x03u, &bus) ==
          jojo::Ps1HleBiosDispatchStatus::handled);
    CHECK(cpu.gpr[2] == 0xCAFEBABEu);
    CHECK(!bios.interrupt_priority_head(2u).has_value());
    check_returned_through_ra(cpu);
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
    test_bios_event_lifecycle_and_ready_delivery();
    test_ready_event_can_be_delivered_by_hardware_pump();
    test_callback_event_preserves_callback_without_fake_delivery();
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
    test_hookentryint_restores_jmpbuf_and_returnfromexception();
    test_b0_5b_changeclearpad();
    test_c0_irq_priority_chain_enqueue_and_dequeue();
    test_c0_03_removes_unmodeled_bios_chain_element();
    test_c0_0a_changeclearrcnt_returns_previous_state();
    test_unknown_selector_is_non_mutating();
    test_unknown_table_is_non_mutating();
    test_changeclearrcnt_out_of_range_is_non_mutating();
    test_hash_is_deterministic_and_tracks_all_hle_state();
    return failures ? 1 : 0;
}
