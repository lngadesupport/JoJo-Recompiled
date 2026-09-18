#include "core/online_directory.h"

#include "core/online_session.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace jojo {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{
    'J','O','J','O','D','I','R','1'};

enum class MessageKind : std::uint8_t {
    publish_room = 1u,
    list_request = 2u,
    list_item = 3u,
    list_done = 4u,
    match_request = 5u,
    match_found = 6u,
};

bool valid_text(
    std::string_view text,
    std::size_t max_length,
    bool allow_empty = false) noexcept {
    if ((!allow_empty && text.empty()) || text.size() > max_length) {
        return false;
    }
    for (const unsigned char ch : text) {
        if (ch < 0x20u || ch == 0x7Fu) return false;
    }
    return true;
}

void append_u16(
    std::vector<std::uint8_t>& out,
    std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8u));
}

bool read_u16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    std::uint16_t& value) noexcept {
    if (offset + 2u > bytes.size()) return false;
    value = static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
    offset += 2u;
    return true;
}

bool append_text(
    std::vector<std::uint8_t>& out,
    std::string_view text,
    std::size_t max_length,
    bool allow_empty = false) {
    if (!valid_text(text, max_length, allow_empty) ||
        text.size() > 255u) {
        return false;
    }
    out.push_back(static_cast<std::uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    return true;
}

bool read_text(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    std::string& text,
    std::size_t max_length,
    bool allow_empty = false) {
    if (offset >= bytes.size()) return false;
    const auto length = static_cast<std::size_t>(bytes[offset++]);
    if (offset + length > bytes.size()) return false;
    text.assign(
        reinterpret_cast<const char*>(bytes.data() + offset),
        length);
    offset += length;
    return valid_text(text, max_length, allow_empty);
}

std::vector<std::uint8_t> begin_packet(MessageKind kind) {
    std::vector<std::uint8_t> out(kMagic.begin(), kMagic.end());
    out.push_back(static_cast<std::uint8_t>(kind));
    return out;
}

bool decode_header(
    const std::vector<std::uint8_t>& bytes,
    MessageKind& kind,
    std::size_t& offset) noexcept {
    if (bytes.size() < kMagic.size() + 1u) return false;
    for (std::size_t i = 0u; i < kMagic.size(); ++i) {
        if (bytes[i] != kMagic[i]) return false;
    }
    kind = static_cast<MessageKind>(bytes[kMagic.size()]);
    offset = kMagic.size() + 1u;
    return true;
}

std::string endpoint_key(NetworkEndpoint endpoint) {
    return format_direct_endpoint(endpoint);
}

std::vector<std::uint8_t> encode_list_request(
    std::string_view region,
    std::string_view revision) {
    auto out = begin_packet(MessageKind::list_request);
    if (!append_text(out, region, 48u, true) ||
        !append_text(out, revision, 64u, true)) {
        return {};
    }
    return out;
}

std::vector<std::uint8_t> encode_room_publish(
    std::string_view name,
    std::string_view region,
    std::string_view revision,
    std::uint16_t gameplay_port,
    std::uint8_t players,
    std::uint8_t max_players) {
    if (gameplay_port == 0u ||
        max_players < 2u ||
        max_players > 8u ||
        players > max_players) {
        return {};
    }
    auto out = begin_packet(MessageKind::publish_room);
    append_u16(out, gameplay_port);
    out.push_back(players);
    out.push_back(max_players);
    if (!append_text(out, name, 40u) ||
        !append_text(out, region, 48u) ||
        !append_text(out, revision, 64u)) {
        return {};
    }
    return out;
}

std::vector<std::uint8_t> encode_list_item(
    const OnlineDirectoryRoom& room) {
    auto out = begin_packet(MessageKind::list_item);
    for (const auto octet : room.gameplay_endpoint.ipv4) {
        out.push_back(octet);
    }
    append_u16(out, room.gameplay_endpoint.port);
    out.push_back(room.players);
    out.push_back(room.max_players);
    if (!append_text(out, room.id, 64u) ||
        !append_text(out, room.name, 40u) ||
        !append_text(out, room.region, 48u) ||
        !append_text(out, room.game_revision, 64u)) {
        return {};
    }
    return out;
}

std::vector<std::uint8_t> encode_match_request(
    OnlineMatchQueue queue,
    std::string_view player_name,
    std::string_view region,
    std::string_view revision,
    std::uint16_t gameplay_port) {
    if (gameplay_port == 0u) return {};
    auto out = begin_packet(MessageKind::match_request);
    out.push_back(
        queue == OnlineMatchQueue::ranked ? 1u : 0u);
    append_u16(out, gameplay_port);
    if (!append_text(out, player_name, 24u) ||
        !append_text(out, region, 48u) ||
        !append_text(out, revision, 64u)) {
        return {};
    }
    return out;
}

std::vector<std::uint8_t> encode_match_found(
    const OnlineDirectoryMatch& match) {
    auto out = begin_packet(MessageKind::match_found);
    out.push_back(
        match.queue == OnlineMatchQueue::ranked ? 1u : 0u);
    for (const auto octet : match.remote_endpoint.ipv4) {
        out.push_back(octet);
    }
    append_u16(out, match.remote_endpoint.port);
    if (!append_text(
            out, match.remote_player_name, 24u)) {
        return {};
    }
    return out;
}



} // namespace

