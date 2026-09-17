#include "core/ps1_disc_session.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path workspace(std::string_view suffix) {
    auto root = fs::temp_directory_path() / ("jojo-ps1-disc-session-" + std::string(suffix));
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

static void assert_session(const jojo::Ps1DiscSession& session,
                           const fs::path& selected_source,
                           std::string_view expected_format,
                           std::uint64_t expected_backing_size,
                           const test_ps1::Ps1DiscFixture& fixture) {
    CHECK(session.binding().source_path == fs::absolute(selected_source).lexically_normal());
    CHECK(session.binding().source_format == expected_format);
    CHECK(session.binding().source_size == expected_backing_size);
    CHECK(session.binding().source_hash_fnv1a64.size() == 16u);
    CHECK(session.binding().revision_id == "synthetic-ps1-jojo");
    CHECK(session.system_cnf().boot_iso_path == "/SLUS_TEST.00");
    CHECK(session.boot_executable().metadata.entry_pc == 0x80010000u);
    CHECK(session.boot_executable().file_bytes == fixture.executable);

    const auto asset = session.read_file("/DATA/ASSET.DAT");
    CHECK(asset);
    if (asset) CHECK(asset.value == fixture.asset);

    const auto pvd = session.read_sectors(16u, 1u);
    CHECK(pvd);
    if (pvd) {
        CHECK(pvd.value.size() == 2048u);
        CHECK(pvd.value[0] == 1u);
        CHECK(std::string(pvd.value.begin() + 1, pvd.value.begin() + 6) == "CD001");
    }
}

static void test_cooked_iso_opens_without_materializing_files() {
    const auto root = workspace("iso");
    const auto source = root / "game.iso";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_cooked_iso(source, fixture);
    const auto files_before = regular_files_under(root);

    const auto opened = jojo::Ps1DiscSession::open(source, options_for(fixture));
    CHECK(opened);
    if (opened) assert_session(opened.value, source, "iso", fs::file_size(source), fixture);
    CHECK(regular_files_under(root) == files_before);

    std::error_code ec;
    fs::remove_all(root, ec);
}

static void test_mode2_bin_opens_without_materializing_files() {
    const auto root = workspace("bin");
    const auto cooked = root / "source.iso";
    const auto source = root / "game.bin";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_mode2_bin(cooked, source, fixture);
    std::error_code ec;
    fs::remove(cooked, ec);
    const auto files_before = regular_files_under(root);

    const auto opened = jojo::Ps1DiscSession::open(source, options_for(fixture));
    CHECK(opened);
    if (opened) assert_session(opened.value, source, "bin", fs::file_size(source), fixture);
    CHECK(regular_files_under(root) == files_before);

    fs::remove_all(root, ec);
}

static void test_cue_binds_descriptor_but_fingerprints_backing_bin() {
    const auto root = workspace("cue");
    const auto cooked = root / "source.iso";
    const auto bin = root / "track.bin";
    const auto cue = root / "game.cue";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_mode2_bin(cooked, bin, fixture);
    std::error_code ec;
    fs::remove(cooked, ec);
    {
        std::ofstream out(cue, std::ios::trunc);
        out << "FILE \"track.bin\" BINARY\n"
               "  TRACK 01 MODE2/2352\n"
               "    INDEX 01 00:00:00\n";
    }
    const auto files_before = regular_files_under(root);

    const auto opened = jojo::Ps1DiscSession::open(cue, options_for(fixture));
    CHECK(opened);
    if (opened) assert_session(opened.value, cue, "cue", fs::file_size(bin), fixture);
    CHECK(regular_files_under(root) == files_before);

    fs::remove_all(root, ec);
}

static void test_gdi_remains_rejected() {
    const auto opened = jojo::Ps1DiscSession::open("game.gdi");
    CHECK(!opened);
    if (!opened) CHECK(opened.error == jojo::ErrorCode::unsupported_format);
}

int main() {
    test_cooked_iso_opens_without_materializing_files();
    test_mode2_bin_opens_without_materializing_files();
    test_cue_binds_descriptor_but_fingerprints_backing_bin();
    test_gdi_remains_rejected();
    if (failures) {
        std::cerr << failures << " PS1 disc-session assertion(s) failed\n";
        return 1;
    }
    std::cout << "all PS1 disc-session assertions passed\n";
    return 0;
}
