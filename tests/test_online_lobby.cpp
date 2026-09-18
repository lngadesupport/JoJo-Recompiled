#include "core/online_lobby.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

void test_home_and_matchmaking_flow() {
    jojo::OnlineLobbyModel model{};
    CHECK(model.screen == jojo::OnlineLobbyScreen::home);
    CHECK(jojo::set_online_player_name(model, "PLAYER"));
    CHECK(jojo::set_online_region(model, "SOUTH AMERICA"));

    jojo::online_open_find_match(model, jojo::OnlineMatchQueue::ranked);
    CHECK(model.screen == jojo::OnlineLobbyScreen::find_match);
    CHECK(model.queue == jojo::OnlineMatchQueue::ranked);

    CHECK(jojo::online_begin_match_search(model));
    CHECK(model.screen == jojo::OnlineLobbyScreen::searching);
    CHECK(model.status.find("RANKED") != std::string::npos);

    jojo::online_cancel_match_search(model);
    CHECK(model.screen == jojo::OnlineLobbyScreen::find_match);
}

void test_public_room_selection_and_join() {
    jojo::OnlineLobbyModel model{};
    jojo::online_open_public_servers(model);
    jojo::online_set_rooms(model, {
        {"room-a", "First Room", "SOUTH AMERICA", 1u, 2u, false, true},
        {"room-b", "Full Room", "SOUTH AMERICA", 2u, 2u, false, true},
    });

    CHECK(jojo::online_select_room(model, 0u));
    CHECK(model.selected_room && *model.selected_room == 0u);
    CHECK(jojo::online_enter_joined_lobby(model));
    CHECK(model.screen == jojo::OnlineLobbyScreen::lobby);
    CHECK(!model.local_player_is_host);

    jojo::online_open_public_servers(model);
    CHECK(!jojo::online_select_room(model, 1u));
}

void test_create_lobby_validation_and_host_flow() {
    jojo::OnlineLobbyModel model{};
    jojo::online_open_create_lobby(model);

    model.create_room.name = "Competitive Lobby";
    model.create_room.max_players = 2u;
    model.create_room.privacy = jojo::OnlineRoomPrivacy::private_room;
    model.create_room.password = "secret";
    CHECK(jojo::online_validate_create_room(model.create_room));
    CHECK(jojo::online_enter_host_lobby(model));
    CHECK(model.screen == jojo::OnlineLobbyScreen::lobby);
    CHECK(model.local_player_is_host);
    CHECK(!model.ready);

    jojo::online_set_ready(model, true);
    CHECK(model.ready);
}

void test_peer_state_chat_and_start_are_synchronized_in_model() {
    jojo::OnlineLobbyModel model{};
    CHECK(jojo::online_set_remote_player_name(model, "RIVAL"));
    CHECK(model.remote_player_name == "RIVAL");

    jojo::online_set_remote_ready(model, true);
    CHECK(model.remote_ready);

    CHECK(jojo::online_append_chat(model, "RIVAL", "READY?"));
    CHECK(model.chat_messages.size() == 1u);
    if (!model.chat_messages.empty()) {
        CHECK(model.chat_messages.front().sender == "RIVAL");
        CHECK(model.chat_messages.front().text == "READY?");
    }

    jojo::online_request_start(model);
    CHECK(model.start_requested);

    jojo::online_reset_peer_state(model);
    CHECK(model.remote_player_name == "OPPONENT");
    CHECK(!model.remote_ready);
    CHECK(!model.start_requested);
    CHECK(model.chat_messages.empty());
}

void test_invalid_fields_are_rejected() {
    jojo::OnlineLobbyModel model{};
    CHECK(!jojo::set_online_player_name(model, ""));
    CHECK(!jojo::set_online_region(model, ""));

    jojo::OnlineCreateRoomDraft draft{};
    draft.max_players = 1u;
    CHECK(!jojo::online_validate_create_room(draft));

    draft.max_players = 2u;
    draft.privacy = jojo::OnlineRoomPrivacy::private_room;
    draft.password.clear();
    CHECK(!jojo::online_validate_create_room(draft));
}
}

int main() {
    test_home_and_matchmaking_flow();
    test_public_room_selection_and_join();
    test_create_lobby_validation_and_host_flow();
    test_peer_state_chat_and_start_are_synchronized_in_model();
    test_invalid_fields_are_rejected();

    if (failures) {
        std::cerr << failures << " online lobby assertion(s) failed\n";
        return 1;
    }
    std::cout << "online lobby assertions passed\n";
    return 0;
}