Result<OnlineDirectoryClient> OnlineDirectoryClient::create(
    NetworkEndpoint directory) {
    if (directory.port == 0u) {
        return Result<OnlineDirectoryClient>::failure(
            ErrorCode::invalid_argument,
            "online directory endpoint requires a port");
    }
    auto transport = UdpNetworkTransport::bind(
        NetworkEndpoint{{0u, 0u, 0u, 0u}, 0u});
    if (!transport) {
        return Result<OnlineDirectoryClient>::failure(
            transport.error, transport.detail);
    }
    OnlineDirectoryClient client{};
    client.directory_ = directory;
    client.transport_ = std::move(transport.value);
    return Result<OnlineDirectoryClient>::success(
        std::move(client));
}

Result<void> OnlineDirectoryClient::publish_room(
    std::string_view name,
    std::string_view region,
    std::string_view game_revision,
    std::uint16_t gameplay_port,
    std::uint8_t players,
    std::uint8_t max_players) {
    const auto packet = encode_room_publish(
        name, region, game_revision, gameplay_port,
        players, max_players);
    if (packet.empty()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "invalid online directory room announcement");
    }
    return transport_.send_datagram(directory_, packet);
}

Result<void> OnlineDirectoryClient::request_rooms(
    std::string_view region,
    std::string_view game_revision) {
    const auto packet =
        encode_list_request(region, game_revision);
    if (packet.empty()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "invalid online directory room filter");
    }
    pending_rooms_.clear();
    return transport_.send_datagram(directory_, packet);
}

Result<void> OnlineDirectoryClient::request_match(
    OnlineMatchQueue queue,
    std::string_view player_name,
    std::string_view region,
    std::string_view game_revision,
    std::uint16_t gameplay_port) {
    const auto packet = encode_match_request(
        queue, player_name, region, game_revision,
        gameplay_port);
    if (packet.empty()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "invalid online directory match request");
    }
    return transport_.send_datagram(directory_, packet);
}

