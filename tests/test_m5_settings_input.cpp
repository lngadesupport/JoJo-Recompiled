#include "core/input.h"
#include "core/settings.h"
#include "core/settings_menu.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_file(const char* name) {
    const auto path = fs::temp_directory_path() / (std::string("jojo_m5_") + name);
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(path.string() + ".tmp", ec);
    return path;
}

static void test_audio_settings_validate_and_round_trip() {
    jojo::AudioSettings audio{};
    CHECK(jojo::validate_audio(audio));
    audio.master_volume = 101;
    CHECK(!jojo::validate_audio(audio));

    const auto path = temp_file("audio.ini");
    jojo::AppSettings settings{};
    settings.audio.master_volume = 73;
    settings.audio.music_volume = 61;
    settings.audio.effects_volume = 88;
    settings.audio.mute_when_unfocused = true;
    CHECK(jojo::save_settings_atomic(path, settings));

    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) CHECK(loaded.value.audio == settings.audio);

    std::error_code ec;
    fs::remove(path, ec);
}

static void test_two_player_bindings_round_trip() {
    const auto path = temp_file("players.ini");
    jojo::AppSettings settings{};
    settings.input.players[0].selected_device = "xinput:0";
    settings.input.players[0].bindings[jojo::GameAction::attack_light] = {
        "xinput:0", jojo::BindingKind::gamepad_button, "A"};
    settings.input.players[1].selected_device = "hid:test-pad";
    settings.input.players[1].bindings[jojo::GameAction::attack_heavy] = {
        "hid:test-pad", jojo::BindingKind::gamepad_button, "HID_BUTTON_0009_0001"};

    CHECK(jojo::save_settings_atomic(path, settings));
    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.input.players[0].selected_device == "xinput:0");
        CHECK(loaded.value.input.players[0].bindings.at(jojo::GameAction::attack_light).code == "A");
        CHECK(loaded.value.input.players[1].selected_device == "hid:test-pad");
        CHECK(loaded.value.input.players[1].bindings.at(jojo::GameAction::attack_heavy).code == "HID_BUTTON_0009_0001");
    }

    std::error_code ec;
    fs::remove(path, ec);
}

static void test_legacy_player_one_keys_still_load() {
    const auto path = temp_file("legacy.ini");
    {
        std::ofstream out(path, std::ios::trunc);
        out << "selected_device=xinput:1\n";
        out << "bind.attack_light=xinput:1|button|B\n";
    }

    const auto loaded = jojo::load_settings(path);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.input.players[0].selected_device == "xinput:1");
        CHECK(loaded.value.input.players[0].bindings.at(jojo::GameAction::attack_light).code == "B");
    }

    std::error_code ec;
    fs::remove(path, ec);
}

static void test_device_registry_reports_hotplug_changes() {
    jojo::InputDeviceRegistry registry;
    auto changes = registry.refresh({
        {"xinput:0", "Controller", jojo::DeviceKind::xinput},
        {"keyboard:default", "Keyboard", jojo::DeviceKind::keyboard},
        {"xinput:0", "Duplicate", jojo::DeviceKind::xinput},
    });
    CHECK(changes.size() == 2);
    CHECK(registry.devices().size() == 2);
    CHECK(registry.devices().front().id == "keyboard:default");
    CHECK(registry.contains("xinput:0"));

    changes = registry.refresh({
        {"keyboard:default", "Keyboard", jojo::DeviceKind::keyboard},
        {"hid:pad-2", "Generic HID", jojo::DeviceKind::hid},
    });
    CHECK(changes.size() == 2);
    bool saw_disconnect = false;
    bool saw_connect = false;
    for (const auto& change : changes) {
        saw_disconnect = saw_disconnect || (change.kind == jojo::DeviceChangeKind::disconnected && change.device.id == "xinput:0");
        saw_connect = saw_connect || (change.kind == jojo::DeviceChangeKind::connected && change.device.id == "hid:pad-2");
    }
    CHECK(saw_disconnect);
    CHECK(saw_connect);
}

