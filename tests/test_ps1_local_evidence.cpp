#include "core/ps1_installation.h"
#include "core/ps1_max3_explorer.h"
#include "core/runtime.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::vector<std::string> regular_files_under(const fs::path& root) {
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && !ec) {
            files.push_back(fs::relative(it->path(), root, ec).generic_string());
            if (ec) break;
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

int main() {
    const auto root = fs::temp_directory_path() / "jojo-ps1-local-evidence";
    const auto source = root / "game.iso";
    const auto install = root / "install";
    const auto report_path = root / "diagnostics" / "m3a-checkpoint.txt";

    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    auto disc = test_ps1::make_disc_fixture();
    disc.executable = test_ps1::make_psx_exe_from_words({
        0x08000028u, // j 0x800000A0 (synthetic unknown BIOS selector)
        0x00000000u, // delay-slot nop
    });
    test_ps1::write_cooked_iso(source, disc);

    jojo::ConversionOptions options{};
    options.revision_profiles.push_back(test_ps1::make_revision_profile(disc));
    const auto converted = jojo::convert_image(source, install, options);
    CHECK(converted);

    const auto resolved = jojo::resolve_active_install_generation(install);
    CHECK(resolved);
    if (!resolved) {
        fs::remove_all(root, ec);
        return 1;
    }

    const auto manifest_path = resolved.value.manifest_path;
    const auto manifest_before = read_text(manifest_path);
    const auto files_before = regular_files_under(resolved.value.generation_dir);

    const auto max3_options = jojo::ps1_max3_local_evidence_options();
    CHECK(max3_options.max_nodes == 5461u);
    CHECK(max3_options.max_branch_depth == 6u);
    CHECK(max3_options.max_total_retired == 1000000000ull);
    CHECK(max3_options.segment_options.instruction_budget == std::numeric_limits<std::uint64_t>::max());
    CHECK(max3_options.segment_options.trace_capacity == 131072u);
    CHECK(max3_options.segment_options.diagnostic_mmio_probe);
    CHECK(max3_options.segment_options.mmio_event_capacity == 65536u);
    CHECK(max3_options.segment_options.bios_event_capacity == 65536u);
    CHECK(max3_options.segment_options.stagnation_instruction_limit == 2000000u);

    auto fast_options = max3_options;
    fast_options.max_nodes = 5u;
    fast_options.max_branch_depth = 1u;
    fast_options.max_total_retired = 128u;
    fast_options.segment_options.trace_capacity = 16u;
    fast_options.segment_options.mmio_event_capacity = 16u;
    fast_options.segment_options.bios_event_capacity = 16u;
    fast_options.segment_options.stagnation_instruction_limit = 16u;

    const auto max3 = jojo::bootstrap_runtime_max3_local_evidence_to_file(
        install, report_path, fast_options);
    CHECK(max3);
    if (max3) {
        CHECK(!max3.value.nodes.empty());
        CHECK(max3.value.nodes.front().stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
        CHECK(max3.value.nodes.front().frontier_table == 0x000000A0u);
    }

    CHECK(fs::is_regular_file(report_path));
    const auto report = read_text(report_path);
    CHECK(report.find("format=jojo-max3-checkpoint-v1\n") == 0u);
    CHECK(report.find("node_count=") != std::string::npos);
    CHECK(report.find("dependency_count=") != std::string::npos);
    CHECK(report.find("best_report_begin=1\n") != std::string::npos);
    CHECK(report.find("best_report_end=1\n") != std::string::npos);
    CHECK(report.find("PS-X EXE") == std::string::npos);
    CHECK(read_text(manifest_path) == manifest_before);
    CHECK(regular_files_under(resolved.value.generation_dir) == files_before);

    // Existing button/runtime-facing API remains source compatible but now writes MAX3.
    const auto compatibility_path = root / "diagnostics" / "compat-checkpoint.txt";
    const auto checkpoint = jojo::bootstrap_runtime_local_evidence_to_file(
        install, compatibility_path);
    CHECK(checkpoint);
    CHECK(fs::is_regular_file(compatibility_path));
    CHECK(read_text(compatibility_path).find("format=jojo-max3-checkpoint-v1\n") == 0u);

    fs::remove_all(root, ec);
    if (failures) {
        std::cerr << failures << " PS1 MAX3 local-evidence assertion(s) failed\n";
        return 1;
    }
    std::cout << "PS1 MAX3 local-evidence assertions passed\n";
    return 0;
}
