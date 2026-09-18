#pragma once

#ifdef _WIN32
#define NOMINMAX
#include "core/input.h"
#include "core/settings.h"
#include "core/settings_menu.h"
#include "app_win32/online_ui.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>
#include <windows.h>
#include <gdiplus.h>

namespace jojo::win32 {

enum class LauncherUiAction {
    none,
    start_game,
    select_disc,
    exit_app,
    settings_changed,
    begin_binding_capture,
    online_refresh_rooms,
    online_host_room,
    online_connect_room,
    online_connect_direct,
    online_begin_matchmaking,
    online_cancel_matchmaking,
    online_leave_lobby,
    online_ready_changed,
    online_send_chat,
    online_start_lobby_game,
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

    void char_input(
        wchar_t ch,
        AppSettings& settings);

    void open_controls() noexcept;
    void open_settings() noexcept;
    void show_main() noexcept;

    [[nodiscard]] bool settings_open() const noexcept;
    [[nodiscard]] bool online_open() const noexcept;
    [[nodiscard]] std::size_t selected_control_player() const noexcept;
    [[nodiscard]] GameAction selected_control_action() const noexcept;
    [[nodiscard]] OnlineLobbyModel& online_model() noexcept { return online_model_; }
    [[nodiscard]] const OnlineLobbyModel& online_model() const noexcept { return online_model_; }
    [[nodiscard]] std::string take_online_chat_message() {
        return online_ui_.take_chat_message();
    }

private:
    enum class Screen {
        main_menu,
        settings,
        online,
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
    OnlineUi online_ui_{};
    OnlineLobbyModel online_model_{};
    ULONG_PTR gdiplus_token_{0};
    std::unique_ptr<Gdiplus::Image> background_{};
};

} // namespace jojo::win32
#endif
