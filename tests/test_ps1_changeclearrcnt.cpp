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

static void test_max_v2_options_expand_evidence_history() {
    const auto options = jojo::ps1_local_evidence_options();
    CHECK(options.instruction_budget == std::numeric_limits<std::uint64_t>::max());
    CHECK(options.trace_capacity == 4096u);
    CHECK(options.diagnostic_mmio_probe);
    CHECK(options.mmio_event_capacity == 8192u);
    CHECK(options.bios_event_capacity == 4096u);
}

static void test_c0_0a_changeclearrcnt_tracks_vblank_flag_and_returns_previous() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 4u, 0x0003u),
        test_mips::i(0x09u, 0u, 5u, 0x0000u),
        test_mips::i(0x09u, 0u, 9u, 0x000Au),
        test_mips::i(0x09u, 0u, 10u, 0x00C0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::r(2u, 0u, 16u, 0u, 0x21u),
        test_mips::i(0x09u, 0u, 5u, 0x0001u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::r(2u, 0u, 17u, 0u, 0x21u),
        test_mips::j(0x02u, 0x8001002Cu >> 2),
        0x00000000u,
    };

    auto runtime = make_runtime(words);
    const auto report = runtime.run({24u});

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.bios_call_count == 2u);
    CHECK(runtime.cpu_state().gpr[17] == 0u);
    CHECK(runtime.bios_root_counter_auto_ack_enabled(3u).has_value());
    if (runtime.bios_root_counter_auto_ack_enabled(3u)) {
        CHECK(*runtime.bios_root_counter_auto_ack_enabled(3u));
    }
}

static void test_recent_bios_history_is_bounded_independently_of_total_calls() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 4u, 0x0000u),
        test_mips::i(0x09u, 0u, 9u, 0x005Bu),
        test_mips::i(0x09u, 0u, 10u, 0x00B0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::j(0x02u, 0x8001000Cu >> 2),
        0x00000000u,
    };

    auto runtime = make_runtime(words);
    jojo::Ps1BootOptions options{};
    options.instruction_budget = 24u;
    options.trace_capacity = 0u;
    options.bios_event_capacity = 2u;
    const auto report = runtime.run(options);

    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.bios_call_count > 2u);
    CHECK(report.recent_bios_calls.size() == 2u);
    if (report.recent_bios_calls.size() == 2u) {
        CHECK(report.recent_bios_calls[0].selector == 0x0000005Bu);
        CHECK(report.recent_bios_calls[1].selector == 0x0000005Bu);
    }
}

int main() {
    test_max_v2_options_expand_evidence_history();
    test_c0_0a_changeclearrcnt_tracks_vblank_flag_and_returns_previous();
    test_recent_bios_history_is_bounded_independently_of_total_calls();
    return failures ? 1 : 0;
}
