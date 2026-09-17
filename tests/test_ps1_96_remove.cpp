#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1BootRuntime make_runtime(std::uint16_t selector) {
    const std::vector<std::uint32_t> words{
        test_mips::i(0x09u, 0u, 2u, 0x1234u),
        test_mips::i(0x09u, 0u, 9u, selector),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x5678u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    };
    auto executable = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(executable);
    auto runtime = executable ? jojo::Ps1BootRuntime::create(executable.value)
                              : jojo::Result<jojo::Ps1BootRuntime>::failure(
                                    jojo::ErrorCode::invalid_installation,
                                    "synthetic executable parse failed");
    CHECK(runtime);
    return runtime ? std::move(runtime.value) : jojo::Ps1BootRuntime{};
}

static void check_alias(std::uint16_t selector) {
    auto runtime = make_runtime(selector);
    const auto report = runtime.run({16u});
    CHECK(report.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.bios_call_count == 1u);
    CHECK(runtime.bios_iso9660_removed());
    CHECK(runtime.cpu_state().gpr[2] == 0x00001234u);
    CHECK(runtime.cpu_state().gpr[16] == 0x00005678u);
    CHECK(report.recent_bios_calls.size() == 1u);
    if (!report.recent_bios_calls.empty()) {
        CHECK(report.recent_bios_calls.back().table_physical == 0x000000A0u);
        CHECK(report.recent_bios_calls.back().selector == selector);
        CHECK(report.recent_bios_calls.back().ra == 0x80010014u);
    }
}

int main() {
    check_alias(0x0056u);
    check_alias(0x0072u);
    return failures ? 1 : 0;
}
