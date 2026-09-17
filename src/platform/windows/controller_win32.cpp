#ifdef _WIN32
#define NOMINMAX
#include "platform/windows/controller_win32.h"
#include "core/device_id.h"
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <xinput.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace jojo::win32 {
namespace {

constexpr USHORT hid_usage_page_generic_desktop = 0x01;
constexpr USHORT hid_usage_joystick = 0x04;
constexpr USHORT hid_usage_gamepad = 0x05;
constexpr USHORT hid_usage_hat_switch = 0x39;

std::wstring lower(std::wstring value) {
    for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}

std::string narrow_utf8(const std::wstring& input) {
    if (input.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                        out.data(), count, nullptr, nullptr);
    return out;
}

HMODULE load_xinput_library(XInputGetStateFn& get_state) {
    constexpr const wchar_t* dlls[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const auto* dll : dlls) {
        HMODULE module = LoadLibraryW(dll);
        if (!module) continue;
        get_state = reinterpret_cast<XInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
        if (get_state) return module;
        FreeLibrary(module);
    }
    get_state = nullptr;
    return nullptr;
}

std::wstring raw_device_name(HANDLE device) {
    UINT chars = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &chars) == static_cast<UINT>(-1) || chars == 0) {
        return {};
    }
    std::wstring path(static_cast<std::size_t>(chars), L'\0');
    UINT capacity = chars;
    const UINT copied = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, path.data(), &capacity);
    if (copied == static_cast<UINT>(-1)) return {};
    path.resize(static_cast<std::size_t>(copied));
    while (!path.empty() && path.back() == L'\0') path.pop_back();
    return path;
}

std::string hid_display_name(const std::wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return "Controle HID";
    wchar_t product[256]{};
    const BOOLEAN ok = HidD_GetProductString(handle, product, sizeof(product));
    CloseHandle(handle);
    if (!ok || product[0] == L'\0') return "Controle HID";
    auto name = narrow_utf8(product);
    return name.empty() ? "Controle HID" : name;
}

bool is_controller_usage(const RID_DEVICE_INFO& info) noexcept {
    return info.dwType == RIM_TYPEHID &&
           info.hid.usUsagePage == hid_usage_page_generic_desktop &&
           (info.hid.usUsage == hid_usage_joystick || info.hid.usUsage == hid_usage_gamepad);
}

void add_xinput(std::vector<InputDeviceInfo>& out) {
    XInputGetStateFn get_state = nullptr;
    HMODULE module = load_xinput_library(get_state);
    if (!module || !get_state) return;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state{};
        if (get_state(i, &state) == ERROR_SUCCESS) {
            out.push_back({make_xinput_device_id(i), "Controle XInput " + std::to_string(i + 1), DeviceKind::xinput});
        }
    }
    FreeLibrary(module);
}

void add_hid(std::vector<InputDeviceInfo>& out) {
    UINT count = 0;
    if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1) || count == 0) {
        return;
    }
    std::vector<RAWINPUTDEVICELIST> devices(count);
    const UINT copied = GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (copied == static_cast<UINT>(-1)) return;

    for (UINT index = 0; index < copied; ++index) {
        if (devices[index].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT info_size = sizeof(info);
        if (GetRawInputDeviceInfoW(devices[index].hDevice, RIDI_DEVICEINFO, &info, &info_size) == static_cast<UINT>(-1)) {
            continue;
        }
        if (!is_controller_usage(info)) continue;

        const auto path = raw_device_name(devices[index].hDevice);
        if (path.empty() || lower(path).find(L"ig_") != std::wstring::npos) continue;
        out.push_back({make_hid_device_id(narrow_utf8(path)), hid_display_name(path), DeviceKind::hid});
    }
}

std::string format_hid_code(const char* prefix, USHORT usage_page, USHORT usage) {
    std::ostringstream out;
    out << prefix << '_' << std::uppercase << std::hex << std::setfill('0')
        << std::setw(4) << static_cast<unsigned>(usage_page)
        << '_' << std::setw(4) << static_cast<unsigned>(usage);
    return out.str();
}

float normalize_thumb(SHORT value) noexcept {
    if (value < 0) return static_cast<float>(value) / 32768.0f;
    if (value > 0) return static_cast<float>(value) / 32767.0f;
    return 0.0f;
}

LONG signed_hid_value(ULONG raw, const HIDP_VALUE_CAPS& cap) noexcept {
    if (cap.LogicalMin >= 0) return static_cast<LONG>(raw);
    const ULONG bits = cap.BitSize;
    if (bits == 0 || bits >= 32) return static_cast<LONG>(raw);
    const ULONG mask = (1UL << bits) - 1UL;
    const ULONG sign = 1UL << (bits - 1UL);
    raw &= mask;
    if ((raw & sign) != 0) raw |= ~mask;
    return static_cast<LONG>(raw);
}

