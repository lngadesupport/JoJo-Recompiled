#pragma once

#include "core/network_transport.h"
#include "core/online_lobby.h"
#include "core/result.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

inline constexpr std::uint16_t kOnlineDirectoryDefaultPort = 27888u;
inline constexpr std::uint64_t kOnlineDirectoryRoomTtlMs = 5000u;
inline constexpr std::uint64_t kOnlineDirectoryMatchTtlMs = 15000u;

struct OnlineDirectoryRoom {
    std::string id{};
    std::string name{};
    std::string owner{};
    std::string region{};
    std::string game_revision{};
    NetworkEndpoint gameplay_endpoint{};
    std::uint8_t players{1u};
    std::uint8_t max_players{2u};
    bool in_game{};
    std::optional<std::uint32_t> directory_ping_ms{};

    friend bool operator==(
        const OnlineDirectoryRoom&,
        const OnlineDirectoryRoom&) = default;
};

struct OnlineDirectoryMatch {
    NetworkEndpoint remote_endpoint{};
    std::string remote_player_name{};
    OnlineMatchQueue queue{OnlineMatchQueue::casual};
    bool local_is_host{};

    friend bool operator==(
        const OnlineDirectoryMatch&,
        const OnlineDirectoryMatch&) = default;
};

struct OnlineDirectoryClientPoll {
    bool room_list_complete{};
    std::vector<OnlineDirectoryRoom> rooms{};
    std::optional<OnlineDirectoryMatch> match{};
};

class OnlineDirectoryClient {
public:
    OnlineDirectoryClient() = default;
    OnlineDirectoryClient(const OnlineDirectoryClient&) = delete;
    OnlineDirectoryClient& operator=(const OnlineDirectoryClient&) = delete;
    OnlineDirectoryClient(OnlineDirectoryClient&&) noexcept = default;
    OnlineDirectoryClient& operator=(OnlineDirectoryClient&&) noexcept = default;

    [[nodiscard]] static Result<OnlineDirectoryClient> create(
        NetworkEndpoint directory);

    [[nodiscard]] Result<void> publish_room(
        std::string_view name,
        std::string_view owner,
        std::string_view region,
        std::string_view game_revision,
        std::uint16_t gameplay_port,
        std::uint8_t players = 1u,
        std::uint8_t max_players = 2u,
        bool in_game = false);

    [[nodiscard]] Result<void> request_rooms(
        std::string_view region,
        std::string_view game_revision);

    [[nodiscard]] Result<void> request_match(
        OnlineMatchQueue queue,
        std::string_view player_name,
        std::string_view region,
        std::string_view game_revision,
        std::uint16_t gameplay_port);

    [[nodiscard]] Result<OnlineDirectoryClientPoll> poll();

    [[nodiscard]] NetworkEndpoint local_endpoint() const noexcept {
        return transport_.local_endpoint();
    }

private:
    NetworkEndpoint directory_{};
    UdpNetworkTransport transport_{};
    std::vector<OnlineDirectoryRoom> pending_rooms_{};
    std::optional<std::uint64_t> room_request_started_ms_{};
};

class OnlineDirectoryServer {
public:
    OnlineDirectoryServer() = default;
    OnlineDirectoryServer(const OnlineDirectoryServer&) = delete;
    OnlineDirectoryServer& operator=(const OnlineDirectoryServer&) = delete;
    OnlineDirectoryServer(OnlineDirectoryServer&&) noexcept = default;
    OnlineDirectoryServer& operator=(OnlineDirectoryServer&&) noexcept = default;

    [[nodiscard]] static Result<OnlineDirectoryServer> bind(
        NetworkEndpoint local = NetworkEndpoint{{
            0u, 0u, 0u, 0u}, kOnlineDirectoryDefaultPort});

    [[nodiscard]] Result<void> poll(std::uint64_t now_ms);

    [[nodiscard]] std::size_t room_count() const noexcept {
        return rooms_.size();
    }
    [[nodiscard]] std::size_t queued_match_count() const noexcept {
        return matches_.size();
    }
    [[nodiscard]] NetworkEndpoint local_endpoint() const noexcept {
        return transport_.local_endpoint();
    }

private:
    struct RoomRecord {
        OnlineDirectoryRoom room{};
        std::uint64_t expires_at_ms{};
    };

    struct MatchRecord {
        NetworkEndpoint source{};
        NetworkEndpoint gameplay{};
        OnlineMatchQueue queue{OnlineMatchQueue::casual};
        std::string player_name{};
        std::string region{};
        std::string game_revision{};
        std::uint64_t expires_at_ms{};
    };

    void expire(std::uint64_t now_ms) noexcept;

    UdpNetworkTransport transport_{};
    std::map<std::string, RoomRecord> rooms_{};
    std::vector<MatchRecord> matches_{};
};

} // namespace jojo
