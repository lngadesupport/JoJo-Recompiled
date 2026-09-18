#pragma once

#ifdef _WIN32
#define NOMINMAX
#include "core/input.h"
#include "core/settings.h"
#include "core/settings_menu.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>
#include <windows.h>

namespace Gdiplus {
class Image;
}

namespace jojo::win32 {

enum class LauncherUiAction {
    none,
    start_game,
    select_disc,
    exit_app,
    settings_changed,
    begin_binding_capture,
};

class LauncherUi {
public:
    LauncherUi() = default;
    ~LauncherUi();

    LauncherUi(const LauncherUi&) = delete;
    LauncherUi& operator=(const LauncherUi&) = delete;

    [[nodiscard]] bool initialize(const std::filesystem::path& background_path);
    void shutdown() noexcept;

    void paint(
        HDC dc,
        const RECT& client,
        const AppSettings& settings,
        const InputDeviceRegistry& devices,
        std::wstring_view source_label,
        std::wstring_view capture_status);

    [[nodiscard]] LauncherUiAction mouse_up(
        POINT client_point,
        const RECT& client,
        AppSettings& settings,
        const InputDeviceRegistry& devices);

    [[nodiscard]] LauncherUiAction key_down(
        WPARAM key,
        AppSettings& settings,
        const InputDeviceRegistry& devices);

    void open_controls() noexcept;
    void open_settings() noexcept;
    void show_main() noexcept;

    [[nodiscard]] bool settings_open() const noexcept;
    [[nodiscard]] std::size_t selected_control_player() const noexcept;
    [[nodiscard]] GameAction selected_control_action() const noexcept;

private:
    enum class Screen {
        main_menu,
        settings,
    };

    [[nodiscard]] LauncherUiAction activate_main_item() noexcept;
    [[nodiscard]] LauncherUiAction adjust_setting(
        int direction,
        AppSettings& settings,
        const InputDeviceRegistry& devices);

    [[nodiscard]] std::size_t row_count() const noexcept;
    void clamp_selected_row() noexcept;
    void cycle_page(int direction) noexcept;
    void cycle_control_player(int direction) noexcept;

    Screen screen_{Screen::main_menu};
    SettingsPage page_{SettingsPage::graphics};
    std::size_t main_selection_{0};
    std::size_t selected_row_{0};
    std::size_t control_player_{0};
    ULONG_PTR gdiplus_token_{0};
    std::unique_ptr<Gdiplus::Image> background_{};
};

} // namespace jojo::win32
#endif
