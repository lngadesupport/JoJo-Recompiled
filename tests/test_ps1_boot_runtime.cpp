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


static void test_vblank_routes_to_r3000a_hardware_irq2() {
    auto runtime = make_runtime({
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    });

    // Enable VBlank in I_MASK and CPU IEc + IP2 mask.
    CHECK(runtime.bus().write16(
              0x1F801074u, 0x0001u).status ==
          jojo::R3000aBusStatus::ok);
    auto state = runtime.save_state();
    state.cpu.cop0.status |= 0x00000401u;
    CHECK(runtime.load_state(state));

    runtime.signal_vblank();
    CHECK(runtime.cpu_state().external_interrupt_pending ==
          0x04u);

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 1u;
    const auto report = runtime.run(options);
    CHECK(report.interrupts_accepted == 1u);
}

static void test_runtime_continues_vblank_through_hookentryint() {
    auto runtime = make_runtime({
        // At entry, install HookEntryInt with a synthetic jmp_buf at 80011000.
        test_mips::i(0x0Fu, 0u, 4u, 0x8001u),       // lui a0,8001
        test_mips::i(0x0Du, 4u, 4u, 0x1000u),       // ori a0,a0,1000
        test_mips::i(0x09u, 0u, 9u, 0x0019u),       // addiu t1,zero,19h
        test_mips::i(0x09u, 0u, 10u, 0x00B0u),      // addiu t2,zero,B0h
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),      // jalr ra,t2
        0x00000000u,
        // Spin after install.
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    });

    // jmp_buf RA points to a tiny hook that acknowledges VBlank then B(17h).
    constexpr std::uint32_t buffer = 0x80011000u;
    constexpr std::uint32_t hook = 0x80011100u;
    CHECK(runtime.bus().write32(buffer + 0x00u, hook).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(buffer + 0x04u, 0x801FF000u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(buffer + 0x08u, 0u).status ==
          jojo::R3000aBusStatus::ok);
    for (std::uint32_t offset = 0x0Cu; offset <= 0x2Cu; offset += 4u) {
        CHECK(runtime.bus().write32(buffer + offset, 0u).status ==
              jojo::R3000aBusStatus::ok);
    }

    // Hook body: clear I_STAT, call B(17h) ReturnFromException.
    const std::array<std::uint32_t, 8> hook_words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x29u, 8u, 0u, 0x1070u),       // sh zero,I_STAT
        test_mips::i(0x09u, 0u, 9u, 0x0017u),
        test_mips::i(0x09u, 0u, 10u, 0x00B0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::j(0x02u, hook >> 2),
        0x00000000u,
    };
    for (std::size_t i = 0u; i < hook_words.size(); ++i) {
        CHECK(runtime.bus().write32(
                  hook + static_cast<std::uint32_t>(i * 4u),
                  hook_words[i]).status ==
              jojo::R3000aBusStatus::ok);
    }

    jojo::Ps1BootOptions setup{};
    setup.instruction_budget = 12u;
    const auto installed = runtime.run(setup);
    CHECK(installed.stop_reason ==
          jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(runtime.bios_interrupt_hook_address() ==
          std::optional<std::uint32_t>{buffer});

    CHECK(runtime.bus().write16(
              0x1F801074u, 0x0001u).status ==
          jojo::R3000aBusStatus::ok);
    auto state = runtime.save_state();
    state.cpu.cop0.status |= 0x00000401u;
    CHECK(runtime.load_state(state));

    const auto interrupted_pc = runtime.cpu_state().pc;
    runtime.signal_vblank();

    jojo::Ps1BootOptions run{};
    run.instruction_budget = 20u;
    const auto report = runtime.run(run);
    CHECK(report.interrupts_accepted >= 1u);
    CHECK(report.stop_reason ==
          jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(!report.cpu_diagnostic.has_value());
    CHECK(runtime.bus().hardware_services().interrupt_status() == 0u);
    CHECK(runtime.cpu_state().pc != 0x80000080u);
    CHECK(runtime.cpu_state().pc == interrupted_pc ||
          runtime.cpu_state().pc == interrupted_pc + 4u ||
          runtime.cpu_state().pc == 0x80010018u ||
          runtime.cpu_state().pc == 0x8001001Cu);
}

static void test_runtime_starts_from_post_bios_cdrom_handoff() {
    auto runtime = make_runtime({
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    });

    CHECK(runtime.bus().write8(0x1F801800u, 0x00u).status ==
          jojo::R3000aBusStatus::ok);
    const auto mask = runtime.bus().read8(0x1F801803u);
    CHECK(mask.status == jojo::R3000aBusStatus::ok);
    CHECK((mask.value & 0x1Fu) == 0x1Fu);
}

static void test_runtime_initializes_clean_room_c0_exception_entry() {
    const std::vector<std::uint32_t> words{
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    const auto entry = runtime.bus().read32(
        jojo::kPs1HleC0TableAddress + 6u * sizeof(std::uint32_t));
    CHECK(entry.status == jojo::R3000aBusStatus::ok);
    CHECK(entry.value == jojo::kPs1HleExceptionHandlerAddress);

    const auto patch_window =
        runtime.bus().read32(jojo::kPs1HleExceptionHandlerAddress + 0x28u);
    CHECK(patch_window.status == jojo::R3000aBusStatus::ok);
    CHECK(patch_window.value == 0u);

    const auto b0_entry = runtime.bus().read32(
        jojo::kPs1HleB0TableAddress + 0x5Bu * sizeof(std::uint32_t));
    CHECK(b0_entry.status == jojo::R3000aBusStatus::ok);
    CHECK(b0_entry.value == jojo::kPs1HleChangeClearPadHandlerAddress);

    const auto b0_patch_window = runtime.bus().read32(
        jojo::kPs1HleChangeClearPadHandlerAddress + 0x9C8u);
    CHECK(b0_patch_window.status == jojo::R3000aBusStatus::ok);
    CHECK(b0_patch_window.value == 0u);
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
        test_mips::i(0x0Du, 8u, 8u, 0x1500u),
        test_mips::i(0x23u, 8u, 9u, 0u),
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.unsupported_access.has_value());
    if (report.unsupported_access) {
        CHECK(report.unsupported_access->guest_address == 0x1F801500u);
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
        test_mips::i(0x0Du, 8u, 8u, 0x1500u),
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
        CHECK(report.recent_mmio[0].address == 0x1F801500u);
        CHECK(report.recent_mmio[0].width == 4u);
        CHECK(report.recent_mmio[0].write);
        CHECK(report.recent_mmio[0].value == 0x00001234u);
        CHECK(report.recent_mmio[0].speculative);
        CHECK(report.recent_mmio[1].address == 0x1F801500u);
        CHECK(!report.recent_mmio[1].write);
        CHECK(report.recent_mmio[1].value == 0x00001234u);
        CHECK(report.recent_mmio[1].speculative);
    }
    CHECK(report.recent_trace.size() == 8u);
    CHECK(runtime.cpu_state().gpr[10] == 0x00001234u);
}


static void test_bios_gpu_cw_routes_command_to_real_gpu() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 4u, 0xE300u),
        test_mips::i(0x0Du, 4u, 4u, 0x0000u),
        test_mips::i(0x09u, 0u, 9u, 0x0049u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x1234u),
        test_mips::j(0x02u, 0x8001001Cu >> 2),
        0x00000000u,
    };

    auto runtime = make_runtime(words);
    const auto before = runtime.bus().hardware_services().gpu_gp0_word_count();
    const auto report = runtime.run({16u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(runtime.bus().hardware_services().gpu_gp0_word_count() == before + 1u);
    CHECK(runtime.cpu_state().gpr[2] == 0u);
    CHECK(runtime.cpu_state().gpr[16] == 0x1234u);
}

static void test_bios_gpu_status_and_gp1_use_real_gpu() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 4u, 0x0300u),
        test_mips::i(0x09u, 0u, 9u, 0x0048u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 9u, 0x004Du),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
    };

    auto runtime = make_runtime(words);
    const auto before = runtime.bus().hardware_services().gpu_gp1_command_count();
    const auto report = runtime.run({14u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(runtime.bus().hardware_services().gpu_gp1_command_count() == before + 1u);
    CHECK(runtime.cpu_state().gpr[2] == runtime.bus().hardware_services().gpu_status());
}

static void test_boot_report_captures_segment_gpu_activity() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x2Bu, 8u, 0u, 0x1810u),
        test_mips::i(0x0Fu, 0u, 9u, 0x0300u),
        test_mips::i(0x2Bu, 8u, 9u, 0x1814u),
        test_mips::j(0x02u, 0x80010010u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({8u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.gpu_gp0_command_count == 1u);
    CHECK(report.gpu_gp1_command_count == 1u);
    CHECK(report.vram_write_count == 0u);

    const auto second = runtime.run({4u});
    CHECK(second.gpu_gp0_command_count == 0u);
    CHECK(second.gpu_gp1_command_count == 0u);
    CHECK(second.vram_write_count == 0u);
}


static void test_cdrom_command_frontier_records_command_evidence() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1801u),
        test_mips::i(0x09u, 0u, 9u, 0x007Fu),
        test_mips::i(0x28u, 8u, 9u, 0x0000u),
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({16u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::device_command_unimplemented);
    CHECK(report.instructions_retired == 3u);
    CHECK(report.recent_cdrom_commands.size() == 1u);
    if (!report.recent_cdrom_commands.empty()) {
        CHECK(report.recent_cdrom_commands.back().command == 0x7Fu);
        CHECK(report.recent_cdrom_commands.back().index == 0u);
    }
    CHECK(report.unsupported_access.has_value());
    if (report.unsupported_access) {
        CHECK(report.unsupported_access->physical_address == 0x1F801801u);
    }
}

