#include "core/version.h"
#include "core/settings.h"
#include "core/input.h"
#include "core/disc_image.h"
#include "core/device_id.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
static int failures = 0;

#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_file(std::string_view name) {
    auto p = fs::temp_directory_path() / (std::string("jojo_recompiled_") + std::string(name));
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove(p.string() + ".tmp", ec);
    return p;
}

static void test_version() {
    CHECK(!std::string_view(jojo::core_version()).empty());
}

static void test_graphics_defaults_are_valid() {
    const jojo::GraphicsSettings g{};
    CHECK(jojo::validate_graphics(g));
    CHECK(g.width >= 640);
    CHECK(g.height >= 480);
}

static void test_graphics_rejects_invalid_options() {
    jojo::GraphicsSettings g{};
    g.width = 9000;
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.msaa = static_cast<jojo::Msaa>(16);
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.texture_filter = static_cast<jojo::TextureFilter>(32);
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.aspect_ratio = static_cast<jojo::AspectRatio>(99);
    CHECK(!jojo::validate_graphics(g));
}

static void test_settings_round_trip() {
    const auto path = temp_file("settings.ini");
    jojo::AppSettings in{};
    in.install_root = "C:/Games/JOJO-Recompiled";
    in.graphics.width = 7680;
    in.graphics.height = 4320;
    in.graphics.aspect_ratio = jojo::AspectRatio::ratio_32_9;
    in.graphics.texture_filter = jojo::TextureFilter::x16;
    in.graphics.msaa = jojo::Msaa::x4;
    in.graphics.display_mode = jojo::DisplayMode::fullscreen;
    in.graphics.vsync = false;

    const auto saved = jojo::save_settings_atomic(path, in);
    CHECK(saved);
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.install_root == in.install_root);
        CHECK(loaded.value.graphics == in.graphics);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_legacy_install_dir_migrates_to_install_root() {
    const auto path = temp_file("legacy_install_dir.ini");
    {
        std::ofstream out(path, std::ios::trunc);
        out << "install_dir=D:/Old/JoJo\n";
    }

    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.install_root == "D:/Old/JoJo");
        CHECK(jojo::save_settings_atomic(path, loaded.value));
    }

    std::ifstream in(path);
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    CHECK(text.find("install_root=D:/Old/JoJo") != std::string::npos);
    CHECK(text.find("install_dir=") == std::string::npos);

    std::error_code ec;
    fs::remove(path, ec);
}

static void test_graphics_extended_options_round_trip() {
    const auto path = temp_file("graphics_extended.ini");
    jojo::AppSettings in{};
    in.graphics.width = 3840;
    in.graphics.height = 2160;
    in.graphics.msaa = jojo::Msaa::x8;
    in.graphics.display_mode = jojo::DisplayMode::borderless;
    in.graphics.ui_scale = jojo::UiScale::percent_80;
    in.graphics.hud_safe_area = jojo::HudSafeArea::safe_16_9;
    CHECK(jojo::validate_graphics(in.graphics));
    CHECK(jojo::save_settings_atomic(path, in));
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.graphics.msaa == jojo::Msaa::x8);
        CHECK(loaded.value.graphics.display_mode == jojo::DisplayMode::borderless);
        CHECK(loaded.value.graphics.ui_scale == jojo::UiScale::percent_80);
        CHECK(loaded.value.graphics.hud_safe_area == jojo::HudSafeArea::safe_16_9);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_graphics_rejects_unknown_extended_options() {
    jojo::GraphicsSettings g{};
    g.display_mode = static_cast<jojo::DisplayMode>(99);
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.ui_scale = static_cast<jojo::UiScale>(999);
    CHECK(!jojo::validate_graphics(g));
    g = {};
    g.hud_safe_area = static_cast<jojo::HudSafeArea>(99);
    CHECK(!jojo::validate_graphics(g));
}

static void test_input_bindings_round_trip() {
    const auto path = temp_file("input_settings.ini");
    jojo::AppSettings in{};
    in.input.selected_device = "xinput:0";
    in.input.bindings[jojo::GameAction::up] = {"xinput:0", jojo::BindingKind::gamepad_button, "DPAD_UP"};
    in.input.bindings[jojo::GameAction::attack_light] = {"xinput:0", jojo::BindingKind::gamepad_button, "A"};
    in.input.bindings[jojo::GameAction::pause] = {"keyboard:default", jojo::BindingKind::keyboard_key, "Escape"};
    CHECK(jojo::save_settings_atomic(path, in));
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.input.selected_device == "xinput:0");
        CHECK(loaded.value.input.bindings.at(jojo::GameAction::attack_light).code == "A");
        CHECK(loaded.value.input.bindings.at(jojo::GameAction::pause).kind == jojo::BindingKind::keyboard_key);
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_disc_extension_detection() {
    CHECK(jojo::supported_disc_extension("game.ISO"));
    CHECK(jojo::supported_disc_extension("game.bin"));
    CHECK(jojo::supported_disc_extension("game.cue"));
    CHECK(!jojo::supported_disc_extension("game.gdi"));
    CHECK(!jojo::supported_disc_extension("game.zip"));
}

static void test_disc_fingerprint_is_deterministic() {
    const auto path = temp_file("disc.iso");
    {
        std::ofstream out(path, std::ios::binary);
        out << "JOJO-RECOMPILED-SYNTHETIC-DISC";
    }
    const auto a = jojo::fingerprint_disc_image(path);
    const auto b = jojo::fingerprint_disc_image(path);
    CHECK(a);
    CHECK(b);
    if (a && b) {
        CHECK(a.value.size_bytes == 30);
        CHECK(a.value.fnv1a64 == b.value.fnv1a64);
        CHECK(a.value.hash_hex == b.value.hash_hex);
        CHECK(a.value.format == "iso");
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_device_id_helpers_are_stable() {
    CHECK(jojo::make_xinput_device_id(0) == "xinput:0");
    CHECK(jojo::make_xinput_device_id(3) == "xinput:3");
    const auto a = jojo::make_hid_device_id(R"(\\?\hid#vid_1234&pid_abcd#one)");
    const auto b = jojo::make_hid_device_id(R"(\\?\HID#VID_1234&PID_ABCD#ONE)");
    CHECK(a == b);
    CHECK(a.rfind("hid:", 0) == 0);
}

int main() {
    test_version();
    test_graphics_defaults_are_valid();
    test_graphics_rejects_invalid_options();
    test_settings_round_trip();
    test_legacy_install_dir_migrates_to_install_root();
    test_graphics_extended_options_round_trip();
    test_graphics_rejects_unknown_extended_options();
    test_input_bindings_round_trip();
    test_disc_extension_detection();
    test_disc_fingerprint_is_deterministic();
    test_device_id_helpers_are_stable();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "all assertions passed\n";
    return 0;
}
