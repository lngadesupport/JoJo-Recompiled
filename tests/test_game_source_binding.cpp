#include "core/game_source_binding.h"
#include "core/ps1_disc_session.h"
#include "core/settings.h"
#include "ps1_fixture.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path workspace(std::string_view suffix) {
    auto root = fs::temp_directory_path() / ("jojo-source-binding-" + std::string(suffix));
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

static void touch(const fs::path& path, std::string_view bytes = "x") {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static void test_missing_or_empty_rom_directory_returns_no_source() {
    const auto root = workspace("empty");
    const auto missing = jojo::discover_single_ps1_source(root / "Data/ROM");
    CHECK(missing);
    if (missing) CHECK(!missing.value.has_value());
    fs::create_directories(root / "Data/ROM");
    const auto empty = jojo::discover_single_ps1_source(root / "Data/ROM");
    CHECK(empty);
    if (empty) CHECK(!empty.value.has_value());
    std::error_code ec; fs::remove_all(root, ec);
}

static void test_single_supported_source_is_discovered() {
    const auto root = workspace("single");
    const auto rom = root / "Data/ROM/jojo.ISO";
    touch(rom);
    const auto discovered = jojo::discover_single_ps1_source(root / "Data/ROM");
    CHECK(discovered);
    if (discovered) {
        CHECK(discovered.value.has_value());
        if (discovered.value) CHECK(*discovered.value == fs::absolute(rom).lexically_normal());
    }
    std::error_code ec; fs::remove_all(root, ec);
}

static void test_gdi_is_not_a_discovery_candidate() {
    const auto root = workspace("gdi");
    touch(root / "Data/ROM/game.gdi");
    const auto discovered = jojo::discover_single_ps1_source(root / "Data/ROM");
    CHECK(discovered);
    if (discovered) CHECK(!discovered.value.has_value());
    std::error_code ec; fs::remove_all(root, ec);
}

static void test_multiple_independent_sources_require_user_selection() {
    const auto root = workspace("multiple");
    touch(root / "Data/ROM/a.iso");
    touch(root / "Data/ROM/b.bin");
    const auto discovered = jojo::discover_single_ps1_source(root / "Data/ROM");
    CHECK(!discovered);
    if (!discovered) {
        CHECK(discovered.error == jojo::ErrorCode::invalid_argument);
        CHECK(discovered.detail.find("multiple") != std::string::npos);
        CHECK(discovered.detail.find("select") != std::string::npos);
    }
    std::error_code ec; fs::remove_all(root, ec);
}

static void test_single_cue_owns_companion_bins_for_discovery() {
    const auto root = workspace("cue-pair");
    const auto cue = root / "Data/ROM/JoJo.cue";
    touch(root / "Data/ROM/JoJo.bin", "track bytes");
    touch(cue, "FILE \"JoJo.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
    const auto discovered = jojo::discover_single_ps1_source(root / "Data/ROM");
    CHECK(discovered);
    if (discovered) {
        CHECK(discovered.value.has_value());
        if (discovered.value) CHECK(*discovered.value == fs::absolute(cue).lexically_normal());
    }
    std::error_code ec; fs::remove_all(root, ec);
}

static void test_binding_round_trip_persists_only_identity() {
    const auto root = workspace("roundtrip");
    jojo::GameSourceBinding binding{};
    binding.source_path = fs::absolute(root / "outside/JoJo.cue").lexically_normal();
    binding.source_format = "cue";
    binding.source_size = 666806112ull;
    binding.source_hash_fnv1a64 = "b8b5dbf79cdb9fcf";
    binding.revision_id = "jojo-usa-observed-b8b5dbf79cdb9fcf";
    const auto path = root / "Data/Config/game-source.ini";
    CHECK(jojo::save_game_source_binding_atomic(path, binding));
    const auto loaded = jojo::load_game_source_binding(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.source_path == binding.source_path);
        CHECK(loaded.value.source_format == binding.source_format);
        CHECK(loaded.value.source_size == binding.source_size);
        CHECK(loaded.value.source_hash_fnv1a64 == binding.source_hash_fnv1a64);
        CHECK(loaded.value.revision_id == binding.revision_id);
    }
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("PS-X EXE") == std::string::npos);
    CHECK(text.find("boot.psxexe") == std::string::npos);
    std::error_code ec; fs::remove_all(root, ec);
}

static void test_changed_bound_source_is_rejected() {
    const auto root = workspace("changed");
    const auto source = root / "JoJo.iso";
    const auto fixture = test_ps1::make_disc_fixture();
    test_ps1::write_cooked_iso(source, fixture);
    const auto options = options_for(fixture);
    const auto opened = jojo::Ps1DiscSession::open(source, options);
    CHECK(opened);
    if (!opened) { std::error_code ec; fs::remove_all(root, ec); return; }
    const auto binding_path = root / "Data/Config/game-source.ini";
    CHECK(jojo::save_game_source_binding_atomic(binding_path, opened.value.binding()));
    const auto loaded = jojo::load_game_source_binding(binding_path);
    CHECK(loaded);
    if (!loaded) { std::error_code ec; fs::remove_all(root, ec); return; }
    {
        std::ofstream out(source, std::ios::binary | std::ios::app);
        const char padding = '\0';
        out.write(&padding, 1);
    }
    const auto reopened = jojo::reopen_bound_source(loaded.value, options);
    CHECK(!reopened);
    if (!reopened) {
        CHECK(reopened.error == jojo::ErrorCode::invalid_installation);
        CHECK(reopened.detail.find("source changed") != std::string::npos);
    }
    std::error_code ec; fs::remove_all(root, ec);
}

static void test_settings_round_trip_persists_binding_path() {
    const auto root = workspace("settings");
    jojo::AppSettings settings{};
    settings.source_binding_path = fs::absolute(root / "Data/Config/game-source.ini").generic_string();
    const auto settings_path = root / "settings.ini";
    CHECK(jojo::save_settings_atomic(settings_path, settings));
    const auto loaded = jojo::load_settings(settings_path);
    CHECK(loaded);
    if (loaded) CHECK(loaded.value.source_binding_path == settings.source_binding_path);
    std::error_code ec; fs::remove_all(root, ec);
}

int main() {
    test_missing_or_empty_rom_directory_returns_no_source();
    test_single_supported_source_is_discovered();
    test_gdi_is_not_a_discovery_candidate();
    test_multiple_independent_sources_require_user_selection();
    test_single_cue_owns_companion_bins_for_discovery();
    test_binding_round_trip_persists_only_identity();
    test_changed_bound_source_is_rejected();
    test_settings_round_trip_persists_binding_path();
    if (failures) {
        std::cerr << failures << " game-source binding assertion(s) failed\n";
        return 1;
    }
    std::cout << "all game-source binding assertions passed\n";
    return 0;
}
