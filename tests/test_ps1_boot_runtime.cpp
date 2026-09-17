#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1BootRuntime make_runtime(const std::vector<std::uint32_t>& words) {
    auto executable = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(executable);
    auto runtime = executable ? jojo::Ps1BootRuntime::create(executable.value)
                              : jojo::Result<jojo::Ps1BootRuntime>::failure(
                                    jojo::ErrorCode::invalid_installation,
                                    "synthetic executable parse failed");
    CHECK(runtime);
    return runtime ? std::move(runtime.value) : jojo::Ps1BootRuntime{};
}

static void test_instruction_budget_is_explicit_stop_reason() {
    const std::vector<std::uint32_t> words{
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({10u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.instructions_retired == 10u);
    CHECK(report.presented_frames == 0u);
}

static void test_budget_exhaustion_keeps_bounded_recent_trace() {
    const std::vector<std::uint32_t> words{
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({20u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.recent_trace.size() == 16u);
    if (report.recent_trace.size() == 16u) {
        CHECK(report.recent_trace.front().pc == 0x80010000u);
        CHECK(report.recent_trace.back().pc == 0x80010004u);
        for (const auto& sample : report.recent_trace) {
            CHECK(sample.opcode.has_value());
        }
    }
}

static void test_local_evidence_options_grow_monotonically() {
    const auto options = jojo::ps1_local_evidence_options();
    CHECK(options.instruction_budget == std::numeric_limits<std::uint64_t>::max());
    CHECK(options.trace_capacity == 4096u);
    CHECK(options.diagnostic_mmio_probe);
    CHECK(options.mmio_event_capacity == 8192u);
    CHECK(options.bios_event_capacity == 4096u);
}

static void test_bios_entry_stops_before_executing_bios_bytes() {
    const std::vector<std::uint32_t> words{
        test_mips::j(0x02u, 0x800000A0u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    runtime.bus().write32(0x000000A0u, 0xFFFFFFFFu);
    const auto report = runtime.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.last_pc == 0x800000A0u);
    CHECK(report.bios_call_count == 1u);
    CHECK(report.recent_bios_calls.size() == 1u);
    if (!report.recent_bios_calls.empty()) {
        CHECK(report.recent_bios_calls.back().table_physical == 0x000000A0u);
    }
}

static void test_a0_39_initheap_returns_to_ra_and_continues() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 4u, 0x4000u),
        test_mips::i(0x09u, 0u, 5u, 0x1000u),
        test_mips::i(0x09u, 0u, 9u, 0x0039u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x1234u),
        test_mips::j(0x02u, 0x8001001Cu >> 2),
        0x00000000u,
    };

    auto runtime = make_runtime(words);
    const auto report = runtime.run({16u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.instructions_retired == 16u);
    CHECK(report.bios_call_count == 1u);
    CHECK(runtime.cpu_state().gpr[16] == 0x1234u);

    const auto& heap = runtime.bios_heap_state();
    CHECK(heap.has_value());
    if (heap) {
        CHECK(heap->base == 0x00004000u);
        CHECK(heap->size == 0x00001000u);
    }
}

static void test_b0_19_hookentryint_records_pointer_args_and_returns() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 4u, 0x6000u),
        test_mips::i(0x09u, 0u, 5u, 0x1111u),
        test_mips::i(0x09u, 0u, 6u, 0x2222u),
        test_mips::i(0x09u, 0u, 7u, 0x3333u),
        test_mips::i(0x09u, 0u, 9u, 0x0019u),
        test_mips::i(0x09u, 0u, 10u, 0x00B0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x1234u),
        test_mips::j(0x02u, 0x80010024u >> 2),
        0x00000000u,
    };

    auto runtime = make_runtime(words);
    const auto report = runtime.run({20u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.bios_call_count == 1u);
    CHECK(runtime.cpu_state().gpr[16] == 0x1234u);
    CHECK(runtime.bios_interrupt_hook_address().has_value());
    if (runtime.bios_interrupt_hook_address()) {
        CHECK(*runtime.bios_interrupt_hook_address() == 0x00006000u);
    }
    CHECK(report.recent_bios_calls.size() == 1u);
    if (!report.recent_bios_calls.empty()) {
        const auto& call = report.recent_bios_calls.back();
        CHECK(call.table_physical == 0x000000B0u);
        CHECK(call.selector == 0x00000019u);
        CHECK(call.a0 == 0x00006000u);
        CHECK(call.a1 == 0x00001111u);
        CHECK(call.a2 == 0x00002222u);
        CHECK(call.a3 == 0x00003333u);
        CHECK(call.ra == 0x80010020u);
    }
}

static void test_b0_5b_changeclearpad_records_flag_and_returns() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 4u, 0x0000u),
        test_mips::i(0x09u, 0u, 9u, 0x005Bu),
        test_mips::i(0x09u, 0u, 10u, 0x00B0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x1234u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    };

    auto runtime = make_runtime(words);
    const auto report = runtime.run({16u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.bios_call_count == 1u);
    CHECK(runtime.cpu_state().gpr[16] == 0x1234u);
    CHECK(runtime.bios_pad_card_auto_ack_enabled().has_value());
    if (runtime.bios_pad_card_auto_ack_enabled()) {
        CHECK(!*runtime.bios_pad_card_auto_ack_enabled());
    }
    CHECK(report.recent_bios_calls.size() == 1u);
    if (!report.recent_bios_calls.empty()) {
        const auto& call = report.recent_bios_calls.back();
        CHECK(call.table_physical == 0x000000B0u);
        CHECK(call.selector == 0x0000005Bu);
        CHECK(call.a0 == 0x00000000u);
        CHECK(call.ra == 0x80010014u);
    }
}

