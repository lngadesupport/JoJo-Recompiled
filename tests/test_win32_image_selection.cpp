#ifdef _WIN32
#define NOMINMAX
#include "core/game_source_binding.h"
#include "core/ps1_disc_session.h"
#include "core/settings.h"
#include "ps1_fixture.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

namespace jojo::win32 {
[[nodiscard]] Result<std::optional<std::filesystem::path>> resolve_startup_source(
    const std::filesystem::path& executable_dir,
    const AppSettings& settings,
    const Ps1DiscOpenOptions& open_options);
}

namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
int failures = 0;
std::atomic_bool application_exited{false};

constexpr int ID_SOURCE_PATH = 1001;
constexpr int ID_SELECT_SOURCE = 1002;
constexpr int ID_VALIDATE_SOURCE = 1003;
constexpr int ID_LEGACY_INSTALL_PATH = 1004;
constexpr int ID_LEGACY_SELECT_INSTALL = 1005;
constexpr int ID_RUN_CHECKPOINT = 1006;

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
    return condition;
}

template<class Predicate>
bool wait_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    do {
        if (predicate()) return true;
        if (application_exited) return false;
        std::this_thread::sleep_for(20ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

struct WindowSearch {
    const wchar_t* class_name;
    HWND owner{};
    HWND result{};
};

BOOL CALLBACK find_window(HWND window, LPARAM param) {
    auto& search = *reinterpret_cast<WindowSearch*>(param);
    wchar_t class_name[128]{};
    GetClassNameW(window, class_name, 128);
    if (IsWindowVisible(window) && std::wcscmp(class_name, search.class_name) == 0 &&
        (!search.owner || GetWindow(window, GW_OWNER) == search.owner)) {
        search.result = window;
        return FALSE;
    }
    return TRUE;
}

HWND thread_window(DWORD thread_id, const wchar_t* class_name, HWND owner = nullptr) {
    WindowSearch search{class_name, owner};
    EnumThreadWindows(thread_id, find_window, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

std::wstring window_text(HWND window) {
    const auto length = static_cast<size_t>(GetWindowTextLengthW(window));
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    text.resize(length);
    return text;
}

bool usable_control(HWND parent, HWND control) {
    if (!control || GetParent(control) != parent || !IsWindowVisible(control) ||
        !IsWindowEnabled(control)) return false;
    RECT bounds{}, client{};
    GetWindowRect(control, &bounds);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&bounds), 2);
    GetClientRect(parent, &client);
    return bounds.right > bounds.left && bounds.bottom > bounds.top &&
        bounds.left >= 0 && bounds.top >= 0 && bounds.right <= client.right &&
        bounds.bottom <= client.bottom;
}

void drop_files(HWND window, const std::vector<fs::path>& paths) {
    std::wstring names;
    for (const auto& path : paths) {
        names += path.wstring();
        names.push_back(L'\0');
    }
    names.push_back(L'\0');
    const auto bytes = names.size() * sizeof(wchar_t);
    auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + bytes);
    if (!check(memory != nullptr, "allocate synthetic file-drop payload")) return;
    auto* data = static_cast<DROPFILES*>(GlobalLock(memory));
    if (!check(data != nullptr, "lock synthetic file-drop payload")) {
        GlobalFree(memory);
        return;
    }
    data->pFiles = sizeof(DROPFILES);
    data->fWide = TRUE;
    std::memcpy(reinterpret_cast<char*>(data) + sizeof(DROPFILES), names.data(), bytes);
    GlobalUnlock(memory);
    SendMessageW(window, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
}

bool cancel_native_picker(DWORD ui_thread, HWND owner) {
    HWND picker = nullptr;
    if (!check(wait_until([&] {
            picker = thread_window(ui_thread, L"#32770", owner);
            return picker != nullptr;
        }), "chooser opens a native #32770 dialog")) {
        return false;
    }
    PostMessageW(picker, WM_CLOSE, 0, 0);
    return check(wait_until([&] { return !IsWindow(picker) && IsWindowEnabled(owner); }),
                 "cancelling chooser returns to the application");
}

jojo::Ps1DiscOpenOptions options_for(const test_ps1::Ps1DiscFixture& fixture) {
    jojo::Ps1DiscOpenOptions options{};
    options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture));
    return options;
}

void test_startup_source_priority() {
    const auto root = fs::temp_directory_path() /
        ("jojo-win32-startup-source-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Data/ROM", ec);

    const auto fixture = test_ps1::make_disc_fixture();
    const auto options = options_for(fixture);
    const auto saved_source = root / "owned/saved.iso";
    fs::create_directories(saved_source.parent_path(), ec);
    test_ps1::write_cooked_iso(saved_source, fixture);
    const auto opened = jojo::Ps1DiscSession::open(saved_source, options);
    check(static_cast<bool>(opened), "synthetic saved source opens before startup-priority test");

    jojo::AppSettings settings{};
    const auto binding_path = root / "config/game-source.ini";
    if (opened) {
        check(static_cast<bool>(jojo::save_game_source_binding_atomic(binding_path, opened.value.binding())),
              "saved source binding is persisted");
        settings.source_binding_path = binding_path.generic_string();
    }

    const auto auto_source = root / "Data/ROM/fallback.iso";
    { std::ofstream out(auto_source, std::ios::binary); out.put('\0'); }

    const auto preferred = jojo::win32::resolve_startup_source(root, settings, options);
    check(static_cast<bool>(preferred), "startup source priority resolves without error");
    if (preferred) {
        check(preferred.value.has_value(), "startup source priority returns a source");
        if (preferred.value) {
            check(*preferred.value == fs::absolute(saved_source).lexically_normal(),
                  "valid saved binding wins over Data/ROM discovery");
        }
    }

    { std::ofstream out(saved_source, std::ios::binary | std::ios::app); out.put('\0'); }
    const auto fallback = jojo::win32::resolve_startup_source(root, settings, options);
    check(static_cast<bool>(fallback), "invalid saved binding falls back to Data/ROM");
    if (fallback && fallback.value) {
        check(*fallback.value == fs::absolute(auto_source).lexically_normal(),
              "Data/ROM source is used after saved binding stops validating");
    }

    fs::remove(auto_source, ec);
    settings.source_binding_path.clear();
    const auto none = jojo::win32::resolve_startup_source(root, settings, options);
    check(static_cast<bool>(none), "empty startup source state is not an error");
    if (none) check(!none.value.has_value(), "empty startup state waits for manual picker");

    fs::remove_all(root, ec);
}

