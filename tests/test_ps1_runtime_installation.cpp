#include "core/runtime.h"
#include "core/ps1_boot_report.h"
#include "core/ps1_installation.h"
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

struct ConvertedFixture {
    fs::path root;
    fs::path source;
    fs::path install;
    test_ps1::Ps1DiscFixture disc;
};

static ConvertedFixture make_converted(std::string_view suffix) {
    ConvertedFixture fixture{};
    fixture.root = fs::temp_directory_path() / ("jojo-ps1-runtime-" + std::string(suffix));
    fixture.source = fixture.root / "game.iso";
    fixture.install = fixture.root / "install";
    fixture.disc = test_ps1::make_disc_fixture();

    std::error_code ec;
    fs::remove_all(fixture.root, ec);
    fs::create_directories(fixture.root, ec);
    test_ps1::write_cooked_iso(fixture.source, fixture.disc);

    jojo::ConversionOptions options{};
    options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture.disc));
    const auto converted = jojo::convert_image(fixture.source, fixture.install, options);
    CHECK(converted);
    return fixture;
}

static void cleanup(const ConvertedFixture& fixture) {
    std::error_code ec;
    fs::remove_all(fixture.root, ec);
}

static fs::path generation_dir(const ConvertedFixture& fixture) {
    const auto resolved = jojo::resolve_active_install_generation(fixture.install);
    CHECK(resolved);
    return resolved ? resolved.value.generation_dir : fs::path{};
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
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

static void test_absent_installation_is_classified() {
    const auto root = fs::temp_directory_path() / "jojo-ps1-runtime-absent";
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto kind = jojo::classify_installation(root);
    CHECK(kind);
    if (kind) CHECK(kind.value == jojo::InstallationKind::absent);
}

static void test_valid_m1_install_survives_source_deletion() {
    auto fixture = make_converted("valid");
    std::error_code ec;
    fs::remove(fixture.source, ec);
    CHECK(!ec);
    CHECK(!fs::exists(fixture.source));

    const auto kind = jojo::classify_installation(fixture.install);
    CHECK(kind);
    if (kind) CHECK(kind.value == jojo::InstallationKind::ps1_m1);

    const auto validated = jojo::validate_installation(fixture.install);
    CHECK(validated);
    if (validated) {
        CHECK(validated.value.install_root == fixture.install);
        CHECK(validated.value.generation_dir == generation_dir(fixture));
        CHECK(validated.value.manifest.manifest_version == "2");
        CHECK(validated.value.manifest.platform == "playstation");
        CHECK(validated.value.manifest.media_status == "verified");
        CHECK(validated.value.manifest.executable_status == "verified");
    }
    cleanup(fixture);
}

static void test_legacy_v1_is_classified_but_not_validated_as_ps1() {
    const auto root = fs::temp_directory_path() / "jojo-ps1-runtime-legacy";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    jojo::ConversionManifest legacy{};
    legacy.manifest_version = "1";
    legacy.converter_version = "legacy-test";
    legacy.source_name = "owned.iso";
    legacy.source_format = "iso";
    legacy.source_size = 1234u;
    legacy.hash_hex = "0123456789abcdef";
    legacy.revision_id = "legacy-synthetic";
    CHECK(jojo::save_conversion_manifest_atomic(root / "game_manifest.ini", legacy));

    const auto kind = jojo::classify_installation(root);
    CHECK(kind);
    if (kind) CHECK(kind.value == jojo::InstallationKind::legacy_v1);

    const auto validated = jojo::validate_installation(root);
    CHECK(!validated);
    if (!validated) {
        CHECK(validated.detail.find("legacy") != std::string::npos);
        CHECK(validated.detail.find("Dreamcast/SH-4") != std::string::npos);
    }
    fs::remove_all(root, ec);
}

static void test_wrong_platform_is_rejected() {
    auto fixture = make_converted("platform");
    const auto generation = generation_dir(fixture);
    const auto manifest_path = generation / "game_manifest.ini";
    auto text = read_text(manifest_path);
    const std::string from = "platform=playstation";
    const auto pos = text.find(from);
    CHECK(pos != std::string::npos);
    if (pos != std::string::npos) text.replace(pos, from.size(), "platform=dreamcast");
    write_text(manifest_path, text);

    const auto validated = jojo::validate_installation(fixture.install);
    CHECK(!validated);
    cleanup(fixture);
}

static void test_missing_local_executable_is_rejected() {
    auto fixture = make_converted("missing-exe");
    std::error_code ec;
    fs::remove(generation_dir(fixture) / "data/boot.psxexe", ec);
    CHECK(!ec);
    const auto validated = jojo::validate_installation(fixture.install);
    CHECK(!validated);
    cleanup(fixture);
}

static void test_modified_local_executable_hash_is_rejected() {
    auto fixture = make_converted("modified-exe");
    const auto exe = generation_dir(fixture) / "data/boot.psxexe";
    {
        std::fstream file(exe, std::ios::binary | std::ios::in | std::ios::out);
        CHECK(file.good());
        file.seekg(0x800);
        char byte{};
        file.read(&byte, 1);
        byte ^= 0x01;
        file.seekp(0x800);
        file.write(&byte, 1);
    }
    const auto validated = jojo::validate_installation(fixture.install);
    CHECK(!validated);
    cleanup(fixture);
}

static void test_active_pointer_to_missing_generation_is_rejected() {
    auto fixture = make_converted("missing-generation");
    write_text(fixture.install / "active_install.ini",
               "format=1\n"
               "generation_id=generation-999999\n"
               "manifest=generations/generation-999999/game_manifest.ini\n");
    const auto kind = jojo::classify_installation(fixture.install);
    CHECK(!kind);
    const auto validated = jojo::validate_installation(fixture.install);
    CHECK(!validated);
    cleanup(fixture);
}

static void test_manifest_metadata_mismatch_is_rejected() {
    auto fixture = make_converted("metadata");
    const auto manifest_path = generation_dir(fixture) / "game_manifest.ini";
    auto manifest = jojo::load_conversion_manifest(manifest_path);
    CHECK(manifest);
    if (manifest && manifest.value.psx_exe_entry.has_value()) {
        manifest.value.psx_exe_entry = *manifest.value.psx_exe_entry + 4u;
        CHECK(jojo::save_conversion_manifest_atomic(manifest_path, manifest.value));
    }
    const auto validated = jojo::validate_installation(fixture.install);
    CHECK(!validated);
    cleanup(fixture);
}

static void test_checkpoint_executes_validated_installed_exe_without_mutation() {
    auto fixture = make_converted("checkpoint");
    const auto generation = generation_dir(fixture);
    const auto manifest_path = generation / "game_manifest.ini";
    const auto before = read_text(manifest_path);

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 4u;
    const auto checkpoint = jojo::bootstrap_runtime_checkpoint(fixture.install, options);
    CHECK(checkpoint);
    if (checkpoint) {
        CHECK(checkpoint.value.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
        CHECK(checkpoint.value.instructions_retired == 4u);
        CHECK(checkpoint.value.last_pc == 0x8001000Cu);
        CHECK(checkpoint.value.presented_frames == 0u);
    }

    const auto boot = jojo::bootstrap_runtime(fixture.install);
    CHECK(!boot);
    if (!boot) {
        CHECK(boot.error == jojo::ErrorCode::backend_unavailable);
        CHECK(boot.detail.find("checkpoint") != std::string::npos);
        CHECK(boot.detail.find("not verified") != std::string::npos);
    }

    CHECK(read_text(manifest_path) == before);
    CHECK(!fs::exists(generation / "cache"));
    cleanup(fixture);
}

static void test_checkpoint_export_is_bounded_and_does_not_mutate_installation() {
    auto fixture = make_converted("checkpoint-export");
    const auto generation = generation_dir(fixture);
    const auto manifest_path = generation / "game_manifest.ini";
    const auto manifest_before = read_text(manifest_path);
    const auto files_before = regular_files_under(generation);
    const auto report_path = fixture.root / "diagnostics" / "m3a-checkpoint.txt";

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 4u;
    const auto checkpoint = jojo::bootstrap_runtime_checkpoint_to_file(
        fixture.install, report_path, options);
    CHECK(checkpoint);
    if (checkpoint) {
        CHECK(checkpoint.value.instructions_retired == 4u);
        CHECK(checkpoint.value.presented_frames == 0u);
    }

    CHECK(fs::is_regular_file(report_path));
    const auto report_text = read_text(report_path);
    CHECK(report_text.find("format=jojo-m3a-checkpoint-v1\n") == 0u);
    CHECK(report_text.find("instructions_retired=4\n") != std::string::npos);
    CHECK(report_text.find("PS-X EXE") == std::string::npos);
    CHECK(read_text(manifest_path) == manifest_before);
    CHECK(regular_files_under(generation) == files_before);
    CHECK(!fs::exists(generation / "diagnostics"));

    cleanup(fixture);
}

int main() {
    test_absent_installation_is_classified();
    test_valid_m1_install_survives_source_deletion();
    test_legacy_v1_is_classified_but_not_validated_as_ps1();
    test_wrong_platform_is_rejected();
    test_missing_local_executable_is_rejected();
    test_modified_local_executable_hash_is_rejected();
    test_active_pointer_to_missing_generation_is_rejected();
    test_manifest_metadata_mismatch_is_rejected();
    test_checkpoint_executes_validated_installed_exe_without_mutation();
    test_checkpoint_export_is_bounded_and_does_not_mutate_installation();
    if (failures) {
        std::cerr << failures << " PS1 runtime-installation assertion(s) failed\n";
        return 1;
    }
    std::cout << "all PS1 runtime-installation assertions passed\n";
    return 0;
}
