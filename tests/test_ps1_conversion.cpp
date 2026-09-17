#include "core/conversion.h"
#include "ps1_fixture.h"

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
    auto root = fs::temp_directory_path() / ("jojo-ps1-conversion-" + std::string(suffix));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

static std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

static jojo::ConversionOptions options_for(const test_ps1::Ps1DiscFixture& fixture) {
    jojo::ConversionOptions options{};
    options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture));
    return options;
}

static void assert_successful_install(const jojo::ConversionManifest& m,
                                      const fs::path& install_root,
                                      const test_ps1::Ps1DiscFixture& fixture) {
    const auto generation = install_root / "generations/generation-000001";
    CHECK(fs::is_regular_file(install_root / "active_install.ini"));
    CHECK(fs::is_regular_file(generation / "game_manifest.ini"));
    CHECK(fs::is_regular_file(generation / "data/SYSTEM.CNF"));
    CHECK(fs::is_regular_file(generation / "data/boot.psxexe"));

    CHECK(m.manifest_version == "2");
    CHECK(m.platform == "playstation");
    CHECK(m.revision_id == "synthetic-ps1-jojo");
    CHECK(m.system_cnf_path == "/SYSTEM.CNF");
    CHECK(m.boot_executable == "/SLUS_TEST.00");
    CHECK(m.media_status == "verified");
    CHECK(m.executable_status == "verified");
    CHECK(m.mips_analysis_status == "pending");
    CHECK(m.reference_runtime_status == "pending");
    CHECK(m.native_codegen_status == "pending");
    CHECK(m.hardware_runtime_status == "pending");
    CHECK(m.boot_status == "pending");
    CHECK(m.rendering_status == "pending");
    CHECK(m.audio_status == "pending");
    CHECK(m.input_status == "pending");
    CHECK(m.gameplay_status == "pending");

    const auto loaded = jojo::load_conversion_manifest(generation / "game_manifest.ini");
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.manifest_version == "2");
        CHECK(loaded.value.revision_id == "synthetic-ps1-jojo");
        CHECK(loaded.value.psx_exe_hash_fnv1a64 == m.psx_exe_hash_fnv1a64);
        CHECK(loaded.value.psx_exe_entry == m.psx_exe_entry);
        CHECK(loaded.value.psx_exe_load_address == m.psx_exe_load_address);
        CHECK(loaded.value.psx_exe_initial_gp == m.psx_exe_initial_gp);
        CHECK(loaded.value.psx_exe_text_size == m.psx_exe_text_size);
        CHECK(loaded.value.psx_exe_stack_base == m.psx_exe_stack_base);
        CHECK(loaded.value.psx_exe_stack_size == m.psx_exe_stack_size);
    }

    CHECK(read_bytes(generation / "data/SYSTEM.CNF") == test_ps1::bytes_of(fixture.system_cnf));
    CHECK(read_bytes(generation / "data/boot.psxexe") == fixture.executable);
}

static void test_cooked_iso_converts_to_source_independent_generation() {
    const auto root = workspace("cooked");
    const auto source = root / "game.iso";
    const auto install = root / "install";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_cooked_iso(source, fixture);

    const auto converted = jojo::convert_image(source, install, options_for(fixture));
    CHECK(converted);
    if (converted) {
        assert_successful_install(converted.value, install, fixture);
        std::error_code ec;
        fs::remove(source, ec);
        CHECK(!ec);
        CHECK(!fs::exists(source));
        CHECK(read_bytes(install / "generations/generation-000001/data/boot.psxexe") ==
              fixture.executable);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
}

static void test_mode2_2352_bin_converts_to_source_independent_generation() {
    const auto root = workspace("mode2");
    const auto cooked = root / "source.iso";
    const auto source = root / "game.bin";
    const auto install = root / "install";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_mode2_bin(cooked, source, fixture);
    std::error_code ec;
    fs::remove(cooked, ec);

    const auto converted = jojo::convert_image(source, install, options_for(fixture));
    CHECK(converted);
    if (converted) {
        assert_successful_install(converted.value, install, fixture);
        fs::remove(source, ec);
        CHECK(!ec);
        CHECK(!fs::exists(source));
        CHECK(read_bytes(install / "generations/generation-000001/data/boot.psxexe") ==
              fixture.executable);
    }

    fs::remove_all(root, ec);
}

static void expect_failure_without_activation(std::string_view suffix,
                                              const test_ps1::Ps1DiscFixture& fixture,
                                              const jojo::ConversionOptions& options,
                                              jojo::ErrorCode expected_error) {
    const auto root = workspace(suffix);
    const auto source = root / "game.iso";
    const auto install = root / "install";
    test_ps1::write_cooked_iso(source, fixture);

    const auto converted = jojo::convert_image(source, install, options);
    CHECK(!converted);
    if (!converted) CHECK(converted.error == expected_error);
    CHECK(!fs::exists(install / "active_install.ini"));

    std::error_code ec;
    fs::remove_all(root, ec);
}

static void test_missing_system_cnf_never_activates() {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.include_system_cnf = false;
    expect_failure_without_activation("missing-system", fixture, options_for(fixture),
                                      jojo::ErrorCode::file_not_found);
}

static void test_malformed_boot_never_activates() {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.system_cnf = "BOOT = host0:SLUS_TEST.00\r\n";
    expect_failure_without_activation("bad-boot", fixture, options_for(fixture),
                                      jojo::ErrorCode::unsupported_format);
}

static void test_invalid_psx_exe_never_activates() {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.executable[0] = static_cast<std::uint8_t>('B');
    expect_failure_without_activation("bad-exe", fixture, options_for(fixture),
                                      jojo::ErrorCode::unsupported_format);
}

static void test_unknown_revision_never_activates() {
    const auto fixture = test_ps1::make_disc_fixture();
    auto options = options_for(fixture);
    options.revision_profiles[0].files[0].fnv1a64 ^= 1u;
    expect_failure_without_activation("unknown-revision", fixture, options,
                                      jojo::ErrorCode::unknown_revision);
}

static void test_existing_regular_file_install_root_never_activates() {
    const auto root = workspace("install-file");
    const auto source = root / "game.iso";
    const auto install = root / "install";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_cooked_iso(source, fixture);
    {
        std::ofstream out(install, std::ios::binary | std::ios::trunc);
        out << "not a directory";
    }

    const auto converted = jojo::convert_image(source, install, options_for(fixture));
    CHECK(!converted);
    CHECK(!fs::exists(root / "active_install.ini"));
    CHECK(fs::is_regular_file(install));

    std::error_code ec;
    fs::remove_all(root, ec);
}

int main() {
    test_cooked_iso_converts_to_source_independent_generation();
    test_mode2_2352_bin_converts_to_source_independent_generation();
    test_missing_system_cnf_never_activates();
    test_malformed_boot_never_activates();
    test_invalid_psx_exe_never_activates();
    test_unknown_revision_never_activates();
    test_existing_regular_file_install_root_never_activates();
    if (failures) {
        std::cerr << failures << " PS1 conversion assertion(s) failed\n";
        return 1;
    }
    std::cout << "all PS1 conversion assertions passed\n";
    return 0;
}
