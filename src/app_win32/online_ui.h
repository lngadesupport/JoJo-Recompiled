#pragma once

#ifdef _WIN32
#define NOMINMAX
#include "core/online_lobby.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <windows.h>

namespace jojo::win32 {

enum class OnlineUiAction {
    none,
    back_to_launcher,
    refresh_public_rooms,
    host_room,
    connect_selected_room,
    connect_direct,
    begin_matchmaking,
    cancel_matchmaking,
    leave_lobby,
    ready_changed,
    send_chat_message,
    start_lobby_game,
};

class OnlineUi {
public:
    void show_home() noexcept;
    [[nodiscard]] std::string take_chat_message();

    void paint(
        HDC dc,
        const RECT& client,
        OnlineLobbyModel& model);

    [[nodiscard]] OnlineUiAction mouse_up(
        POINT client_point,
        const RECT& client,
        OnlineLobbyModel& model);

    [[nodiscard]] OnlineUiAction key_down(
        WPARAM key,
        OnlineLobbyModel& model);

    void char_input(
        wchar_t ch,
        OnlineLobbyModel& model);

private:
    enum class TextField {
        none,
        player_name,
        lobby_name,
        password,
        direct_endpoint,
        chat,
    };

    void cycle_region(
        OnlineLobbyModel& model,
        int direction) noexcept;
    void toggle_queue(
        OnlineLobbyModel& model) noexcept;
    void cycle_privacy(
        OnlineLobbyModel& model) noexcept;
    void cycle_max_players(
        OnlineLobbyModel& model,
        int direction) noexcept;

    TextField text_field_{TextField::none};
    std::size_t selected_row_{0};
    std::size_t selected_room_row_{0};
    std::wstring chat_draft_{};
};

} // namespace jojo::win32
#endif
