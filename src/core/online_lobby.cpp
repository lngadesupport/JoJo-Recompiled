#include "core/online_lobby.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace jojo {
namespace {

bool valid_text(std::string_view value, std::size_t max_length) noexcept {
    if (value.empty() || value.size() > max_length) return false;
    for (const unsigned char ch : value) {
        if (std::iscntrl(ch) != 0) return false;
    }
    return true;
}

} // namespace

bool valid_online_player_name(std::string_view value) noexcept {
    return valid_text(value, 24u);
}

bool valid_online_room_name(std::string_view value) noexcept {
    return valid_text(value, 40u);
}

Result<void> set_online_player_name(
    OnlineLobbyModel& model,
    std::string name) {
    if (!valid_online_player_name(name)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "online player name must contain 1-24 printable characters");
    }
    model.player_name = std::move(name);
    return Result<void>::success();
}

Result<void> set_online_region(
    OnlineLobbyModel& model,
    std::string region) {
    if (!valid_text(region, 48u)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "online region must contain 1-48 printable characters");
    }
    model.region = std::move(region);
    return Result<void>::success();
}

void online_reset_peer_state(OnlineLobbyModel& model) noexcept {
    model.remote_player_name = "OPPONENT";
    model.remote_game_revision.clear();
    model.remote_ready = false;
    model.start_requested = false;
    model.chat_messages.clear();
}

void online_open_home(OnlineLobbyModel& model) noexcept {
    model.screen = OnlineLobbyScreen::home;
    online_reset_peer_state(model);
    model.status.clear();
}

void online_open_public_servers(OnlineLobbyModel& model) noexcept {
    model.screen = OnlineLobbyScreen::public_servers;
    model.selected_room.reset();
    model.status.clear();
}

void online_open_create_lobby(OnlineLobbyModel& model) noexcept {
    model.screen = OnlineLobbyScreen::create_lobby;
    model.status.clear();
}

void online_open_find_match(
    OnlineLobbyModel& model,
    OnlineMatchQueue queue) noexcept {
    model.queue = queue;
    model.screen = OnlineLobbyScreen::find_match;
    model.status.clear();
}

Result<void> online_begin_match_search(
    OnlineLobbyModel& model) {
    if (!valid_online_player_name(model.player_name) ||
        !valid_text(model.region, 48u)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "matchmaking requires a valid player name and region");
    }
    model.screen = OnlineLobbyScreen::searching;
    model.status = model.queue == OnlineMatchQueue::ranked
        ? "SEARCHING RANKED OPPONENT"
        : "SEARCHING CASUAL OPPONENT";
    return Result<void>::success();
}

void online_cancel_match_search(OnlineLobbyModel& model) noexcept {
    model.screen = OnlineLobbyScreen::find_match;
    model.status.clear();
}

void online_set_rooms(
    OnlineLobbyModel& model,
    std::vector<OnlineRoomInfo> rooms) {
    model.rooms = std::move(rooms);
    model.selected_room.reset();
}

Result<void> online_select_room(
    OnlineLobbyModel& model,
    std::size_t index) {
    if (index >= model.rooms.size()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "online room index is outside the available room list");
    }
    if (!model.rooms[index].available ||
        model.rooms[index].players >= model.rooms[index].max_players) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "selected online room is not joinable");
    }
    model.selected_room = index;
    return Result<void>::success();
}

Result<void> online_validate_create_room(
    const OnlineCreateRoomDraft& draft) {
    if (!valid_online_room_name(draft.name)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "online lobby name must contain 1-40 printable characters");
    }
    if (draft.max_players < 2u || draft.max_players > 8u) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "online lobby max players must be between 2 and 8");
    }
    if (draft.privacy == OnlineRoomPrivacy::private_room &&
        (draft.password.empty() || draft.password.size() > 64u)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "private online lobbies require a password up to 64 characters");
    }
    return Result<void>::success();
}

Result<void> online_enter_host_lobby(
    OnlineLobbyModel& model) {
    const auto valid = online_validate_create_room(model.create_room);
    if (!valid) return valid;
    model.screen = OnlineLobbyScreen::lobby;
    model.local_player_is_host = true;
    model.ready = false;
    online_reset_peer_state(model);
    model.status = "WAITING FOR PLAYERS";
    return Result<void>::success();
}

Result<void> online_enter_joined_lobby(
    OnlineLobbyModel& model) {
    if (!model.selected_room || *model.selected_room >= model.rooms.size()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "joining an online lobby requires a selected public room");
    }
    model.screen = OnlineLobbyScreen::lobby;
    model.local_player_is_host = false;
    model.ready = false;
    online_reset_peer_state(model);
    model.status = "CONNECTED TO LOBBY";
    return Result<void>::success();
}

void online_enter_direct_lobby(
    OnlineLobbyModel& model) noexcept {
    model.screen = OnlineLobbyScreen::lobby;
    model.local_player_is_host = false;
    model.ready = false;
    online_reset_peer_state(model);
    model.status = "CONNECTED DIRECTLY TO LOBBY";
}

void online_set_connecting(
    OnlineLobbyModel& model,
    std::string status) {
    model.screen = OnlineLobbyScreen::connecting;
    model.status = std::move(status);
}

void online_set_ready(
    OnlineLobbyModel& model,
    bool ready) noexcept {
    model.ready = ready;
}

Result<void> online_set_remote_player_name(
    OnlineLobbyModel& model,
    std::string name) {
    if (!valid_online_player_name(name)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "remote player name must contain 1-24 printable characters");
    }
    model.remote_player_name = std::move(name);
    return Result<void>::success();
}

void online_set_remote_ready(
    OnlineLobbyModel& model,
    bool ready) noexcept {
    model.remote_ready = ready;
}

Result<void> online_set_local_game_revision(
    OnlineLobbyModel& model,
    std::string revision) {
    if (!revision.empty() && !valid_text(revision, 64u)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "local game revision must contain at most 64 printable characters");
    }
    model.local_game_revision = std::move(revision);
    return Result<void>::success();
}

Result<void> online_set_remote_game_revision(
    OnlineLobbyModel& model,
    std::string revision) {
    if (!revision.empty() && !valid_text(revision, 64u)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "remote game revision must contain at most 64 printable characters");
    }
    model.remote_game_revision = std::move(revision);
    return Result<void>::success();
}

bool online_game_revision_matches(
    const OnlineLobbyModel& model) noexcept {
    return !model.local_game_revision.empty() &&
        !model.remote_game_revision.empty() &&
        model.local_game_revision == model.remote_game_revision;
}

Result<void> online_append_chat(
    OnlineLobbyModel& model,
    std::string sender,
    std::string text) {
    if (!valid_online_player_name(sender) || !valid_text(text, 120u)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "online chat sender/message is invalid");
    }
    model.chat_messages.push_back(
        OnlineChatMessage{std::move(sender), std::move(text)});
    constexpr std::size_t max_messages = 32u;
    if (model.chat_messages.size() > max_messages) {
        model.chat_messages.erase(
            model.chat_messages.begin(),
            model.chat_messages.begin() +
                static_cast<std::ptrdiff_t>(
                    model.chat_messages.size() - max_messages));
    }
    return Result<void>::success();
}

void online_request_start(
    OnlineLobbyModel& model) noexcept {
    model.start_requested = true;
}

} // namespace jojo
