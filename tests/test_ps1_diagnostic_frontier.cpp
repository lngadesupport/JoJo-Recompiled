#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <array>
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

static jojo::Ps1BootRuntime make_unknown_bios_runtime() {
    return make_runtime({
        test_mips::i(0x09u, 0u, 2u, 0x1234u),
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x5678u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    });
}

static void test_unknown_bios_frontier_can_branch_from_snapshot() {
    auto stopped = make_unknown_bios_runtime();
    CHECK(!stopped.apply_diagnostic_bios_fallback(jojo::Ps1BiosFallback::return_zero));

    const auto frontier = stopped.run({16u});
    CHECK(frontier.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(frontier.last_pc == 0x000000A0u);
    CHECK(stopped.cpu_state().gpr[2] == 0x00001234u);

    const std::array<jojo::Ps1BiosFallback, 4> policies{
        jojo::Ps1BiosFallback::return_zero,
        jojo::Ps1BiosFallback::return_one,
        jojo::Ps1BiosFallback::return_minus_one,
        jojo::Ps1BiosFallback::preserve_v0,
    };
    const std::array<std::uint32_t, 4> expected{
        0u, 1u, 0xFFFFFFFFu, 0x00001234u,
    };

    for (std::size_t i = 0; i < policies.size(); ++i) {
        auto child = stopped;
        CHECK(child.apply_diagnostic_bios_fallback(policies[i]));
        CHECK(child.cpu_state().gpr[2] == expected[i]);
        CHECK(!child.apply_diagnostic_bios_fallback(policies[i]));
        const auto resumed = child.run({2u});
        CHECK(resumed.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
        CHECK(resumed.instructions_retired == 2u);
        CHECK(child.cpu_state().gpr[16] == 0x00005678u);
    }
}

static void test_stagnation_watchdog_stops_tight_loop() {
    auto runtime = make_runtime({
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    });
    jojo::Ps1BootOptions options{};
    options.instruction_budget = std::numeric_limits<std::uint64_t>::max();
    options.trace_capacity = 8u;
    options.stagnation_instruction_limit = 64u;
    const auto report = runtime.run(options);
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::diagnostic_stall);
    CHECK(report.instructions_retired == 64u);
    CHECK(report.recent_trace.size() == 8u);
}

static void test_diagnostic_state_fingerprint_tracks_guest_state() {
    const std::vector<std::uint32_t> loop{
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    };
    auto baseline = make_runtime(loop);
    auto identical = make_runtime(loop);
    CHECK(baseline.diagnostic_state_hash() == identical.diagnostic_state_hash());

    auto ram_changed = baseline;
    CHECK(ram_changed.bus().write32(0x00000200u, 0x12345678u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(ram_changed.diagnostic_state_hash() != baseline.diagnostic_state_hash());

    auto dicr_changed = baseline;
    CHECK(dicr_changed.bus().write32(0x1F8010F4u, 0x00000001u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(dicr_changed.diagnostic_state_hash() != baseline.diagnostic_state_hash());

    auto shadow_changed = baseline;
    shadow_changed.bus().set_diagnostic_mmio_probe_enabled(true);
    CHECK(shadow_changed.bus().write32(0x1F801080u, 0xA5A55A5Au).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(shadow_changed.diagnostic_state_hash() != baseline.diagnostic_state_hash());

    auto frontier = make_unknown_bios_runtime();
    CHECK(frontier.run({16u}).stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    auto zero = frontier;
    auto one = frontier;
    CHECK(zero.apply_diagnostic_bios_fallback(jojo::Ps1BiosFallback::return_zero));
    CHECK(one.apply_diagnostic_bios_fallback(jojo::Ps1BiosFallback::return_one));
    CHECK(zero.diagnostic_state_hash() != one.diagnostic_state_hash());
}

int main() {
    test_unknown_bios_frontier_can_branch_from_snapshot();
    test_stagnation_watchdog_stops_tight_loop();
    test_diagnostic_state_fingerprint_tracks_guest_state();
    return failures ? 1 : 0;
}
