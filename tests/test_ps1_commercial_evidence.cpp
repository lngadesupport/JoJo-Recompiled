#include "core/ps1_commercial_evidence.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;
namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

std::vector<std::uint8_t> read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void test_normal_mode_retains_disc_and_never_mutates_source(const fs::path& temp) {
    auto fixture = test_ps1::make_disc_fixture();
    const auto source = test_ps1::write_cooked_iso(temp / "normal.iso", fixture);
    const auto before = read_all(source);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture));

    auto opened = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(opened);
    if (opened) {
        CHECK(opened.value.disc_session().binding().revision_id == "synthetic-ps1-jojo");
        auto sector = opened.value.disc_session().read_sectors(16u, 1u);
        CHECK(sector);
        if (sector) CHECK(sector.value.size() == 2048u);

        jojo::Ps1CommercialEvidenceOptions options{};
        options.boot.instruction_budget = 4u;
        options.boot.trace_capacity = 2u;
        options.boot.mmio_event_capacity = 2u;
        options.boot.bios_event_capacity = 2u;

        const auto report = opened.value.run(options);
        CHECK(report.source.revision_id == "synthetic-ps1-jojo");
        CHECK(report.frontier == jojo::Ps1CommercialFrontierClass::execution_budget);
        CHECK(report.boot.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
        CHECK(report.boot.instructions_retired == 4u);
        CHECK(report.total_instructions_retired == 4u);
        CHECK(report.diagnostic_decisions.empty());
        CHECK(report.boot.recent_trace.size() <= 2u);
        CHECK(report.boot.recent_bios_calls.size() <= 2u);
        CHECK(report.boot.recent_mmio.size() <= 2u);
    }

    CHECK(before == read_all(source));
}

void test_bios_fallback_is_opt_in_and_recorded(const fs::path& temp) {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.executable = test_ps1::make_psx_exe_from_words({
        test_mips::i(0x09u, 0u, 2u, 0x1234u),
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x5678u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    });
    const auto source = test_ps1::write_cooked_iso(temp / "bios.iso", fixture);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture, "synthetic-bios-frontier"));

    jojo::Ps1CommercialEvidenceOptions normal{};
    normal.boot.instruction_budget = 16u;

    auto normal_runner = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(normal_runner);
    if (normal_runner) {
        const auto report = normal_runner.value.run(normal);
        CHECK(report.frontier == jojo::Ps1CommercialFrontierClass::bios_call);
        CHECK(report.boot.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
        CHECK(report.diagnostic_decisions.empty());
    }

    auto diagnostic_runner = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(diagnostic_runner);
    if (diagnostic_runner) {
        jojo::Ps1CommercialEvidenceOptions diagnostic = normal;
        diagnostic.diagnostic_bios_fallbacks.push_back(jojo::Ps1BiosFallback::return_zero);
        const auto report = diagnostic_runner.value.run(diagnostic);
        CHECK(report.diagnostic_decisions.size() == 1u);
        if (!report.diagnostic_decisions.empty()) {
            CHECK(report.diagnostic_decisions[0].bios_table == 0x000000A0u);
            CHECK(report.diagnostic_decisions[0].bios_selector == 0x00000033u);
            CHECK(report.diagnostic_decisions[0].fallback == jojo::Ps1BiosFallback::return_zero);
        }
        CHECK(report.total_instructions_retired > report.boot.instructions_retired);
        CHECK(report.frontier != jojo::Ps1CommercialFrontierClass::bios_call);
    }
}
}

int main() {
    const auto temp = fs::temp_directory_path() / "jojo_phase2_commercial_evidence";
    std::error_code ec;
    fs::remove_all(temp, ec);
    fs::create_directories(temp, ec);
    CHECK(!ec);

    test_normal_mode_retains_disc_and_never_mutates_source(temp);
    test_bios_fallback_is_opt_in_and_recorded(temp);

    fs::remove_all(temp, ec);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "commercial evidence runner passed\n";
    return 0;
}