float normalize_hid_axis(LONG value, LONG logical_min, LONG logical_max) noexcept {
    if (logical_max <= logical_min) return 0.0f;
    const double span = static_cast<double>(logical_max) - static_cast<double>(logical_min);
    const double unit = (static_cast<double>(value) - static_cast<double>(logical_min)) / span;
    return std::clamp(static_cast<float>(unit * 2.0 - 1.0), -1.0f, 1.0f);
}

void add_hat_buttons(InputDeviceState& state, USHORT usage_page, USHORT usage, LONG value) {
    if (value < 0 || value > 7) return;
    const auto base = format_hid_code("HID_HAT", usage_page, usage);
    if (value == 7 || value == 0 || value == 1) state.pressed.insert(base + "_UP");
    if (value == 1 || value == 2 || value == 3) state.pressed.insert(base + "_RIGHT");
    if (value == 3 || value == 4 || value == 5) state.pressed.insert(base + "_DOWN");
    if (value == 5 || value == 6 || value == 7) state.pressed.insert(base + "_LEFT");
}

void decode_hid_report(InputDeviceState& state,
                       PHIDP_PREPARSED_DATA preparsed,
                       const HIDP_CAPS& caps,
                       PCHAR report,
                       ULONG report_size) {
    ULONG usage_count = HidP_MaxUsageListLength(HidP_Input, 0, preparsed);
    if (usage_count != 0) {
        std::vector<USAGE_AND_PAGE> usages(usage_count);
        ULONG actual = usage_count;
        if (HidP_GetUsagesEx(HidP_Input, 0, usages.data(), &actual,
                             preparsed, report, report_size) == HIDP_STATUS_SUCCESS) {
            for (ULONG i = 0; i < actual; ++i) {
                state.pressed.insert(format_hid_code("HID_BUTTON", usages[i].UsagePage, usages[i].Usage));
            }
        }
    }

    USHORT value_count = caps.NumberInputValueCaps;
    if (value_count == 0) return;
    std::vector<HIDP_VALUE_CAPS> value_caps(value_count);
    if (HidP_GetValueCaps(HidP_Input, value_caps.data(), &value_count, preparsed) != HIDP_STATUS_SUCCESS) return;

    for (USHORT index = 0; index < value_count; ++index) {
        const auto& cap = value_caps[index];
        const USAGE usage_min = cap.IsRange ? cap.Range.UsageMin : cap.NotRange.Usage;
        const USAGE usage_max = cap.IsRange ? cap.Range.UsageMax : cap.NotRange.Usage;
        for (ULONG usage_value = usage_min; usage_value <= usage_max; ++usage_value) {
            ULONG raw_value = 0;
            const auto usage = static_cast<USAGE>(usage_value);
            if (HidP_GetUsageValue(HidP_Input, cap.UsagePage, cap.LinkCollection, usage,
                                   &raw_value, preparsed, report, report_size) != HIDP_STATUS_SUCCESS) {
                if (usage_value == usage_max) break;
                continue;
            }
            const LONG value = signed_hid_value(raw_value, cap);
            if (cap.UsagePage == hid_usage_page_generic_desktop && usage == hid_usage_hat_switch) {
                add_hat_buttons(state, cap.UsagePage, usage, value);
            } else {
                state.axes[format_hid_code("HID_AXIS", cap.UsagePage, usage)] =
                    normalize_hid_axis(value, cap.LogicalMin, cap.LogicalMax);
            }
            if (usage_value == usage_max) break;
        }
    }
}

InputDeviceState sample_keyboard() {
    InputDeviceState state{"keyboard:default", DeviceKind::keyboard, {}, {}};
    for (unsigned int virtual_key = 1; virtual_key < 256; ++virtual_key) {
        if (virtual_key == VK_SHIFT || virtual_key == VK_CONTROL || virtual_key == VK_MENU) continue;
        if ((GetAsyncKeyState(static_cast<int>(virtual_key)) & 0x8000) != 0) {
            state.pressed.insert(keyboard_code_from_virtual_key(virtual_key));
        }
    }
    return state;
}

}

std::vector<InputDeviceInfo> enumerate_input_devices() {
    std::vector<InputDeviceInfo> devices;
    devices.push_back({"keyboard:default", "Teclado", DeviceKind::keyboard});
    add_xinput(devices);
    add_hid(devices);
    std::stable_sort(devices.begin(), devices.end(),
                     [](const InputDeviceInfo& a, const InputDeviceInfo& b) { return a.id < b.id; });
    devices.erase(std::unique(devices.begin(), devices.end(),
                              [](const InputDeviceInfo& a, const InputDeviceInfo& b) { return a.id == b.id; }),
                  devices.end());
    return devices;
}

