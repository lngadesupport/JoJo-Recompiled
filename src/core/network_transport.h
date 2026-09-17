#pragma once

#include "core/network_protocol.h"
#include "core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace jojo {

inline constexpr std::size_t kNetworkTransportMaxDatagram = 1200u;

struct NetworkEndpoint {
    std::array<std::uint8_t, 4> ipv4{};
    std::uint16_t port{};

    [[nodiscard]] static constexpr NetworkEndpoint loopback(
        std::uint16_t port_value) noexcept {
        return NetworkEndpoint{{127u, 0u, 0u, 1u}, port_value};
    }

    friend bool operator==(const NetworkEndpoint&, const NetworkEndpoint&) = default;
};

struct UdpDatagram {
    NetworkEndpoint source{};
    std::vector<std::uint8_t> bytes;
};

struct ReceivedNetworkPacket {
    NetworkEndpoint source{};
    NetworkPacket packet{};
};

class UdpNetworkTransport {
public:
    UdpNetworkTransport() noexcept = default;
    UdpNetworkTransport(const UdpNetworkTransport&) = delete;
    UdpNetworkTransport& operator=(const UdpNetworkTransport&) = delete;

    UdpNetworkTransport(UdpNetworkTransport&& other) noexcept {
        move_from(other);
    }

    UdpNetworkTransport& operator=(UdpNetworkTransport&& other) noexcept {
        if (this != &other) {
            close();
            move_from(other);
        }
        return *this;
    }

    ~UdpNetworkTransport() { close(); }

    [[nodiscard]] static Result<UdpNetworkTransport> bind(NetworkEndpoint endpoint) {
        UdpNetworkTransport transport;
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return Result<UdpNetworkTransport>::failure(
                ErrorCode::backend_unavailable, "Winsock startup failed");
        }
        transport.winsock_started_ = true;
#endif
        const auto socket_value = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_value == invalid_socket()) {
            return Result<UdpNetworkTransport>::failure(
                ErrorCode::backend_unavailable, "UDP socket creation failed");
        }
        transport.socket_ = socket_value;

        const auto address = make_sockaddr(endpoint);
        if (::bind(transport.socket_, reinterpret_cast<const sockaddr*>(&address),
                   static_cast<socket_length_type>(sizeof(address))) != 0) {
            return Result<UdpNetworkTransport>::failure(
                ErrorCode::io_error, "UDP bind failed");
        }
        if (!set_nonblocking(transport.socket_)) {
            return Result<UdpNetworkTransport>::failure(
                ErrorCode::io_error, "UDP nonblocking mode could not be enabled");
        }

        sockaddr_in local{};
        socket_length_type local_length = static_cast<socket_length_type>(sizeof(local));
        if (::getsockname(transport.socket_, reinterpret_cast<sockaddr*>(&local),
                          &local_length) != 0) {
            return Result<UdpNetworkTransport>::failure(
                ErrorCode::io_error, "UDP local endpoint query failed");
        }
        transport.local_ = endpoint_from_sockaddr(local);
        return Result<UdpNetworkTransport>::success(std::move(transport));
    }

    [[nodiscard]] NetworkEndpoint local_endpoint() const noexcept { return local_; }
    [[nodiscard]] bool is_open() const noexcept { return socket_ != invalid_socket(); }

    [[nodiscard]] Result<void> send_datagram(
        NetworkEndpoint destination,
        std::span<const std::uint8_t> bytes) {
        if (!is_open()) {
            return Result<void>::failure(ErrorCode::backend_unavailable,
                                         "UDP socket is closed");
        }
        if (destination.port == 0u) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "UDP destination port must be non-zero");
        }
        if (bytes.size() > kNetworkTransportMaxDatagram) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "UDP datagram exceeds transport limit");
        }
        const auto address = make_sockaddr(destination);
