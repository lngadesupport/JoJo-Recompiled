#ifdef _WIN32
#define NOMINMAX
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
#include <string>
#include <thread>
#include <vector>

// Link the shipping entry point rather than re-creating its initialization in a test.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
int failures = 0;
std::atomic_bool application_exited{false};

constexpr int ID_SOURCE_PATH = 1001;
constexpr int ID_SELECT_SOURCE = 1002;
constexpr int ID_PREPARE = 1003;
constexpr int ID_INSTALL_PATH = 1004;
constexpr int ID_SELECT_INSTALL = 1005;
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
    const auto prepare_button = GetDlgItem(window, ID_PREPARE);
    const auto install_box = GetDlgItem(window, ID_INSTALL_PATH);
    const auto install_button = GetDlgItem(window, ID_SELECT_INSTALL);
    const auto checkpoint_button = GetDlgItem(window, ID_RUN_CHECKPOINT);

    check(usable_control(window, source_box), "source-image path field is visible and usable");
    const bool can_select_source = check(usable_control(window, source_button),
                                         "source-image chooser button is visible and usable");
    check(usable_control(window, prepare_button), "PREPARE button is visible and usable");
    check(usable_control(window, install_box), "install-root path field is visible and usable");
    const bool can_select_install = check(usable_control(window, install_button),
                                          "install-root chooser button is visible and usable");
    const bool checkpoint_exists = check(checkpoint_button != nullptr,
                                         "checkpoint button control 1006 exists");
    if (checkpoint_exists) {
        check(GetParent(checkpoint_button) == window && IsWindowVisible(checkpoint_button),
              "checkpoint button is a visible child of the application window");
        RECT bounds{}, client{};
        GetWindowRect(checkpoint_button, &bounds);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
        GetClientRect(window, &client);
        check(bounds.right > bounds.left && bounds.bottom > bounds.top &&
                  bounds.left >= 0 && bounds.top >= 0 &&
                  bounds.right <= client.right && bounds.bottom <= client.bottom,
              "checkpoint button lies inside the application client area");
        check(window_text(checkpoint_button) == L"EXECUTAR CHECKPOINT",
              "checkpoint button text is exactly EXECUTAR CHECKPOINT");
        check(!IsWindowEnabled(checkpoint_button),
              "checkpoint button is disabled while the initial installation is absent");
    }
    const bool can_drop = check((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0,
                               "application accepts files dropped from Explorer");

    const auto directory = fs::temp_directory_path() /
        (L"jojo-image-selection-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(directory);
    const auto bin = directory / L"JoJo teste \u00e7.BIN";
    { std::ofstream file(bin, std::ios::binary); file.put('\0'); }

    if (can_drop && source_box) {
        for (const auto* extension : {L".iso", L".cue", L".BIN"}) {
            const auto image = directory / (std::wstring(L"JoJo teste \u00e7") + extension);
            { std::ofstream file(image, std::ios::binary); file.put('\0'); }
            drop_files(window, {image});
            check(window_text(source_box) == image.wstring(),
                  "dropping a supported PS1 image selects its complete Unicode path");
        }
        const auto selected = window_text(source_box);
        const auto gdi = directory / L"dreamcast.gdi";
        { std::ofstream file(gdi); file.put('\0'); }
        drop_files(window, {gdi});
        check(window_text(source_box) == selected, ".gdi drops do not replace the PS1 source selection");

        const auto archive = directory / L"image.zip";
        { std::ofstream file(archive); file.put('\0'); }
        drop_files(window, {archive});
        check(window_text(source_box) == selected, "unsupported drops preserve the selected image");
        drop_files(window, {directory / L"missing.bin"});
        check(window_text(source_box) == selected, "missing files do not replace the selected image");
        const auto folder = directory / L"directory.bin";
        fs::create_directory(folder);
        drop_files(window, {folder});
        check(window_text(source_box) == selected, "directories do not replace the selected image");
        drop_files(window, {bin, directory / L"JoJo teste \u00e7.iso"});
        check(window_text(source_box) == selected, "multiple dropped files are not silently reduced to one");
    }

    if (can_select_source) {
        const auto before = window_text(source_box);
        PostMessageW(source_button, BM_CLICK, 0, 0);
        if (cancel_native_picker(ui_thread, window)) {
            check(window_text(source_box) == before,
                  "cancelling source picker preserves the selected image");
        }
    }

    if (can_select_install) {
        const auto before = window_text(install_box);
        PostMessageW(install_button, BM_CLICK, 0, 0);
        if (cancel_native_picker(ui_thread, window)) {
            check(window_text(install_box) == before,
                  "cancelling install-root picker preserves the destination");
        }
    }

    std::error_code error;
    fs::remove_all(directory, error);
    PostMessageW(window, WM_CLOSE, 0, 0);
}
}

int main() {
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
        std::cerr << failures << " Win32 image-selection assertion(s) failed\n";
        return 1;
    }
    std::cout << "Win32 image-selection assertions passed\n";
    return 0;
}
#endif
