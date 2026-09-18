#include "core/lan_lobby_discovery.h"

#include "core/online_session.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace jojo {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{
    'J','O','J','O','L','A','N','1'};
constexpr std::uint8_t kDiscoveryRequest = 1u;
constexpr std::uint8_t kDiscoveryAdvertisement = 2u;

bool append_text(
    std::vector<std::uint8_t>& out,
    std::string_view text,
    std::size_t max_length) {
    if (text.size() > max_length || text.size() > 255u) return false;
    out.push_back(static_cast<std::uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    return true;
}

bool read_text(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    std::string& out,
    std::size_t max_length) {
    if (offset >= bytes.size()) return false;
    const auto length = static_cast<std::size_t>(bytes[offset++]);
    if (length > max_length || offset + length > bytes.size()) return false;
    out.assign(
        reinterpret_cast<const char*>(bytes.data() + offset),
        length);
    offset += length;
    return true;
}

bool has_magic(const std::vector<std::uint8_t>& bytes) noexcept {
    if (bytes.size() < kMagic.size() + 1u) return false;
    for (std::size_t i = 0u; i < kMagic.size(); ++i) {
        if (bytes[i] != kMagic[i]) return false;
    }
    return true;
}

std::vector<std::uint8_t> discovery_request_packet() {
    std::vector<std::uint8_t> bytes(kMagic.begin(), kMagic.end());
    bytes.push_back(kDiscoveryRequest);
    return bytes;
}

} // namespace

Result<std::vector<std::uint8_t>>
encode_lan_lobby_advertisement(
    const LanLobbyAdvertisement& advertisement) {
    if (!valid_online_room_name(advertisement.name) ||
        advertisement.region.empty() ||
        advertisement.region.size() > 48u ||
        advertisement.game_revision.size() > 64u ||
        advertisement.gameplay_port == 0u ||
        advertisement.players > advertisement.max_players ||
        advertisement.max_players < 2u ||
        advertisement.max_players > 8u) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_argument,
            "LAN lobby advertisement is invalid");
    }

    std::vector<std::uint8_t> bytes(kMagic.begin(), kMagic.end());
    bytes.reserve(
        kMagic.size() + 16u +
        advertisement.name.size() +
        advertisement.owner.size() +
        advertisement.region.size() +
        advertisement.game_revision.size());
    bytes.push_back(kDiscoveryAdvertisement);
    bytes.push_back(
        static_cast<std::uint8_t>(advertisement.gameplay_port & 0xFFu));
    bytes.push_back(
        static_cast<std::uint8_t>(advertisement.gameplay_port >> 8u));
    bytes.push_back(advertisement.players);
    bytes.push_back(advertisement.max_players);
    bytes.push_back(
        static_cast<std::uint8_t>(
            advertisement.password_required ? 1u : 0u));
    bytes.push_back(
        static_cast<std::uint8_t>(
            advertisement.in_game ? 1u : 0u));

    if (!append_text(bytes, advertisement.name, 40u) ||
        !append_text(bytes, advertisement.owner, 24u) ||
        !append_text(bytes, advertisement.region, 48u) ||
        !append_text(bytes, advertisement.game_revision, 64u)) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_argument,
            "LAN lobby text fields exceed protocol limits");
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<LanLobbyAdvertisement>
decode_lan_lobby_advertisement(
    const std::vector<std::uint8_t>& bytes) {
    if (!has_magic(bytes) ||
        bytes[kMagic.size()] != kDiscoveryAdvertisement) {
        return Result<LanLobbyAdvertisement>::failure(
            ErrorCode::invalid_argument,
            "datagram is not a JOJO LAN lobby advertisement");
    }

    std::size_t offset = kMagic.size() + 1u;
    if (offset + 6u > bytes.size()) {
        return Result<LanLobbyAdvertisement>::failure(
            ErrorCode::invalid_argument,
            "LAN lobby advertisement is truncated");
    }

    LanLobbyAdvertisement advertisement{};
    advertisement.gameplay_port =
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
    offset += 2u;
    advertisement.players = bytes[offset++];
    advertisement.max_players = bytes[offset++];
    advertisement.password_required = bytes[offset++] != 0u;
    advertisement.in_game = bytes[offset++] != 0u;

    if (!read_text(bytes, offset, advertisement.name, 40u) ||
        !read_text(bytes, offset, advertisement.owner, 24u) ||
        !read_text(bytes, offset, advertisement.region, 48u) ||
        !read_text(bytes, offset, advertisement.game_revision, 64u) ||
        offset != bytes.size()) {
        return Result<LanLobbyAdvertisement>::failure(
            ErrorCode::invalid_argument,
            "LAN lobby advertisement has malformed text fields");
    }

    const auto validated =
        encode_lan_lobby_advertisement(advertisement);
    if (!validated) {
        return Result<LanLobbyAdvertisement>::failure(
            validated.error, validated.detail);
    }
    return Result<LanLobbyAdvertisement>::success(
        std::move(advertisement));
}