#ifdef _WIN32
        const auto sent = ::sendto(
            socket_, reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size()), 0,
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<int>(sizeof(address)));
        if (sent == SOCKET_ERROR || static_cast<std::size_t>(sent) != bytes.size()) {
#else
        const auto sent = ::sendto(
            socket_, bytes.data(), bytes.size(), 0,
            reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
#endif
            return Result<void>::failure(ErrorCode::io_error,
                                         "UDP datagram send failed");
        }
        return Result<void>::success();
    }

    [[nodiscard]] Result<std::optional<UdpDatagram>> receive_datagram() {
        if (!is_open()) {
            return Result<std::optional<UdpDatagram>>::failure(
                ErrorCode::backend_unavailable, "UDP socket is closed");
        }
        std::array<std::uint8_t, kNetworkTransportMaxDatagram> buffer{};
        sockaddr_in source{};
        socket_length_type source_length = static_cast<socket_length_type>(sizeof(source));
#ifdef _WIN32
        const auto received = ::recvfrom(
            socket_, reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&source), &source_length);
        if (received == SOCKET_ERROR) {
#else
        const auto received = ::recvfrom(
            socket_, buffer.data(), buffer.size(), 0,
            reinterpret_cast<sockaddr*>(&source), &source_length);
        if (received < 0) {
#endif
            if (would_block()) {
                return Result<std::optional<UdpDatagram>>::success(std::nullopt);
            }
            return Result<std::optional<UdpDatagram>>::failure(
                ErrorCode::io_error, "UDP datagram receive failed");
        }

        UdpDatagram datagram{};
        datagram.source = endpoint_from_sockaddr(source);
        datagram.bytes.assign(buffer.begin(),
                              buffer.begin() + static_cast<std::ptrdiff_t>(received));
        return Result<std::optional<UdpDatagram>>::success(
            std::optional<UdpDatagram>{std::move(datagram)});
    }

    [[nodiscard]] Result<void> send_packet(NetworkEndpoint destination,
                                           const NetworkPacket& packet) {
        const auto encoded = serialize_network_packet(packet);
        if (!encoded) return Result<void>::failure(encoded.error, encoded.detail);
        return send_datagram(destination, encoded.value);
    }

    [[nodiscard]] Result<std::optional<ReceivedNetworkPacket>> receive_packet() {
        const auto datagram = receive_datagram();
        if (!datagram) {
            return Result<std::optional<ReceivedNetworkPacket>>::failure(
                datagram.error, datagram.detail);
        }
        if (!datagram.value) {
            return Result<std::optional<ReceivedNetworkPacket>>::success(std::nullopt);
        }
        const auto parsed = parse_network_packet(datagram.value->bytes);
        if (!parsed) {
            return Result<std::optional<ReceivedNetworkPacket>>::failure(
                parsed.error, parsed.detail);
        }
        ReceivedNetworkPacket received{};
        received.source = datagram.value->source;
        received.packet = parsed.value;
        return Result<std::optional<ReceivedNetworkPacket>>::success(
            std::optional<ReceivedNetworkPacket>{std::move(received)});
    }

private:
#ifdef _WIN32
    using native_socket_type = SOCKET;
    using socket_length_type = int;
#else
    using native_socket_type = int;
    using socket_length_type = socklen_t;