static void test_per_player_resolution_handles_buttons_and_axes() {
    jojo::InputSettings settings{};
    settings.players[0].bindings[jojo::GameAction::attack_light] = {
        "xinput:0", jojo::BindingKind::gamepad_button, "A"};
    settings.players[0].bindings[jojo::GameAction::right] = {
        "xinput:0", jojo::BindingKind::gamepad_axis, "LX+"};
    settings.players[1].bindings[jojo::GameAction::attack_heavy] = {
        "hid:pad-2", jojo::BindingKind::gamepad_button, "HID_BUTTON_0009_0001"};

    jojo::InputFrame frame{};
    frame.devices.push_back({
        "xinput:0", jojo::DeviceKind::xinput, {"A"}, {{"LX", 0.80f}}});
    frame.devices.push_back({
        "hid:pad-2", jojo::DeviceKind::hid, {"HID_BUTTON_0009_0001"}, {}});

    const auto resolved = jojo::resolve_player_actions(settings, frame);
    CHECK(resolved[0].pressed(jojo::GameAction::attack_light));
    CHECK(resolved[0].pressed(jojo::GameAction::right));
    CHECK(!resolved[0].pressed(jojo::GameAction::attack_heavy));
    CHECK(resolved[1].pressed(jojo::GameAction::attack_heavy));
}

static void test_binding_capture_detects_new_button_and_axis_direction() {
    jojo::InputFrame before{};
    before.devices.push_back({"xinput:0", jojo::DeviceKind::xinput, {}, {{"LX", 0.0f}}});

    jojo::InputFrame button_after{};
    button_after.devices.push_back({"xinput:0", jojo::DeviceKind::xinput, {"A"}, {{"LX", 0.0f}}});
    const auto button = jojo::capture_binding("xinput:0", before, button_after);
    CHECK(button);
    if (button) {
        CHECK(button.value.kind == jojo::BindingKind::gamepad_button);
        CHECK(button.value.code == "A");
    }

    jojo::InputFrame axis_after{};
    axis_after.devices.push_back({"xinput:0", jojo::DeviceKind::xinput, {}, {{"LX", -0.90f}}});
    const auto axis = jojo::capture_binding("xinput:0", before, axis_after);
    CHECK(axis);
    if (axis) {
        CHECK(axis.value.kind == jojo::BindingKind::gamepad_axis);
        CHECK(axis.value.code == "LX-");
    }
}

static void test_settings_menu_uses_draft_commit_and_discard() {
    jojo::InputDeviceRegistry registry;
    registry.refresh({
        {"keyboard:default", "Keyboard", jojo::DeviceKind::keyboard},
        {"xinput:0", "Controller", jojo::DeviceKind::xinput},
    });

    jojo::AppSettings baseline{};
    jojo::SettingsMenuSession menu(baseline);
    CHECK(menu.page() == jojo::SettingsPage::graphics);
    CHECK(!menu.dirty());

    menu.set_page(jojo::SettingsPage::audio);
    auto audio = menu.draft().audio;
    audio.master_volume = 50;
    CHECK(menu.set_audio(audio));
    CHECK(menu.select_player(1));
    CHECK(menu.select_device(1, "xinput:0", registry));
    CHECK(menu.bind_action(1, jojo::GameAction::attack_medium,
                           {"xinput:0", jojo::BindingKind::gamepad_button, "X"}));
    CHECK(menu.dirty());

    menu.discard();
    CHECK(!menu.dirty());
    CHECK(menu.draft().audio.master_volume == baseline.audio.master_volume);
    CHECK(menu.draft().input.players[1].selected_device == baseline.input.players[1].selected_device);

    audio = menu.draft().audio;
    audio.effects_volume = 42;
    CHECK(menu.set_audio(audio));
    const auto committed = menu.commit();
    CHECK(committed.audio.effects_volume == 42);
    CHECK(!menu.dirty());
}

int main() {
    test_audio_settings_validate_and_round_trip();
    test_two_player_bindings_round_trip();
    test_legacy_player_one_keys_still_load();
    test_device_registry_reports_hotplug_changes();
    test_per_player_resolution_handles_buttons_and_axes();
    test_binding_capture_detects_new_button_and_axis_direction();
    test_settings_menu_uses_draft_commit_and_discard();

    if (failures) {
        std::cerr << failures << " M5 assertion(s) failed\n";
        return 1;
    }
    std::cout << "M5 portable settings/input assertions passed\n";
    return 0;
}
