#pragma once

#include "core/network_transport.h"
#include "core/online_lobby.h"
#include "core/result.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace jojo {

inline constexpr std::uint16_t kLanLobbyDiscoveryPort = 27887u;

struct LanLobbyAdvertisement {
    std::string name{"JOJO Lobby"};
    std::string region{"LAN"};
    std::string game_revision{};
    std::uint16_t gameplay_port{27886u};
    std::uint8_t players{1u};
    std::uint8_t max_players{2u};
    bool password_required{};

    friend bool operator==(
        const LanLobbyAdvertisement&,
        const LanLobbyAdvertisement&) = default;
};

[[nodiscard]] Result<std::vector<std::uint8_t>>
encode_lan_lobby_advertisement(
    const LanLobbyAdvertisement& advertisement);

[[nodiscard]] Result<LanLobbyAdvertisement>
decode_lan_lobby_advertisement(
    const std::vector<std::uint8_t>& bytes);

class LanLobbyDiscovery {
public:
    LanLobbyDiscovery() = default;
    LanLobbyDiscovery(const LanLobbyDiscovery&) = delete;
    LanLobbyDiscovery& operator=(const LanLobbyDiscovery&) = delete;
    LanLobbyDiscovery(LanLobbyDiscovery&&) noexcept = default;
    LanLobbyDiscovery& operator=(LanLobbyDiscovery&&) noexcept = default;

    [[nodiscard]] static Result<LanLobbyDiscovery> create();

    [[nodiscard]] Result<void> set_host(
        std::optional<LanLobbyAdvertisement> advertisement);
    [[nodiscard]] Result<void> request_scan();
    [[nodiscard]] Result<std::vector<OnlineRoomInfo>> poll();

    [[nodiscard]] const std::optional<LanLobbyAdvertisement>&
    hosted_advertisement() const noexcept {
        return hosted_advertisement_;
    }

private:
    UdpNetworkTransport scan_transport_{};
    std::optional<UdpNetworkTransport> host_transport_{};
    std::optional<LanLobbyAdvertisement> hosted_advertisement_{};
    std::map<std::string, OnlineRoomInfo> discovered_rooms_{};
};

} // namespace jojo