#endif

    [[nodiscard]] static constexpr native_socket_type invalid_socket() noexcept {
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -1;
#endif
    }

    [[nodiscard]] static sockaddr_in make_sockaddr(NetworkEndpoint endpoint) noexcept {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(endpoint.port);
        const auto host = (static_cast<std::uint32_t>(endpoint.ipv4[0]) << 24u) |
                          (static_cast<std::uint32_t>(endpoint.ipv4[1]) << 16u) |
                          (static_cast<std::uint32_t>(endpoint.ipv4[2]) << 8u) |
                          static_cast<std::uint32_t>(endpoint.ipv4[3]);
        address.sin_addr.s_addr = htonl(host);
        return address;
    }

    [[nodiscard]] static NetworkEndpoint endpoint_from_sockaddr(
        const sockaddr_in& address) noexcept {
        const auto host = ntohl(address.sin_addr.s_addr);
        return NetworkEndpoint{{
            static_cast<std::uint8_t>(host >> 24u),
            static_cast<std::uint8_t>(host >> 16u),
            static_cast<std::uint8_t>(host >> 8u),
            static_cast<std::uint8_t>(host)},
            ntohs(address.sin_port)};
    }

    [[nodiscard]] static bool set_nonblocking(native_socket_type socket_value) noexcept {
#ifdef _WIN32
        u_long enabled = 1u;
        return ::ioctlsocket(socket_value, FIONBIO, &enabled) == 0;
#else
        const auto flags = ::fcntl(socket_value, F_GETFL, 0);
        return flags >= 0 && ::fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    }

    [[nodiscard]] static bool would_block() noexcept {
#ifdef _WIN32
        return WSAGetLastError() == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
    }

    void close() noexcept {
        if (socket_ != invalid_socket()) {
#ifdef _WIN32
            ::closesocket(socket_);
#else
            ::close(socket_);
#endif
            socket_ = invalid_socket();
        }
#ifdef _WIN32
        if (winsock_started_) {
            WSACleanup();
            winsock_started_ = false;
        }
#endif
        local_ = {};
    }

    void move_from(UdpNetworkTransport& other) noexcept {
        socket_ = other.socket_;
        local_ = other.local_;
        other.socket_ = invalid_socket();
        other.local_ = {};
#ifdef _WIN32
        winsock_started_ = other.winsock_started_;
        other.winsock_started_ = false;
#endif
    }

    native_socket_type socket_{invalid_socket()};
    NetworkEndpoint local_{};
#ifdef _WIN32
    bool winsock_started_{};
#endif
};

enum class DirectSessionRole {
    host,
    client,
};

struct DirectSessionTiming {
    std::uint64_t retry_interval_ms{100u};
    std::uint64_t heartbeat_interval_ms{500u};
    std::uint64_t liveness_timeout_ms{2000u};
    std::uint64_t reconnect_timeout_ms{5000u};
};

enum class DirectSessionState {
    idle,
    connecting,
    connected,
    reconnecting,
    disconnected,
};

class DirectUdpSession {
public:
    DirectUdpSession() = default;
    DirectUdpSession(const DirectUdpSession&) = delete;
    DirectUdpSession& operator=(const DirectUdpSession&) = delete;
    DirectUdpSession(DirectUdpSession&&) noexcept = default;
    DirectUdpSession& operator=(DirectUdpSession&&) noexcept = default;

    [[nodiscard]] static Result<DirectUdpSession> bind(
        DirectSessionRole role,
        NetworkEndpoint local,
        std::uint64_t retry_interval_ms = 100u) {
        DirectSessionTiming timing{};
        timing.retry_interval_ms = retry_interval_ms;
        return bind(role, local, timing);
    }

    [[nodiscard]] static Result<DirectUdpSession> bind(
        DirectSessionRole role,
        NetworkEndpoint local,
        DirectSessionTiming timing) {
        if (timing.heartbeat_interval_ms == 0u ||
            timing.liveness_timeout_ms == 0u ||
            timing.reconnect_timeout_ms == 0u) {
            return Result<DirectUdpSession>::failure(
                ErrorCode::invalid_argument,
                "direct-session heartbeat/liveness/reconnect timing must be non-zero");
        }
        if (timing.liveness_timeout_ms < timing.heartbeat_interval_ms) {
            return Result<DirectUdpSession>::failure(
                ErrorCode::invalid_argument,
                "direct-session liveness timeout must not be shorter than heartbeat interval");
        }

        auto transport = UdpNetworkTransport::bind(local);
        if (!transport) {
            return Result<DirectUdpSession>::failure(transport.error, transport.detail);
        }
        DirectUdpSession session;
        session.role_ = role;
        session.timing_ = timing;
        session.transport_ = std::move(transport.value);
        session.reliability_.emplace(timing.retry_interval_ms);
        session.telemetry_.state = NetworkConnectionState::disconnected;
        return Result<DirectUdpSession>::success(std::move(session));
    }