Result<OnlineDirectoryClientPoll>
OnlineDirectoryClient::poll() {
    OnlineDirectoryClientPoll result{};
    for (;;) {
        const auto incoming = transport_.receive_datagram();
        if (!incoming) {
            return Result<OnlineDirectoryClientPoll>::failure(
                incoming.error, incoming.detail);
        }
        if (!incoming.value) break;
        if (incoming.value->source != directory_) continue;

        MessageKind kind{};
        std::size_t offset = 0u;
        if (!decode_header(
                incoming.value->bytes, kind, offset)) {
            continue;
        }

        if (kind == MessageKind::list_done) {
            result.room_list_complete = true;
            result.rooms = pending_rooms_;
            continue;
        }

        if (kind == MessageKind::list_item) {
            const auto& bytes = incoming.value->bytes;
            if (offset + 8u > bytes.size()) continue;

            OnlineDirectoryRoom room{};
            for (std::size_t i = 0u; i < 4u; ++i) {
                room.gameplay_endpoint.ipv4[i] =
                    bytes[offset++];
            }
            if (!read_u16(
                    bytes, offset,
                    room.gameplay_endpoint.port)) {
                continue;
            }
            room.players = bytes[offset++];
            room.max_players = bytes[offset++];

            if (!read_text(bytes, offset, room.id, 64u) ||
                !read_text(bytes, offset, room.name, 40u) ||
                !read_text(bytes, offset, room.region, 48u) ||
                !read_text(
                    bytes, offset, room.game_revision, 64u) ||
                offset != bytes.size()) {
                continue;
            }
            pending_rooms_.push_back(std::move(room));
            continue;
        }

        if (kind == MessageKind::match_found) {
            const auto& bytes = incoming.value->bytes;
            if (offset + 7u > bytes.size()) continue;
            OnlineDirectoryMatch match{};
            match.queue =
                bytes[offset++] != 0u
                ? OnlineMatchQueue::ranked
                : OnlineMatchQueue::casual;
            for (std::size_t i = 0u; i < 4u; ++i) {
                match.remote_endpoint.ipv4[i] =
                    bytes[offset++];
            }
            if (!read_u16(
                    bytes, offset,
                    match.remote_endpoint.port) ||
                !read_text(
                    bytes, offset,
                    match.remote_player_name, 24u) ||
                offset != bytes.size()) {
                continue;
            }
            result.match = std::move(match);
        }
    }
    return Result<OnlineDirectoryClientPoll>::success(
        std::move(result));
}

Result<OnlineDirectoryServer> OnlineDirectoryServer::bind(
    NetworkEndpoint local) {
    auto transport = UdpNetworkTransport::bind(
        local, UdpBindOptions{true, false});
    if (!transport) {
        return Result<OnlineDirectoryServer>::failure(
            transport.error, transport.detail);
    }
    OnlineDirectoryServer server{};
    server.transport_ = std::move(transport.value);
    return Result<OnlineDirectoryServer>::success(
        std::move(server));
}

void OnlineDirectoryServer::expire(
    std::uint64_t now_ms) noexcept {
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        if (it->second.expires_at_ms <= now_ms) {
            it = rooms_.erase(it);
        } else {
            ++it;
        }
    }
    matches_.erase(
        std::remove_if(
            matches_.begin(), matches_.end(),
            [now_ms](const MatchRecord& entry) {
                return entry.expires_at_ms <= now_ms;
            }),
        matches_.end());
}