static void test_gpu_frontier_records_unsupported_gp0_command() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Fu, 0u, 9u, 0xFE00u),
        test_mips::i(0x2Bu, 8u, 9u, 0x1810u),
    };
    auto runtime = make_runtime(words);
    const auto report = runtime.run({16u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::gpu_command_unimplemented);
    CHECK(report.unsupported_gpu_gp0_command.has_value());
    if (report.unsupported_gpu_gp0_command) {
        CHECK(*report.unsupported_gpu_gp0_command == 0xFEu);
    }
    CHECK(!report.unsupported_gpu_gp1_command.has_value());
}

static void test_runtime_exposes_host_neutral_gpu_display_frame() {
    const std::vector<std::uint32_t> words{
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);

    CHECK(runtime.bus().write32(0x1F801810u, 0xA0000000u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x1F801810u, 0x00000000u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x1F801810u, (1u << 16u) | 2u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x1F801810u, 0x03E0001Fu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(runtime.bus().write32(0x1F801814u, 0x03000000u).status ==
          jojo::R3000aBusStatus::ok);

    const auto frame = runtime.display_frame();
    CHECK(frame.width == 256u);
    CHECK(frame.height == 240u);
    CHECK(frame.rgba8.size() == static_cast<std::size_t>(256u * 240u));
    if (frame.rgba8.size() >= 2u) {
        CHECK(frame.rgba8[0] == 0xFF0000FFu);
        CHECK(frame.rgba8[1] == 0xFF00FF00u);
    }
}



static void test_entercriticalsection_syscall_is_hle_without_vector_walk() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 4u, 1u), // a0 = SYS EnterCriticalSection
        0x0000000Cu,                     // SYSCALL
        test_mips::i(0x09u, 0u, 8u, 7u),
        0x00000000u,
    };
    auto runtime = make_runtime(words);

    const auto report = runtime.run({3u});
    CHECK(report.stop_reason ==
          jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.execution_steps == 3u);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.reference_instructions_retired == 2u);
    CHECK(runtime.cpu_state().gpr[8] == 7u);
    CHECK(runtime.cpu_state().pc == 0x8001000Cu);
    CHECK(runtime.cpu_state().cop0.epc == 0u);
    CHECK(((runtime.cpu_state().cop0.cause >> 2u) & 0x1Fu) == 0u);
}

