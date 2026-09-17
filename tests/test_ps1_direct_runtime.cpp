#include "core/runtime.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path workspace(std::string_view suffix) {
    auto root = fs::temp_directory_path() / ("jojo-ps1-direct-runtime-" + std::string(suffix));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

static jojo::Ps1DiscOpenOptions options_for(const test_ps1::Ps1DiscFixture& fixture) {
    jojo::Ps1DiscOpenOptions options{};
    options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture));
    return options;
}

static std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
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

static void test_checkpoint_executes_directly_from_owned_image_without_materialization() {
    const auto root = workspace("checkpoint");
    const auto source = root / "game.iso";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_cooked_iso(source, fixture);

    const auto source_before = read_bytes(source);
    const auto files_before = regular_files_under(root);

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 4u;
    const auto checkpoint = jojo::bootstrap_runtime_checkpoint_from_disc(
        source, options_for(fixture), options);

    CHECK(checkpoint);
    if (checkpoint) {
        CHECK(checkpoint.value.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
        CHECK(checkpoint.value.instructions_retired == 4u);
        CHECK(checkpoint.value.last_pc == 0x8001000Cu);
        CHECK(checkpoint.value.presented_frames == 0u);
    }

    CHECK(read_bytes(source) == source_before);
    CHECK(regular_files_under(root) == files_before);
    CHECK(!fs::exists(root / "install"));
    CHECK(!fs::exists(root / "generations"));
    CHECK(!fs::exists(root / "active_install.ini"));
    CHECK(!fs::exists(root / "boot.psxexe"));

    std::error_code ec;
    fs::remove_all(root, ec);
}

static void test_checkpoint_report_adds_only_requested_bounded_diagnostic() {
    const auto root = workspace("report");
    const auto source = root / "game.iso";
    const auto report_path = root / "diagnostics" / "m3a-checkpoint.txt";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_cooked_iso(source, fixture);

    const auto source_before = read_bytes(source);
    auto expected_files = regular_files_under(root);
    expected_files.push_back("diagnostics/m3a-checkpoint.txt");
    std::sort(expected_files.begin(), expected_files.end());

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 4u;
    const auto checkpoint = jojo::bootstrap_runtime_checkpoint_from_disc_to_file(
        source, options_for(fixture), report_path, options);

    CHECK(checkpoint);
    CHECK(fs::is_regular_file(report_path));
    CHECK(read_bytes(source) == source_before);
    CHECK(regular_files_under(root) == expected_files);
    CHECK(!fs::exists(root / "generations"));
    CHECK(!fs::exists(root / "active_install.ini"));
    CHECK(!fs::exists(root / "boot.psxexe"));

    if (checkpoint) {
        CHECK(checkpoint.value.instructions_retired == 4u);
        CHECK(checkpoint.value.last_pc == 0x8001000Cu);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
}

int main() {
    test_checkpoint_executes_directly_from_owned_image_without_materialization();
    test_checkpoint_report_adds_only_requested_bounded_diagnostic();
    if (failures) {
        std::cerr << failures << " PS1 direct-runtime assertion(s) failed\n";
        return 1;
    }
    std::cout << "all PS1 direct-runtime assertions passed\n";
    return 0;
}