Result<void> OnlineDirectoryServer::poll(
    std::uint64_t now_ms) {
    expire(now_ms);

    for (;;) {
        const auto incoming = transport_.receive_datagram();
        if (!incoming) {
            return Result<void>::failure(
                incoming.error, incoming.detail);
        }
        if (!incoming.value) break;

        const auto& datagram = *incoming.value;
        MessageKind kind{};
        std::size_t offset = 0u;
        if (!decode_header(datagram.bytes, kind, offset)) {
            continue;
        }

        if (kind == MessageKind::publish_room) {
            std::uint16_t gameplay_port = 0u;
            if (!read_u16(
                    datagram.bytes, offset, gameplay_port) ||
                offset + 2u > datagram.bytes.size()) {
                continue;
            }
            const auto players = datagram.bytes[offset++];
            const auto max_players = datagram.bytes[offset++];

            OnlineDirectoryRoom room{};
            room.gameplay_endpoint = datagram.source;
            room.gameplay_endpoint.port = gameplay_port;
            room.players = players;
            room.max_players = max_players;
            if (!read_text(
                    datagram.bytes, offset, room.name, 40u) ||
                !read_text(
                    datagram.bytes, offset, room.region, 48u) ||
                !read_text(
                    datagram.bytes, offset,
                    room.game_revision, 64u) ||
                offset != datagram.bytes.size() ||
                gameplay_port == 0u ||
                max_players < 2u ||
                max_players > 8u ||
                players > max_players) {
                continue;
            }
            room.id = endpoint_key(room.gameplay_endpoint);
            rooms_[room.id] =
                RoomRecord{room, now_ms + kOnlineDirectoryRoomTtlMs};
            continue;
        }

        if (kind == MessageKind::list_request) {
            std::string region;
            std::string revision;
            if (!read_text(
                    datagram.bytes, offset, region, 48u, true) ||
                !read_text(
                    datagram.bytes, offset, revision, 64u, true) ||
                offset != datagram.bytes.size()) {
                continue;
            }

            for (const auto& [id, record] : rooms_) {
                (void)id;
                if (!region.empty() &&
                    record.room.region != region) {
                    continue;
                }
                if (!revision.empty() &&
                    record.room.game_revision != revision) {
                    continue;
                }
                const auto packet =
                    encode_list_item(record.room);
                if (packet.empty()) continue;
                const auto sent =
                    transport_.send_datagram(
                        datagram.source, packet);
                if (!sent) return sent;
            }
            const auto done =
                begin_packet(MessageKind::list_done);
            const auto sent =
                transport_.send_datagram(
                    datagram.source, done);
            if (!sent) return sent;
            continue;
        }

        if (kind == MessageKind::match_request) {
            if (offset + 3u > datagram.bytes.size()) continue;
            MatchRecord request{};
            request.queue =
                datagram.bytes[offset++] != 0u
                ? OnlineMatchQueue::ranked
                : OnlineMatchQueue::casual;
            request.source = datagram.source;
            request.gameplay = datagram.source;
            if (!read_u16(
                    datagram.bytes, offset,
                    request.gameplay.port) ||
                !read_text(
                    datagram.bytes, offset,
                    request.player_name, 24u) ||
                !read_text(
                    datagram.bytes, offset,
                    request.region, 48u) ||
                !read_text(
                    datagram.bytes, offset,
                    request.game_revision, 64u) ||
                offset != datagram.bytes.size() ||
                request.gameplay.port == 0u) {
                continue;
            }
            request.expires_at_ms =
                now_ms + kOnlineDirectoryMatchTtlMs;

            auto peer = std::find_if(
                matches_.begin(), matches_.end(),
                [&](const MatchRecord& candidate) {
                    return request.queue == candidate.queue &&
                        request.region == candidate.region &&
                        request.game_revision ==
                            candidate.game_revision &&
                        request.source != candidate.source;
                });
            if (peer == matches_.end()) {
                matches_.push_back(std::move(request));
                continue;
            }

            OnlineDirectoryMatch to_request{};
            to_request.remote_endpoint = peer->gameplay;
            to_request.remote_player_name = peer->player_name;
            to_request.queue = request.queue;

            OnlineDirectoryMatch to_peer{};
            to_peer.remote_endpoint = request.gameplay;
            to_peer.remote_player_name = request.player_name;
            to_peer.queue = request.queue;

            const auto request_packet =
                encode_match_found(to_request);
            const auto peer_packet =
                encode_match_found(to_peer);
            if (request_packet.empty() || peer_packet.empty()) {
                matches_.erase(peer);
                continue;
            }

            auto sent = transport_.send_datagram(
                request.source, request_packet);
            if (!sent) return sent;
            sent = transport_.send_datagram(
                peer->source, peer_packet);
            if (!sent) return sent;
            matches_.erase(peer);
        }
    }
    return Result<void>::success();
}

} // namespace jojo
