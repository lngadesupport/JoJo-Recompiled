#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

enum class OnlineLobbyScreen {
    home,
    public_servers,
    create_lobby,
    lobby,
    find_match,
    searching,
    connecting,
};

enum class OnlineMatchQueue {
    casual,
    ranked,
};

enum class OnlineRoomPrivacy {
    public_room,
    private_room,
};

struct OnlineRoomInfo {
    std::string id{};
    std::string name{};
    std::string region{};
    std::uint32_t players{0};
    std::uint32_t max_players{2};
    bool password_required{false};
    bool available{true};

    friend bool operator==(const OnlineRoomInfo&, const OnlineRoomInfo&) = default;
};

struct OnlineCreateRoomDraft {
    std::string name{"JOJO Lobby"};
    std::uint32_t max_players{2};
    OnlineRoomPrivacy privacy{OnlineRoomPrivacy::public_room};
    std::string password{};

    friend bool operator==(const OnlineCreateRoomDraft&, const OnlineCreateRoomDraft&) = default;
};

struct OnlineLobbyModel {
    OnlineLobbyScreen screen{OnlineLobbyScreen::home};
    std::string player_name{"PLAYER"};
    std::string region{"SOUTH AMERICA"};
    OnlineMatchQueue queue{OnlineMatchQueue::casual};
    OnlineCreateRoomDraft create_room{};
    std::vector<OnlineRoomInfo> rooms{};
    std::optional<std::size_t> selected_room{};
    bool ready{false};
    bool local_player_is_host{false};
    std::uint32_t spectator_count{0};
    std::string status{};
};

[[nodiscard]] bool valid_online_player_name(std::string_view value) noexcept;
[[nodiscard]] bool valid_online_room_name(std::string_view value) noexcept;
[[nodiscard]] Result<void> set_online_player_name(
    OnlineLobbyModel& model,
    std::string name);
[[nodiscard]] Result<void> set_online_region(
    OnlineLobbyModel& model,
    std::string region);
void online_open_home(OnlineLobbyModel& model) noexcept;
void online_open_public_servers(OnlineLobbyModel& model) noexcept;
void online_open_create_lobby(OnlineLobbyModel& model) noexcept;
void online_open_find_match(
    OnlineLobbyModel& model,
    OnlineMatchQueue queue) noexcept;
[[nodiscard]] Result<void> online_begin_match_search(
    OnlineLobbyModel& model);
void online_cancel_match_search(OnlineLobbyModel& model) noexcept;
void online_set_rooms(
    OnlineLobbyModel& model,
    std::vector<OnlineRoomInfo> rooms);
[[nodiscard]] Result<void> online_select_room(
    OnlineLobbyModel& model,
    std::size_t index);
[[nodiscard]] Result<void> online_validate_create_room(
    const OnlineCreateRoomDraft& draft);
[[nodiscard]] Result<void> online_enter_host_lobby(
    OnlineLobbyModel& model);
[[nodiscard]] Result<void> online_enter_joined_lobby(
    OnlineLobbyModel& model);
void online_set_connecting(
    OnlineLobbyModel& model,
    std::string status);
void online_set_ready(
    OnlineLobbyModel& model,
    bool ready) noexcept;

} // namespace jojo
