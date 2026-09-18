#include "core/lan_lobby_discovery.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

void test_advertisement_round_trip() {
    jojo::LanLobbyAdvertisement ad{};
    ad.name = "JoJo LAN";
    ad.owner = "JOTARO";
    ad.region = "SOUTH AMERICA";
    ad.game_revision = "SLUS_010.60";
    ad.gameplay_port = 27886u;
    ad.players = 1u;
    ad.max_players = 2u;
    ad.password_required = false;
    ad.in_game = true;
    ad.in_game = false;

    const auto encoded = jojo::encode_lan_lobby_advertisement(ad);
    CHECK(encoded);
    if (!encoded) return;

    const auto decoded =
        jojo::decode_lan_lobby_advertisement(encoded.value);
    CHECK(decoded);
    if (decoded) CHECK(decoded.value == ad);

    auto malformed = encoded.value;
    malformed.push_back(0xFFu);
    CHECK(!jojo::decode_lan_lobby_advertisement(malformed));
}

void test_loopback_discovery_finds_host() {
    auto host = jojo::LanLobbyDiscovery::create();
    auto client = jojo::LanLobbyDiscovery::create();
    CHECK(host);
    CHECK(client);
    if (!host || !client) return;

    jojo::LanLobbyAdvertisement ad{};
    ad.name = "Local Arena";
    ad.owner = "DIO";
    ad.region = "LAN";
    ad.game_revision = "SLUS_010.60";
    CHECK(host.value.set_host(ad));
    CHECK(client.value.request_scan());

    std::vector<jojo::OnlineRoomInfo> rooms;
    for (int attempt = 0; attempt < 30 && rooms.empty(); ++attempt) {
        const auto hosted = host.value.poll();
        CHECK(hosted);
        const auto discovered = client.value.poll();
        CHECK(discovered);
        if (discovered) rooms = discovered.value;
        if (rooms.empty()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5));
        }
    }

    CHECK(!rooms.empty());
    if (!rooms.empty()) {
        CHECK(rooms.front().name == "Local Arena");
        CHECK(rooms.front().owner == "DIO");
        CHECK(rooms.front().region == "LAN");
        CHECK(rooms.front().lan);
        CHECK(rooms.front().status == jojo::OnlineRoomStatus::wait);
        CHECK(rooms.front().players == 1u);
        CHECK(rooms.front().max_players == 2u);
        CHECK(!rooms.front().connect_endpoint.empty());
        CHECK(rooms.front().connect_endpoint.find(":27886") !=
              std::string::npos);
    }

    CHECK(host.value.set_host(std::nullopt));
    CHECK(!host.value.hosted_advertisement().has_value());
}
}

int main() {
    test_advertisement_round_trip();
    test_loopback_discovery_finds_host();

    if (failures) {
        std::cerr << failures
                  << " LAN lobby discovery assertion(s) failed\n";
        return 1;
    }
    std::cout << "LAN lobby discovery assertions passed\n";
    return 0;
}