static void test_a0_33_remains_unimplemented() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x1234u),
    };

    auto runtime = make_runtime(words);
    const auto report = runtime.run({16u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(report.bios_call_count == 1u);
    CHECK(report.recent_bios_calls.size() == 1u);
    if (!report.recent_bios_calls.empty()) {
        CHECK(report.recent_bios_calls.back().table_physical == 0x000000A0u);
        CHECK(report.recent_bios_calls.back().selector == 0x00000033u);
    }
    CHECK(runtime.cpu_state().gpr[16] == 0u);
}

static void test_mmio_access_stops_with_structured_evidence() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1070u),
        test_mips::i(0x23u, 8u, 9u, 0u),
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.unsupported_access.has_value());
    if (report.unsupported_access) {
        CHECK(report.unsupported_access->guest_address == 0x1F801070u);
        CHECK(report.unsupported_access->width == 4u);
        CHECK(!report.unsupported_access->write);
    }
    CHECK(report.recent_mmio.size() == 1u);
    if (!report.recent_mmio.empty()) {
        CHECK(!report.recent_mmio.back().speculative);
    }
}

static void test_mega_probe_continues_through_unknown_mmio_and_records_events() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1080u),
        test_mips::i(0x09u, 0u, 9u, 0x1234u),
        test_mips::i(0x2Bu, 8u, 9u, 0u),
        test_mips::i(0x23u, 8u, 10u, 0u),
        0x00000000u,
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    };

    auto runtime = make_runtime(words);
    jojo::Ps1BootOptions options{};
    options.instruction_budget = 12u;
    options.trace_capacity = 8u;
    options.diagnostic_mmio_probe = true;
    options.mmio_event_capacity = 4u;
    const auto report = runtime.run(options);

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.instructions_retired == 12u);
    CHECK(report.diagnostic_probe_mode);
    CHECK(report.speculative_mmio_count == 2u);
    CHECK(report.recent_mmio.size() == 2u);
    if (report.recent_mmio.size() == 2u) {
        CHECK(report.recent_mmio[0].address == 0x1F801080u);
        CHECK(report.recent_mmio[0].width == 4u);
        CHECK(report.recent_mmio[0].write);
        CHECK(report.recent_mmio[0].value == 0x00001234u);
        CHECK(report.recent_mmio[0].speculative);
        CHECK(report.recent_mmio[1].address == 0x1F801080u);
        CHECK(!report.recent_mmio[1].write);
        CHECK(report.recent_mmio[1].value == 0x00001234u);
        CHECK(report.recent_mmio[1].speculative);
    }
    CHECK(report.recent_trace.size() == 8u);
    CHECK(runtime.cpu_state().gpr[10] == 0x00001234u);
}

