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
    std::string connect_endpoint{};
    std::string game_revision{};

    friend bool operator==(const OnlineRoomInfo&, const OnlineRoomInfo&) = default;
};

struct OnlineCreateRoomDraft {
    std::string name{"JOJO Lobby"};
    std::uint32_t max_players{2};
    OnlineRoomPrivacy privacy{OnlineRoomPrivacy::public_room};
    std::string password{};

    friend bool operator==(const OnlineCreateRoomDraft&, const OnlineCreateRoomDraft&) = default;
};

struct OnlineChatMessage {
    std::string sender{};
    std::string text{};
    friend bool operator==(const OnlineChatMessage&, const OnlineChatMessage&) = default;
};

struct OnlineLobbyModel {
    OnlineLobbyScreen screen{OnlineLobbyScreen::home};
    std::string player_name{"PLAYER"};
    std::string remote_player_name{"OPPONENT"};
    std::string local_game_revision{};
    std::string remote_game_revision{};
    std::string region{"SOUTH AMERICA - ARGENTINA"};
    std::string direct_connect_endpoint{"127.0.0.1:27886"};
    OnlineMatchQueue queue{OnlineMatchQueue::casual};
    OnlineCreateRoomDraft create_room{};
    std::vector<OnlineRoomInfo> rooms{};
    std::optional<std::size_t> selected_room{};
    bool ready{false};
    bool remote_ready{false};
    bool local_player_is_host{false};
    bool start_requested{false};
    std::uint32_t spectator_count{0};
    std::vector<OnlineChatMessage> chat_messages{};
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
void online_enter_direct_lobby(
    OnlineLobbyModel& model) noexcept;
void online_set_connecting(
    OnlineLobbyModel& model,
    std::string status);
void online_set_ready(
    OnlineLobbyModel& model,
    bool ready) noexcept;
[[nodiscard]] Result<void> online_set_remote_player_name(
    OnlineLobbyModel& model,
    std::string name);
void online_set_remote_ready(
    OnlineLobbyModel& model,
    bool ready) noexcept;
[[nodiscard]] Result<void> online_set_local_game_revision(
    OnlineLobbyModel& model,
    std::string revision);
[[nodiscard]] Result<void> online_set_remote_game_revision(
    OnlineLobbyModel& model,
    std::string revision);
[[nodiscard]] bool online_game_revision_matches(
    const OnlineLobbyModel& model) noexcept;
[[nodiscard]] Result<void> online_append_chat(
    OnlineLobbyModel& model,
    std::string sender,
    std::string text);
void online_request_start(
    OnlineLobbyModel& model) noexcept;
void online_reset_peer_state(
    OnlineLobbyModel& model) noexcept;

} // namespace jojo
