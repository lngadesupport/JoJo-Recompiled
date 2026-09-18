#include "core/online_directory.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

template <typename Predicate>
bool wait_until(Predicate&& predicate) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (predicate()) return true;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(2));
    }
    return false;
}

void test_publish_list_and_expiry() {
    auto server = jojo::OnlineDirectoryServer::bind(
        jojo::NetworkEndpoint::loopback(0u));
    CHECK(server);
    if (!server) return;

    auto host = jojo::OnlineDirectoryClient::create(
        server.value.local_endpoint());
    auto browser = jojo::OnlineDirectoryClient::create(
        server.value.local_endpoint());
    CHECK(host);
    CHECK(browser);
    if (!host || !browser) return;

    CHECK(host.value.publish_room(
        "DIO'S MANSION",
        "SOUTH AMERICA",
        "SLUS_010.60",
        27886u,
        1u,
        2u));
    CHECK(server.value.poll(100u));
    CHECK(server.value.room_count() == 1u);

    CHECK(browser.value.request_rooms(
        "SOUTH AMERICA", "SLUS_010.60"));

    jojo::OnlineDirectoryClientPoll result{};
    CHECK(wait_until([&] {
        if (!server.value.poll(101u)) return false;
        const auto polled = browser.value.poll();
        if (!polled) return false;
        if (polled.value.room_list_complete) {
            result = polled.value;
            return true;
        }
        return false;
    }));

    CHECK(result.rooms.size() == 1u);
    if (!result.rooms.empty()) {
        CHECK(result.rooms[0].name == "DIO'S MANSION");
        CHECK(result.rooms[0].region == "SOUTH AMERICA");
        CHECK(result.rooms[0].game_revision == "SLUS_010.60");
        const std::array<std::uint8_t, 4> loopback{
            127u, 0u, 0u, 1u};
        CHECK(result.rooms[0].gameplay_endpoint.ipv4 == loopback);
        CHECK(result.rooms[0].gameplay_endpoint.port == 27886u);
    }

    CHECK(server.value.poll(
        100u + jojo::kOnlineDirectoryRoomTtlMs + 1u));
    CHECK(server.value.room_count() == 0u);
}

void test_matchmaking_pairs_only_compatible_queue() {
    auto server = jojo::OnlineDirectoryServer::bind(
        jojo::NetworkEndpoint::loopback(0u));
    CHECK(server);
    if (!server) return;

    auto a = jojo::OnlineDirectoryClient::create(
        server.value.local_endpoint());
    auto b = jojo::OnlineDirectoryClient::create(
        server.value.local_endpoint());
    auto c = jojo::OnlineDirectoryClient::create(
        server.value.local_endpoint());
    CHECK(a);
    CHECK(b);
    CHECK(c);
    if (!a || !b || !c) return;

    CHECK(a.value.request_match(
        jojo::OnlineMatchQueue::ranked,
        "JOTARO",
        "SOUTH AMERICA",
        "SLUS_010.60",
        30001u));
    CHECK(b.value.request_match(
        jojo::OnlineMatchQueue::casual,
        "DIO",
        "SOUTH AMERICA",
        "SLUS_010.60",
        30002u));
    CHECK(server.value.poll(1000u));
    CHECK(server.value.queued_match_count() == 2u);

    CHECK(c.value.request_match(
        jojo::OnlineMatchQueue::ranked,
        "POLNAREFF",
        "SOUTH AMERICA",
        "SLUS_010.60",
        30003u));

    jojo::OnlineDirectoryClientPoll a_result{};
    jojo::OnlineDirectoryClientPoll c_result{};
    CHECK(wait_until([&] {
        if (!server.value.poll(1001u)) return false;
        const auto pa = a.value.poll();
        const auto pc = c.value.poll();
        if (!pa || !pc) return false;
        if (pa.value.match) a_result = pa.value;
        if (pc.value.match) c_result = pc.value;
        return a_result.match.has_value() &&
               c_result.match.has_value();
    }));

    CHECK(a_result.match.has_value());
    CHECK(c_result.match.has_value());
    if (a_result.match && c_result.match) {
        CHECK(a_result.match->queue ==
              jojo::OnlineMatchQueue::ranked);
        CHECK(c_result.match->queue ==
              jojo::OnlineMatchQueue::ranked);
        CHECK(a_result.match->remote_player_name == "POLNAREFF");
        CHECK(c_result.match->remote_player_name == "JOTARO");
        CHECK(a_result.match->local_is_host);
        CHECK(!c_result.match->local_is_host);
        CHECK(a_result.match->remote_endpoint.port == 30003u);
        CHECK(c_result.match->remote_endpoint.port == 30001u);
    }

    CHECK(server.value.queued_match_count() == 1u);
    CHECK(server.value.poll(
        1000u + jojo::kOnlineDirectoryMatchTtlMs + 1u));
    CHECK(server.value.queued_match_count() == 0u);
}
}

int main() {
    test_publish_list_and_expiry();
    test_matchmaking_pairs_only_compatible_queue();

    if (failures) {
        std::cerr << failures
                  << " online directory assertion(s) failed\n";
        return 1;
    }
    std::cout << "online directory assertions passed\n";
    return 0;
}