    [[nodiscard]] NetworkEndpoint local_endpoint() const noexcept {
        return transport_.local_endpoint();
    }
    [[nodiscard]] DirectSessionState state() const noexcept { return state_; }
    [[nodiscard]] const std::optional<NetworkEndpoint>& remote_endpoint() const noexcept {
        return remote_;
    }
    [[nodiscard]] const NetworkTelemetry& telemetry() const noexcept { return telemetry_; }

    [[nodiscard]] Result<void> connect(NetworkEndpoint remote, std::uint64_t now_ms) {
        if (role_ != DirectSessionRole::client) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "only a direct-session client initiates connect");
        }
        if (remote.port == 0u) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "direct-session remote port must be non-zero");
        }
        if (state_ == DirectSessionState::connected ||
            state_ == DirectSessionState::connecting ||
            state_ == DirectSessionState::reconnecting) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "direct-session connect is already active");
        }

        remote_ = remote;
        const auto hello = send_new_hello(now_ms);
        if (!hello) {
            if (hello_sequence_ != 0u) reliability_->acknowledge(hello_sequence_);
            hello_sequence_ = 0u;
            remote_.reset();
            return hello;
        }
        state_ = DirectSessionState::connecting;
        telemetry_.state = NetworkConnectionState::reconnecting;
        return Result<void>::success();
    }

    [[nodiscard]] Result<std::vector<NetworkPacket>> poll(std::uint64_t now_ms) {
        std::vector<NetworkPacket> delivered;
        if (!reliability_) {
            return Result<std::vector<NetworkPacket>>::failure(
                ErrorCode::backend_unavailable, "direct UDP session is not bound");
        }

        if (state_ == DirectSessionState::reconnecting &&
            interval_elapsed(now_ms, reconnect_started_ms_, timing_.reconnect_timeout_ms)) {
            mark_disconnected();
            return Result<std::vector<NetworkPacket>>::success(std::move(delivered));
        }

        if (remote_ && state_ != DirectSessionState::disconnected) {
            for (const auto& packet : reliability_->due_retransmits(now_ms)) {
                const auto resent = transport_.send_packet(*remote_, packet);
                if (!resent) {
                    return Result<std::vector<NetworkPacket>>::failure(
                        resent.error, resent.detail);
                }
                telemetry_.record_packet_sent();
            }
        }

        for (;;) {
            const auto incoming = transport_.receive_packet();
            if (!incoming) {
                return Result<std::vector<NetworkPacket>>::failure(
                    incoming.error, incoming.detail);
            }
            if (!incoming.value) break;

            const auto& source = incoming.value->source;
            const auto& packet = incoming.value->packet;
            if (state_ == DirectSessionState::disconnected) continue;

            if (role_ == DirectSessionRole::host &&
                state_ == DirectSessionState::idle) {
                if (packet.kind != NetworkPacketKind::session_hello) continue;
                remote_ = source;
            } else if (!remote_ || source != *remote_) {
                continue;
            }

            telemetry_.record_packet_received();
            if (packet.ack != 0u) reliability_->acknowledge(packet.ack);

            if ((state_ == DirectSessionState::connected ||
                 state_ == DirectSessionState::reconnecting) &&
                packet.kind == NetworkPacketKind::disconnect) {
                mark_disconnected();
                continue;
            }

            if (role_ == DirectSessionRole::host &&
                packet.kind == NetworkPacketKind::session_hello) {
                NetworkPacket accept{};
                accept.kind = NetworkPacketKind::session_accept;
                accept.sequence = next_sequence_++;
                accept.ack = packet.sequence;
                accept.timestamp_ms = now_ms;
                const auto tracked = reliability_->track(accept, now_ms);
                if (!tracked) {
                    return Result<std::vector<NetworkPacket>>::failure(
                        tracked.error, tracked.detail);
                }
                const auto sent = send_control_packet(accept);
                if (!sent) {
                    reliability_->acknowledge(accept.sequence);
                    return Result<std::vector<NetworkPacket>>::failure(
                        sent.error, sent.detail);
                }
                mark_connected(now_ms);
                continue;
            }

            if (role_ == DirectSessionRole::client &&
                (state_ == DirectSessionState::connecting ||
                 state_ == DirectSessionState::reconnecting) &&
                packet.kind == NetworkPacketKind::session_accept) {
                if (packet.ack != hello_sequence_) continue;
                reliability_->acknowledge(hello_sequence_);
                mark_connected(now_ms);

                NetworkPacket acknowledge{};
                acknowledge.kind = NetworkPacketKind::ping;
                acknowledge.sequence = next_sequence_++;
                acknowledge.ack = packet.sequence;
                acknowledge.timestamp_ms = now_ms;
                const auto sent = send_control_packet(acknowledge);
                if (!sent) {
                    return Result<std::vector<NetworkPacket>>::failure(
                        sent.error, sent.detail);
                }
                continue;
            }

            if (state_ != DirectSessionState::connected) continue;
            refresh_peer_liveness(now_ms);

            if (packet.kind == NetworkPacketKind::ping) {
                NetworkPacket pong{};
                pong.kind = NetworkPacketKind::pong;
                pong.sequence = next_sequence_++;
                pong.ack = packet.sequence;
                pong.timestamp_ms = packet.timestamp_ms;
                const auto sent = send_control_packet(pong);
                if (!sent) {
                    return Result<std::vector<NetworkPacket>>::failure(
                        sent.error, sent.detail);
                }
                continue;
            }
            if (packet.kind == NetworkPacketKind::pong) {
                if (now_ms >= packet.timestamp_ms) {
                    telemetry_.record_rtt(
                        static_cast<double>(now_ms - packet.timestamp_ms));
                }
                continue;
            }
            if (packet.kind == NetworkPacketKind::session_hello ||
                packet.kind == NetworkPacketKind::session_accept) {
                continue;
            }
            delivered.push_back(packet);
        }

        if (state_ == DirectSessionState::connected && remote_) {
            if (has_peer_receive_time_ &&
                interval_elapsed(now_ms, last_peer_receive_ms_, timing_.liveness_timeout_ms)) {
                const auto reconnect = begin_reconnect(now_ms);
                if (!reconnect) {
                    return Result<std::vector<NetworkPacket>>::failure(
                        reconnect.error, reconnect.detail);
                }
            } else if (interval_elapsed(
                           now_ms, last_heartbeat_send_ms_, timing_.heartbeat_interval_ms)) {
                const auto heartbeat = send_heartbeat(now_ms);
                if (!heartbeat) {
                    return Result<std::vector<NetworkPacket>>::failure(
                        heartbeat.error, heartbeat.detail);
                }
            }
        }

        if (state_ == DirectSessionState::reconnecting &&
            interval_elapsed(now_ms, reconnect_started_ms_, timing_.reconnect_timeout_ms)) {
            mark_disconnected();
        }

        return Result<std::vector<NetworkPacket>>::success(std::move(delivered));
    }

    [[nodiscard]] Result<void> send(const NetworkPacket& packet,
                                    std::uint64_t /*now_ms*/) {
        if (state_ != DirectSessionState::connected || !remote_) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "direct-session gameplay send requires connection");
        }
        if (is_reliable_control(packet.kind)) {
            return Result<void>::failure(
                ErrorCode::invalid_argument,
                "direct-session control packets are owned by the session state machine");
        }
        const auto sent = transport_.send_packet(*remote_, packet);
        if (!sent) return sent;
        telemetry_.record_packet_sent();
        return Result<void>::success();
    }

    [[nodiscard]] Result<void> disconnect(std::uint64_t now_ms) {
        if (state_ != DirectSessionState::connected || !remote_) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "direct-session disconnect requires connection");
        }
        NetworkPacket packet{};
        packet.kind = NetworkPacketKind::disconnect;
        packet.sequence = next_sequence_++;
        packet.timestamp_ms = now_ms;
        const auto sent = transport_.send_packet(*remote_, packet);
        if (!sent) return sent;
        telemetry_.record_packet_sent();
        mark_disconnected();
        return Result<void>::success();
    }

