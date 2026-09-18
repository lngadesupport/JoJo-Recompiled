#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

jojo::Ps1BootRuntime make_runtime(
    const std::vector<std::uint32_t>& words,
    bool native_enabled) {
    auto executable =
        jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(executable);
    auto runtime = executable
        ? jojo::Ps1BootRuntime::create(executable.value)
        : jojo::Result<jojo::Ps1BootRuntime>::failure(
              jojo::ErrorCode::invalid_installation,
              "synthetic executable parse failed");
    CHECK(runtime);
    if (!runtime) return {};
    runtime.value.set_native_x64_enabled(native_enabled);
    return std::move(runtime.value);
}

void check_cpu_equal(
    const jojo::R3000aState& lhs,
    const jojo::R3000aState& rhs) {
    CHECK(lhs.gpr == rhs.gpr);
    CHECK(lhs.hi == rhs.hi);
    CHECK(lhs.lo == rhs.lo);
    CHECK(lhs.pc == rhs.pc);
    CHECK(lhs.next_pc == rhs.next_pc);
    CHECK(lhs.pending_load.valid == rhs.pending_load.valid);
    CHECK(lhs.pending_load.reg == rhs.pending_load.reg);
    CHECK(lhs.pending_load.value == rhs.pending_load.value);
    CHECK(lhs.delay_slot.active == rhs.delay_slot.active);
    CHECK(lhs.delay_slot.branch_pc == rhs.delay_slot.branch_pc);
    CHECK(lhs.delay_slot.taken == rhs.delay_slot.taken);
    CHECK(lhs.delay_slot.target == rhs.delay_slot.target);
}

void test_opt_in_native_runtime_matches_reference_for_safe_alu() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1234u),
        test_mips::i(0x0Du, 8u, 8u, 0x00F0u),
        test_mips::i(0x09u, 8u, 9u, 0xFFFFu),
        test_mips::r(8u, 9u, 10u, 0u, 0x26u),
    };

    auto native = make_runtime(words, true);
    auto reference = make_runtime(words, false);

    const auto native_report = native.run({4u});
    const auto reference_report = reference.run({4u});

    CHECK(native_report.stop_reason ==
          jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(reference_report.stop_reason == native_report.stop_reason);
    CHECK(native_report.instructions_retired == 4u);
    CHECK(reference_report.instructions_retired == 4u);
    CHECK(native_report.native_x64_instructions_retired +
              native_report.reference_instructions_retired ==
          native_report.instructions_retired);
    CHECK(reference_report.native_x64_instructions_retired == 0u);
    CHECK(reference_report.reference_instructions_retired == 4u);
#if defined(_WIN32) && defined(_M_X64)
    CHECK(native_report.native_x64_instructions_retired == 4u);
    CHECK(native_report.reference_instructions_retired == 0u);
#else
    CHECK(native_report.native_x64_instructions_retired == 0u);
    CHECK(native_report.reference_instructions_retired == 4u);
#endif

    check_cpu_equal(native.cpu_state(), reference.cpu_state());
    CHECK(native.bus().hardware_services().timer_counter(0u) == 4u);
    CHECK(reference.bus().hardware_services().timer_counter(0u) == 4u);
}

void test_branch_and_delay_slot_remain_reference() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x04u, 0u, 0u, 1u),
        test_mips::i(0x09u, 0u, 8u, 7u),
        0x00000000u,
    };
    auto runtime = make_runtime(words, true);
    const auto report = runtime.run({2u});
    CHECK(report.instructions_retired == 2u);
#if defined(_WIN32) && defined(_M_X64)
    CHECK(report.native_x64_instructions_retired == 1u);
    CHECK(report.reference_instructions_retired == 1u);
#else
    CHECK(report.native_x64_instructions_retired == 0u);
    CHECK(report.reference_instructions_retired == 2u);
#endif
    CHECK(runtime.cpu_state().gpr[8] == 7u);
    CHECK(runtime.cpu_state().pc == 0x80010008u);
}

void test_pending_load_forces_one_instruction_reference_fallback() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x0000u),
        test_mips::i(0x23u, 8u, 9u, 0x0000u),
        test_mips::i(0x09u, 9u, 10u, 1u),
        test_mips::i(0x09u, 9u, 11u, 2u),
    };
    auto runtime = make_runtime(words, true);
    const auto report = runtime.run({4u});
    CHECK(report.instructions_retired == 4u);
    CHECK(report.reference_instructions_retired >= 2u);
#if defined(_WIN32) && defined(_M_X64)
    CHECK(report.native_x64_instructions_retired >= 1u);
#endif
    CHECK(runtime.cpu_state().gpr[10] == 1u);
    CHECK(runtime.cpu_state().gpr[11] == 2u);
}

void test_mmio_frontier_is_unchanged_with_native_enabled() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),
        test_mips::i(0x0Du, 8u, 8u, 0x1500u),
        test_mips::i(0x23u, 8u, 9u, 0u),
    };
    auto runtime = make_runtime(words, true);
    const auto report = runtime.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(report.instructions_retired == 2u);
    CHECK(report.unsupported_access.has_value());
    if (report.unsupported_access) {
        CHECK(report.unsupported_access->physical_address == 0x1F801500u);
    }
    CHECK(report.native_x64_instructions_retired +
              report.reference_instructions_retired ==
          report.instructions_retired);
}

void test_native_mode_defaults_off() {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 8u, 1u),
    };
    auto runtime = make_runtime(words, false);
    CHECK(!runtime.native_x64_enabled());
    const auto report = runtime.run({1u});
    CHECK(report.native_x64_instructions_retired == 0u);
    CHECK(report.reference_instructions_retired == 1u);
}
} // namespace

int main() {
    test_opt_in_native_runtime_matches_reference_for_safe_alu();
    test_branch_and_delay_slot_remain_reference();
    test_pending_load_forces_one_instruction_reference_fallback();
    test_mmio_frontier_is_unchanged_with_native_enabled();
    test_native_mode_defaults_off();
    return failures ? 1 : 0;
}