Result<LanLobbyDiscovery> LanLobbyDiscovery::create() {
    auto scanner = UdpNetworkTransport::bind(
        NetworkEndpoint{{0u, 0u, 0u, 0u}, 0u},
        UdpBindOptions{false, true});
    if (!scanner) {
        return Result<LanLobbyDiscovery>::failure(
            scanner.error, scanner.detail);
    }

    LanLobbyDiscovery discovery{};
    discovery.scan_transport_ = std::move(scanner.value);
    return Result<LanLobbyDiscovery>::success(std::move(discovery));
}

Result<void> LanLobbyDiscovery::set_host(
    std::optional<LanLobbyAdvertisement> advertisement) {
    if (!advertisement) {
        hosted_advertisement_.reset();
        host_transport_.reset();
        return Result<void>::success();
    }

    const auto encoded =
        encode_lan_lobby_advertisement(*advertisement);
    if (!encoded) {
        return Result<void>::failure(encoded.error, encoded.detail);
    }

    if (!host_transport_) {
        auto listener = UdpNetworkTransport::bind(
            NetworkEndpoint{{0u, 0u, 0u, 0u}, kLanLobbyDiscoveryPort},
            UdpBindOptions{true, true});
        if (!listener) {
            return Result<void>::failure(
                listener.error, listener.detail);
        }
        host_transport_ = std::move(listener.value);
    }

    hosted_advertisement_ = std::move(advertisement);
    return Result<void>::success();
}

Result<void> LanLobbyDiscovery::request_scan() {
    discovered_rooms_.clear();
    const auto request = discovery_request_packet();

    const auto broadcast = scan_transport_.send_datagram(
        NetworkEndpoint{{
            255u, 255u, 255u, 255u},
            kLanLobbyDiscoveryPort},
        request);
    if (!broadcast) {
        return broadcast;
    }

    // Also probe loopback so two local instances can be used for testing.
    (void)scan_transport_.send_datagram(
        NetworkEndpoint::loopback(kLanLobbyDiscoveryPort),
        request);
    return Result<void>::success();
}

Result<std::vector<OnlineRoomInfo>> LanLobbyDiscovery::poll() {
    if (host_transport_ && hosted_advertisement_) {
        for (;;) {
            const auto incoming =
                host_transport_->receive_datagram();
            if (!incoming) {
                return Result<std::vector<OnlineRoomInfo>>::failure(
                    incoming.error, incoming.detail);
            }
            if (!incoming.value) break;

            const auto& datagram = *incoming.value;
            if (!has_magic(datagram.bytes) ||
                datagram.bytes[kMagic.size()] != kDiscoveryRequest) {
                continue;
            }

            const auto encoded =
                encode_lan_lobby_advertisement(
                    *hosted_advertisement_);
            if (!encoded) {
                return Result<std::vector<OnlineRoomInfo>>::failure(
                    encoded.error, encoded.detail);
            }
            const auto sent = host_transport_->send_datagram(
                datagram.source, encoded.value);
            if (!sent) {
                return Result<std::vector<OnlineRoomInfo>>::failure(
                    sent.error, sent.detail);
            }
        }
    }

    for (;;) {
        const auto incoming = scan_transport_.receive_datagram();
        if (!incoming) {
            return Result<std::vector<OnlineRoomInfo>>::failure(
                incoming.error, incoming.detail);
        }
        if (!incoming.value) break;

        const auto parsed =
            decode_lan_lobby_advertisement(
                incoming.value->bytes);
        if (!parsed) continue;

        auto endpoint = incoming.value->source;
        endpoint.port = parsed.value.gameplay_port;
        const auto connect_endpoint =
            format_direct_endpoint(endpoint);

        OnlineRoomInfo room{};
        room.id = "lan:" + connect_endpoint;
        room.name = parsed.value.name;
        room.owner = parsed.value.owner;
        room.region = parsed.value.region;
        room.players = parsed.value.players;
        room.max_players = parsed.value.max_players;
        room.password_required =
            parsed.value.password_required;
        room.lan = true;
        room.status = parsed.value.in_game
            ? OnlineRoomStatus::in_game
            : (room.players >= room.max_players
                ? OnlineRoomStatus::full
                : OnlineRoomStatus::wait);
        room.available =
            room.status == OnlineRoomStatus::wait;
        room.connect_endpoint = connect_endpoint;
        room.game_revision = parsed.value.game_revision;
        discovered_rooms_[room.id] = std::move(room);
    }

    std::vector<OnlineRoomInfo> rooms;
    rooms.reserve(discovered_rooms_.size());
    for (const auto& [id, room] : discovered_rooms_) {
        (void)id;
        rooms.push_back(room);
    }
    return Result<std::vector<OnlineRoomInfo>>::success(
        std::move(rooms));
}

} // namespace jojo
