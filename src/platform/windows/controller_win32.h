#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "core/input.h"
#include "core/result.h"
#include <windows.h>
#include <xinput.h>
#include <map>
#include <string>
#include <vector>

namespace jojo::win32 {

using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);

[[nodiscard]] std::vector<InputDeviceInfo> enumerate_input_devices();
[[nodiscard]] std::string keyboard_code_from_virtual_key(unsigned int virtual_key);
[[nodiscard]] InputDeviceState translate_xinput_state(unsigned int index, const XINPUT_STATE& state);

class Win32InputHost {
public:
    Win32InputHost();
    ~Win32InputHost();

    Win32InputHost(const Win32InputHost&) = delete;
    Win32InputHost& operator=(const Win32InputHost&) = delete;
    Win32InputHost(Win32InputHost&&) = delete;
    Win32InputHost& operator=(Win32InputHost&&) = delete;

    [[nodiscard]] std::vector<InputDeviceChange> refresh_devices();
    [[nodiscard]] Result<void> register_raw_input(HWND window);
    [[nodiscard]] bool handle_raw_input(HRAWINPUT raw_input);
    [[nodiscard]] InputFrame snapshot() const;
    [[nodiscard]] const InputDeviceRegistry& registry() const noexcept { return registry_; }

private:
    HMODULE xinput_module_{nullptr};
    XInputGetStateFn xinput_get_state_{nullptr};
    InputDeviceRegistry registry_{};
    std::map<std::string, InputDeviceState> hid_states_{};
};

}
#endif