static void test_deterministic_replay_matches_full_m3a_state() {
    const std::vector<std::uint32_t> words{
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto first = make_runtime(words);
    auto second = make_runtime(words);

    const auto first_report = first.run({32u});
    const auto second_report = second.run({32u});

    CHECK(first_report.stop_reason == second_report.stop_reason);
    CHECK(first_report.instructions_retired == second_report.instructions_retired);
    CHECK(first_report.last_pc == second_report.last_pc);
    CHECK(first_report.last_opcode == second_report.last_opcode);
    CHECK(first_report.bios_call_count == second_report.bios_call_count);
    CHECK(first_report.interrupts_accepted == second_report.interrupts_accepted);
    CHECK(first_report.dma_transfer_count == second_report.dma_transfer_count);
    CHECK(first_report.gpu_gp0_command_count == second_report.gpu_gp0_command_count);
    CHECK(first_report.gpu_gp1_command_count == second_report.gpu_gp1_command_count);
    CHECK(first_report.vram_write_count == second_report.vram_write_count);
    CHECK(first_report.presented_frames == second_report.presented_frames);
    CHECK(first_report.recent_trace.size() == second_report.recent_trace.size());
    for (std::size_t i = 0; i < first_report.recent_trace.size(); ++i) {
        CHECK(first_report.recent_trace[i].pc == second_report.recent_trace[i].pc);
        CHECK(first_report.recent_trace[i].opcode == second_report.recent_trace[i].opcode);
    }

    const auto& a = first.cpu_state();
    const auto& b = second.cpu_state();
    CHECK(a.gpr == b.gpr);
    CHECK(a.hi == b.hi);
    CHECK(a.lo == b.lo);
    CHECK(a.pc == b.pc);
    CHECK(a.next_pc == b.next_pc);
    CHECK(a.pending_load.valid == b.pending_load.valid);
    CHECK(a.pending_load.reg == b.pending_load.reg);
    CHECK(a.pending_load.value == b.pending_load.value);
    CHECK(a.delay_slot.active == b.delay_slot.active);
    CHECK(a.delay_slot.branch_pc == b.delay_slot.branch_pc);
    CHECK(a.delay_slot.taken == b.delay_slot.taken);
    CHECK(a.delay_slot.target == b.delay_slot.target);
    CHECK(a.cop0.target_address == b.cop0.target_address);
    CHECK(a.cop0.bad_vaddr == b.cop0.bad_vaddr);
    CHECK(a.cop0.status == b.cop0.status);
    CHECK(a.cop0.cause == b.cop0.cause);
    CHECK(a.cop0.epc == b.cop0.epc);
    CHECK(a.external_interrupt_pending == b.external_interrupt_pending);
    CHECK(first.bus().read32(0x80010000u).value == second.bus().read32(0x80010000u).value);
}

int main() {
    test_instruction_budget_is_explicit_stop_reason();
    test_budget_exhaustion_keeps_bounded_recent_trace();
    test_local_evidence_options_grow_monotonically();
    test_bios_entry_stops_before_executing_bios_bytes();
    test_a0_39_initheap_returns_to_ra_and_continues();
    test_b0_19_hookentryint_records_pointer_args_and_returns();
    test_b0_5b_changeclearpad_records_flag_and_returns();
    test_a0_33_remains_unimplemented();
    test_mmio_access_stops_with_structured_evidence();
    test_mega_probe_continues_through_unknown_mmio_and_records_events();
    test_deterministic_replay_matches_full_m3a_state();
    return failures ? 1 : 0;
}