private:
    [[nodiscard]] static bool interval_elapsed(
        std::uint64_t now_ms,
        std::uint64_t then_ms,
        std::uint64_t interval_ms) noexcept {
        return now_ms >= then_ms && now_ms - then_ms >= interval_ms;
    }

    void mark_connected(std::uint64_t now_ms) noexcept {
        state_ = DirectSessionState::connected;
        telemetry_.state = NetworkConnectionState::connected;
        last_peer_receive_ms_ = now_ms;
        last_heartbeat_send_ms_ = now_ms;
        reconnect_started_ms_ = 0u;
        has_peer_receive_time_ = true;
    }

    void refresh_peer_liveness(std::uint64_t now_ms) noexcept {
        last_peer_receive_ms_ = now_ms;
        has_peer_receive_time_ = true;
    }

    void mark_disconnected() noexcept {
        state_ = DirectSessionState::disconnected;
        telemetry_.state = NetworkConnectionState::disconnected;
        reconnect_started_ms_ = 0u;
    }

    [[nodiscard]] Result<void> send_new_hello(std::uint64_t now_ms) {
        NetworkPacket hello{};
        hello.kind = NetworkPacketKind::session_hello;
        hello.sequence = next_sequence_++;
        hello.timestamp_ms = now_ms;
        hello_sequence_ = hello.sequence;
        const auto tracked = reliability_->track(hello, now_ms);
        if (!tracked) return tracked;
        return send_control_packet(hello);
    }

    [[nodiscard]] Result<void> begin_reconnect(std::uint64_t now_ms) {
        if (!remote_) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "direct-session reconnect requires pinned peer");
        }
        state_ = DirectSessionState::reconnecting;
        telemetry_.state = NetworkConnectionState::reconnecting;
        telemetry_.record_packet_lost();
        reconnect_started_ms_ = now_ms;
        last_heartbeat_send_ms_ = now_ms;

        if (role_ == DirectSessionRole::client) {
            return send_new_hello(now_ms);
        }
        return Result<void>::success();
    }

    [[nodiscard]] Result<void> send_heartbeat(std::uint64_t now_ms) {
        NetworkPacket ping{};
        ping.kind = NetworkPacketKind::ping;
        ping.sequence = next_sequence_++;
        ping.timestamp_ms = now_ms;
        const auto sent = send_control_packet(ping);
        if (!sent) return sent;
        last_heartbeat_send_ms_ = now_ms;
        return Result<void>::success();
    }

    [[nodiscard]] Result<void> send_control_packet(const NetworkPacket& packet) {
        if (!remote_) {
            return Result<void>::failure(ErrorCode::invalid_argument,
                                         "direct-session peer is not selected");
        }
        const auto sent = transport_.send_packet(*remote_, packet);
        if (!sent) return sent;
        telemetry_.record_packet_sent();
        return Result<void>::success();
    }

    DirectSessionRole role_{DirectSessionRole::host};
    DirectSessionState state_{DirectSessionState::idle};
    DirectSessionTiming timing_{};
    UdpNetworkTransport transport_{};
    std::optional<NetworkEndpoint> remote_{};
    std::optional<ControlReliabilityQueue> reliability_{};
    NetworkTelemetry telemetry_{};
    std::uint32_t next_sequence_{1u};
    std::uint32_t hello_sequence_{};
    std::uint64_t last_peer_receive_ms_{};
    std::uint64_t last_heartbeat_send_ms_{};
    std::uint64_t reconnect_started_ms_{};
    bool has_peer_receive_time_{};
};

} // namespace jojo