std::string keyboard_code_from_virtual_key(unsigned int virtual_key) {
    switch (virtual_key) {
        case VK_UP: return "ArrowUp";
        case VK_DOWN: return "ArrowDown";
        case VK_LEFT: return "ArrowLeft";
        case VK_RIGHT: return "ArrowRight";
        case VK_RETURN: return "Enter";
        case VK_ESCAPE: return "Escape";
        case VK_SPACE: return "Space";
        case VK_TAB: return "Tab";
        case VK_BACK: return "Backspace";
        case VK_DELETE: return "Delete";
        case VK_INSERT: return "Insert";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "PageUp";
        case VK_NEXT: return "PageDown";
        case VK_LSHIFT: return "LeftShift";
        case VK_RSHIFT: return "RightShift";
        case VK_LCONTROL: return "LeftControl";
        case VK_RCONTROL: return "RightControl";
        case VK_LMENU: return "LeftAlt";
        case VK_RMENU: return "RightAlt";
        case VK_CAPITAL: return "CapsLock";
        case VK_NUMPAD0: return "Numpad0";
        case VK_NUMPAD1: return "Numpad1";
        case VK_NUMPAD2: return "Numpad2";
        case VK_NUMPAD3: return "Numpad3";
        case VK_NUMPAD4: return "Numpad4";
        case VK_NUMPAD5: return "Numpad5";
        case VK_NUMPAD6: return "Numpad6";
        case VK_NUMPAD7: return "Numpad7";
        case VK_NUMPAD8: return "Numpad8";
        case VK_NUMPAD9: return "Numpad9";
        case VK_MULTIPLY: return "NumpadMultiply";
        case VK_ADD: return "NumpadAdd";
        case VK_SUBTRACT: return "NumpadSubtract";
        case VK_DECIMAL: return "NumpadDecimal";
        case VK_DIVIDE: return "NumpadDivide";
        default: break;
    }
    if ((virtual_key >= '0' && virtual_key <= '9') || (virtual_key >= 'A' && virtual_key <= 'Z')) {
        return std::string(1, static_cast<char>(virtual_key));
    }
    if (virtual_key >= VK_F1 && virtual_key <= VK_F24) {
        return "F" + std::to_string(virtual_key - VK_F1 + 1);
    }
    std::ostringstream out;
    out << "VK_" << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << virtual_key;
    return out.str();
}

InputDeviceState translate_xinput_state(unsigned int index, const XINPUT_STATE& state) {
    InputDeviceState result{make_xinput_device_id(index), DeviceKind::xinput, {}, {}};
    const auto buttons = state.Gamepad.wButtons;
    const auto add = [&](WORD mask, const char* code) {
        if ((buttons & mask) != 0) result.pressed.insert(code);
    };
    add(XINPUT_GAMEPAD_DPAD_UP, "DPAD_UP");
    add(XINPUT_GAMEPAD_DPAD_DOWN, "DPAD_DOWN");
    add(XINPUT_GAMEPAD_DPAD_LEFT, "DPAD_LEFT");
    add(XINPUT_GAMEPAD_DPAD_RIGHT, "DPAD_RIGHT");
    add(XINPUT_GAMEPAD_START, "START");
    add(XINPUT_GAMEPAD_BACK, "BACK");
    add(XINPUT_GAMEPAD_LEFT_THUMB, "LEFT_THUMB");
    add(XINPUT_GAMEPAD_RIGHT_THUMB, "RIGHT_THUMB");
    add(XINPUT_GAMEPAD_LEFT_SHOULDER, "LEFT_SHOULDER");
    add(XINPUT_GAMEPAD_RIGHT_SHOULDER, "RIGHT_SHOULDER");
    add(XINPUT_GAMEPAD_A, "A");
    add(XINPUT_GAMEPAD_B, "B");
    add(XINPUT_GAMEPAD_X, "X");
    add(XINPUT_GAMEPAD_Y, "Y");

    result.axes["LX"] = normalize_thumb(state.Gamepad.sThumbLX);
    result.axes["LY"] = normalize_thumb(state.Gamepad.sThumbLY);
    result.axes["RX"] = normalize_thumb(state.Gamepad.sThumbRX);
    result.axes["RY"] = normalize_thumb(state.Gamepad.sThumbRY);
    result.axes["LT"] = static_cast<float>(state.Gamepad.bLeftTrigger) / 255.0f;
    result.axes["RT"] = static_cast<float>(state.Gamepad.bRightTrigger) / 255.0f;
    return result;
}

