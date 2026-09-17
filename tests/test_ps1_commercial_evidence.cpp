#include "core/ps1_commercial_evidence.h"
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
}

int main() {
    const auto temp = fs::temp_directory_path() / "jojo_phase2_commercial_evidence";
    std::error_code ec;
    fs::remove_all(temp, ec);
    fs::create_directories(temp, ec);
    CHECK(!ec);

    auto fixture = test_ps1::make_disc_fixture();
    const auto source = test_ps1::write_cooked_iso(temp / "jojo.iso", fixture);
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

    const auto after = read_all(source);
    CHECK(before == after);

    fs::remove_all(temp, ec);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "commercial evidence runner passed\n";
    return 0;
}
