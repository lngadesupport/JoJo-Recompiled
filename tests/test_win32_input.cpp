#ifdef _WIN32
#define NOMINMAX
#include "platform/windows/controller_win32.h"
#include <windows.h>
#include <xinput.h>
#include <cmath>
#include <iostream>
#include <string>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void test_keyboard_is_always_enumerated() {
    const auto devices = jojo::win32::enumerate_input_devices();
    bool found = false;
    for (const auto& device : devices) {
        if (device.id == "keyboard:default") {
            found = true;
            CHECK(device.kind == jojo::DeviceKind::keyboard);
            CHECK(!device.name.empty());
        }
    }
    CHECK(found);
}

static void test_keyboard_codes_are_stable() {
    CHECK(jojo::win32::keyboard_code_from_virtual_key(VK_UP) == "ArrowUp");
    CHECK(jojo::win32::keyboard_code_from_virtual_key(VK_RETURN) == "Enter");
    CHECK(jojo::win32::keyboard_code_from_virtual_key(VK_RSHIFT) == "RightShift");
    CHECK(jojo::win32::keyboard_code_from_virtual_key('A') == "A");
    CHECK(jojo::win32::keyboard_code_from_virtual_key('7') == "7");
    CHECK(jojo::win32::keyboard_code_from_virtual_key(VK_F12) == "F12");
}

static void test_xinput_translation_uses_portable_codes() {
    XINPUT_STATE native{};
    native.dwPacketNumber = 5;
    native.Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_LEFT_SHOULDER;
    native.Gamepad.bLeftTrigger = 255;
    native.Gamepad.bRightTrigger = 128;
    native.Gamepad.sThumbLX = 32767;
    native.Gamepad.sThumbLY = -32768;
    native.Gamepad.sThumbRX = 16384;
    native.Gamepad.sThumbRY = 0;

    const auto state = jojo::win32::translate_xinput_state(2, native);
    CHECK(state.device_id == "xinput:2");
    CHECK(state.kind == jojo::DeviceKind::xinput);
    CHECK(state.pressed.contains("A"));
    CHECK(state.pressed.contains("DPAD_UP"));
    CHECK(state.pressed.contains("LEFT_SHOULDER"));
    CHECK(state.axes.at("LX") > 0.99f);
    CHECK(state.axes.at("LY") <= -0.99f);
    CHECK(state.axes.at("RX") > 0.49f && state.axes.at("RX") < 0.51f);
    CHECK(state.axes.at("LT") > 0.99f);
    CHECK(state.axes.at("RT") > 0.49f && state.axes.at("RT") < 0.51f);
}

static void test_host_refresh_and_snapshot_work_without_a_controller() {
    jojo::win32::Win32InputHost host;
    const auto changes = host.refresh_devices();
    (void)changes;
    CHECK(host.registry().contains("keyboard:default"));

    const auto frame = host.snapshot();
    bool found_keyboard = false;
    for (const auto& state : frame.devices) {
        if (state.device_id == "keyboard:default") {
            found_keyboard = true;
            CHECK(state.kind == jojo::DeviceKind::keyboard);
        }
    }
    CHECK(found_keyboard);
    CHECK(!host.handle_raw_input(nullptr));
}

static void test_raw_input_registration_accepts_a_message_window() {
    HWND window = CreateWindowExW(0, L"STATIC", L"JOJO raw input test", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    CHECK(window != nullptr);
    if (window) {
        jojo::win32::Win32InputHost host;
        CHECK(host.register_raw_input(window));
        DestroyWindow(window);
    }
}

int main() {
    test_keyboard_is_always_enumerated();
    test_keyboard_codes_are_stable();
    test_xinput_translation_uses_portable_codes();
    test_host_refresh_and_snapshot_work_without_a_controller();
    test_raw_input_registration_accepts_a_message_window();

    if (failures) {
        std::cerr << failures << " Win32 input assertion(s) failed\n";
        return 1;
    }
    std::cout << "Win32 input assertions passed\n";
    return 0;
}
#endif