static void test_retired_instruction_advances_hardware_once() {
    const std::vector<std::uint32_t> words{
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    CHECK(runtime.bus().hardware_services().timer_counter(0u) == 0u);
    const auto report = runtime.run({1u});
    CHECK(report.instructions_retired == 1u);
    CHECK(runtime.bus().hardware_services().timer_counter(0u) == 1u);
}

static void test_runtime_snapshot_restores_deterministically() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 8u, 8u, 1u),
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto runtime = make_runtime(words);
    runtime.set_native_x64_enabled(true);

    const auto warmup = runtime.run({9u});
    CHECK(warmup.stop_reason ==
          jojo::Ps1BootStopReason::execution_budget_exhausted);
    runtime.signal_vblank();
    runtime.bus().hardware_services().sio0().set_digital_pad_buttons(
        0u, 0x7FFFu);

    const auto snapshot = runtime.save_state();
    const auto before_hash = runtime.diagnostic_state_hash();
    const auto before_cpu = runtime.cpu_state();

    const auto first = runtime.run({18u});
    const auto first_hash = runtime.diagnostic_state_hash();
    const auto first_cpu = runtime.cpu_state();
    CHECK(first.stop_reason ==
          jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(first_hash != before_hash);

    const auto restored = runtime.load_state(snapshot);
    CHECK(restored);
    CHECK(runtime.diagnostic_state_hash() == before_hash);
    CHECK(runtime.cpu_state().gpr == before_cpu.gpr);
    CHECK(runtime.cpu_state().pc == before_cpu.pc);
    CHECK(runtime.cpu_state().next_pc == before_cpu.next_pc);
    CHECK(runtime.native_x64_enabled());

    const auto second = runtime.run({18u});
    CHECK(second.stop_reason == first.stop_reason);
    CHECK(second.instructions_retired == first.instructions_retired);
    CHECK(second.native_x64_instructions_retired ==
          first.native_x64_instructions_retired);
    CHECK(runtime.diagnostic_state_hash() == first_hash);
    CHECK(runtime.cpu_state().gpr == first_cpu.gpr);
    CHECK(runtime.cpu_state().pc == first_cpu.pc);
    CHECK(runtime.cpu_state().next_pc == first_cpu.next_pc);
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
    test_runtime_starts_from_post_bios_cdrom_handoff();
    test_vblank_routes_to_r3000a_hardware_irq2();
    test_runtime_continues_vblank_through_hookentryint();
    test_runtime_initializes_clean_room_c0_exception_entry();
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
    test_bios_gpu_cw_routes_command_to_real_gpu();
    test_bios_gpu_status_and_gp1_use_real_gpu();
    test_boot_report_captures_segment_gpu_activity();
    test_cdrom_command_frontier_records_command_evidence();
    test_gpu_frontier_records_unsupported_gp0_command();
    test_runtime_exposes_host_neutral_gpu_display_frame();
    test_entercriticalsection_syscall_is_hle_without_vector_walk();
    test_retired_instruction_advances_hardware_once();
    test_runtime_snapshot_restores_deterministically();
    test_deterministic_replay_matches_full_m3a_state();
    return failures ? 1 : 0;
}