Win32InputHost::Win32InputHost() {
    xinput_module_ = load_xinput_library(xinput_get_state_);
    const auto changes = refresh_devices();
    (void)changes;
}

Win32InputHost::~Win32InputHost() {
    if (xinput_module_) FreeLibrary(xinput_module_);
}

std::vector<InputDeviceChange> Win32InputHost::refresh_devices() {
    auto changes = registry_.refresh(enumerate_input_devices());
    for (auto it = hid_states_.begin(); it != hid_states_.end();) {
        if (!registry_.contains(it->first)) it = hid_states_.erase(it);
        else ++it;
    }
    return changes;
}

Result<void> Win32InputHost::register_raw_input(HWND window) {
    if (!window) {
        return Result<void>::failure(ErrorCode::invalid_argument, "Raw Input requires a valid window handle");
    }
    constexpr DWORD flags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    RAWINPUTDEVICE devices[2]{};
    devices[0].usUsagePage = hid_usage_page_generic_desktop;
    devices[0].usUsage = hid_usage_joystick;
    devices[0].dwFlags = flags;
    devices[0].hwndTarget = window;
    devices[1].usUsagePage = hid_usage_page_generic_desktop;
    devices[1].usUsage = hid_usage_gamepad;
    devices[1].dwFlags = flags;
    devices[1].hwndTarget = window;
    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "RegisterRawInputDevices failed with Win32 error " + std::to_string(GetLastError()));
    }
    return Result<void>::success();
}

bool Win32InputHost::handle_raw_input(HRAWINPUT raw_input) {
    if (!raw_input) return false;
    UINT size = 0;
    if (GetRawInputData(raw_input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        size < sizeof(RAWINPUTHEADER)) {
        return false;
    }
    std::vector<std::byte> bytes(size);
    UINT capacity = size;
    if (GetRawInputData(raw_input, RID_INPUT, bytes.data(), &capacity, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return false;
    }
    auto* raw = reinterpret_cast<RAWINPUT*>(bytes.data());
    if (raw->header.dwType != RIM_TYPEHID || raw->data.hid.dwSizeHid == 0 || raw->data.hid.dwCount == 0) return false;

    const auto path = raw_device_name(raw->header.hDevice);
    if (path.empty() || lower(path).find(L"ig_") != std::wstring::npos) return false;
    const auto device_id = make_hid_device_id(narrow_utf8(path));

    UINT preparsed_size = 0;
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, nullptr, &preparsed_size) == static_cast<UINT>(-1) ||
        preparsed_size == 0) {
        return false;
    }
    std::vector<std::byte> preparsed_bytes(preparsed_size);
    UINT preparsed_capacity = preparsed_size;
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA,
                               preparsed_bytes.data(), &preparsed_capacity) == static_cast<UINT>(-1)) {
        return false;
    }
    auto* preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed_bytes.data());
    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS ||
        caps.UsagePage != hid_usage_page_generic_desktop ||
        (caps.Usage != hid_usage_joystick && caps.Usage != hid_usage_gamepad)) {
        return false;
    }

    InputDeviceState state{device_id, DeviceKind::hid, {}, {}};
    for (DWORD report_index = 0; report_index < raw->data.hid.dwCount; ++report_index) {
        auto* report = reinterpret_cast<PCHAR>(raw->data.hid.bRawData +
                                               report_index * raw->data.hid.dwSizeHid);
        decode_hid_report(state, preparsed, caps, report, raw->data.hid.dwSizeHid);
    }
    hid_states_[device_id] = std::move(state);
    if (!registry_.contains(device_id)) {
        const auto changes = refresh_devices();
        (void)changes;
    }
    return true;
}

InputFrame Win32InputHost::snapshot() const {
    InputFrame frame{};
    for (const auto& device : registry_.devices()) {
        switch (device.kind) {
            case DeviceKind::keyboard:
                frame.devices.push_back(sample_keyboard());
                break;
            case DeviceKind::xinput: {
                if (!xinput_get_state_) break;
                for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index) {
                    if (device.id != make_xinput_device_id(index)) continue;
                    XINPUT_STATE state{};
                    if (xinput_get_state_(index, &state) == ERROR_SUCCESS) {
                        frame.devices.push_back(translate_xinput_state(index, state));
                    }
                    break;
                }
                break;
            }
            case DeviceKind::hid: {
                const auto it = hid_states_.find(device.id);
                if (it != hid_states_.end()) frame.devices.push_back(it->second);
                else frame.devices.push_back({device.id, DeviceKind::hid, {}, {}});
                break;
            }
        }
    }
    return frame;
}

}
#endif