void inspect_application(DWORD ui_thread) {
    HWND window = nullptr;
    if (!check(wait_until([&] {
            window = thread_window(ui_thread, L"JOJORecompiledWindow");
            return window != nullptr;
        }), "shipping entry point creates a visible application window")) {
        PostThreadMessageW(ui_thread, WM_QUIT, 1, 0);
        return;
    }

    const auto source_box = GetDlgItem(window, ID_SOURCE_PATH);
    const auto source_button = GetDlgItem(window, ID_SELECT_SOURCE);
    const auto validate_button = GetDlgItem(window, ID_VALIDATE_SOURCE);
    const auto checkpoint_button = GetDlgItem(window, ID_RUN_CHECKPOINT);

    check(usable_control(window, source_box), "source-image path field is visible and usable");
    const bool can_select_source = check(usable_control(window, source_button),
                                         "source-image chooser button is visible and usable");
    check(usable_control(window, validate_button), "VALIDAR JOGO button is visible and usable");
    check(window_text(validate_button) == L"VALIDAR JOGO",
          "primary source action is VALIDAR JOGO, not PREPARAR JOGO");
    check(GetDlgItem(window, ID_LEGACY_INSTALL_PATH) == nullptr,
          "legacy install-root field no longer exists");
    check(GetDlgItem(window, ID_LEGACY_SELECT_INSTALL) == nullptr,
          "legacy install-root chooser no longer exists");

    const bool checkpoint_exists = check(checkpoint_button != nullptr,
                                         "checkpoint button control 1006 exists");
    if (checkpoint_exists) {
        check(GetParent(checkpoint_button) == window && IsWindowVisible(checkpoint_button),
              "checkpoint button is visible");
        check(window_text(checkpoint_button) == L"EXECUTAR CHECKPOINT",
              "checkpoint button text remains explicit");
        check(!IsWindowEnabled(checkpoint_button),
              "checkpoint is disabled until the selected original image validates");
    }
    const bool can_drop = check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0,
                                "application accepts files dropped from Explorer");

    const auto directory = fs::temp_directory_path() /
        (L"jojo-image-selection-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(directory);

    if (can_drop && source_box) {
        for (const auto* extension : {L".iso", L".cue", L".BIN"}) {
            const auto image = directory / (std::wstring(L"JoJo teste ç") + extension);
            { std::ofstream file(image, std::ios::binary); file.put('\0'); }
            drop_files(window, {image});
            check(window_text(source_box) == image.wstring(),
                  "dropping a supported PS1 image selects its complete Unicode path");
            check(IsWindowEnabled(validate_button),
                  "selected source can be validated without preparing an installation");
        }
        const auto selected = window_text(source_box);
        const auto gdi = directory / L"dreamcast.gdi";
        { std::ofstream file(gdi); file.put('\0'); }
        drop_files(window, {gdi});
        check(window_text(source_box) == selected, ".gdi drops do not replace the PS1 source selection");

        const auto archive = directory / L"image.zip";
        { std::ofstream file(archive); file.put('\0'); }
        drop_files(window, {archive});
        check(window_text(source_box) == selected, "unsupported drops preserve selected source");
        drop_files(window, {directory / L"missing.bin"});
        check(window_text(source_box) == selected, "missing files preserve selected source");
        const auto folder = directory / L"directory.bin";
        fs::create_directory(folder);
        drop_files(window, {folder});
        check(window_text(source_box) == selected, "directories preserve selected source");
    }

    if (can_select_source) {
        const auto before = window_text(source_box);
        PostMessageW(source_button, BM_CLICK, 0, 0);
        if (cancel_native_picker(ui_thread, window)) {
            check(window_text(source_box) == before,
                  "cancelling source picker preserves selected image");
        }
    }

    std::error_code error;
    fs::remove_all(directory, error);
    PostMessageW(window, WM_CLOSE, 0, 0);
}
}

int main() {
    test_startup_source_priority();

    const DWORD ui_thread = GetCurrentThreadId();
    std::thread observer([&] {
        try {
            inspect_application(ui_thread);
        } catch (const std::exception& error) {
            check(false, error.what());
            PostThreadMessageW(ui_thread, WM_QUIT, 1, 0);
        }
    });
    wchar_t arguments[] = L"";
    const int result = wWinMain(GetModuleHandleW(nullptr), nullptr, arguments, SW_SHOWNORMAL);
    application_exited = true;
    observer.join();
    check(result == 0, "application shuts down normally");
    if (failures) {
        std::cerr << failures << " Win32 direct-source assertion(s) failed\n";
        return 1;
    }
    std::cout << "Win32 direct-source assertions passed\n";
    return 0;
}
#endif
