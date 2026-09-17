#include "core/online_session.h"

#include <array>
#include <cctype>
#include <utility>

namespace jojo {
namespace {

Result<unsigned> parse_decimal(std::string_view text, unsigned maximum,
                               const char* detail) {
    if (text.empty()) {
        return Result<unsigned>::failure(ErrorCode::invalid_argument, detail);
    }

    unsigned value = 0u;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return Result<unsigned>::failure(ErrorCode::invalid_argument, detail);
        }
        const unsigned digit = static_cast<unsigned>(ch - '0');
        if (value > (maximum - digit) / 10u) {
            return Result<unsigned>::failure(ErrorCode::invalid_argument, detail);
        }
        value = value * 10u + digit;
    }
    return Result<unsigned>::success(value);
}

} // namespace

Result<NetworkEndpoint> parse_direct_endpoint(std::string_view text) {
    for (const unsigned char ch : text) {
        if (std::isspace(ch) != 0) {
            return Result<NetworkEndpoint>::failure(
                ErrorCode::invalid_argument,
                "direct endpoint must use A.B.C.D:PORT");
        }
    }

    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0u ||
        colon + 1u >= text.size() ||
        text.find(':', colon + 1u) != std::string_view::npos) {
        return Result<NetworkEndpoint>::failure(
            ErrorCode::invalid_argument,
            "direct endpoint must use A.B.C.D:PORT");
    }

    std::array<std::uint8_t, 4> octets{};
    const std::string_view address = text.substr(0u, colon);
    std::size_t begin = 0u;
    for (std::size_t index = 0u; index < octets.size(); ++index) {
        const auto dot = address.find('.', begin);
        const bool last = index + 1u == octets.size();
        if ((last && dot != std::string_view::npos) ||
            (!last && dot == std::string_view::npos)) {
            return Result<NetworkEndpoint>::failure(
                ErrorCode::invalid_argument,
                "direct endpoint must use A.B.C.D:PORT");
        }

        const auto end = last ? address.size() : dot;
        const auto value = parse_decimal(
            address.substr(begin, end - begin), 255u,
            "direct endpoint IPv4 octet is invalid");
        if (!value) {
            return Result<NetworkEndpoint>::failure(value.error, value.detail);
        }
        octets[index] = static_cast<std::uint8_t>(value.value);
        begin = end + 1u;
    }

    const auto port = parse_decimal(
        text.substr(colon + 1u), 65535u,
        "direct endpoint port is invalid");
    if (!port || port.value == 0u) {
        return Result<NetworkEndpoint>::failure(
            ErrorCode::invalid_argument,
            "direct endpoint port is invalid");
    }

    return Result<NetworkEndpoint>::success(
        NetworkEndpoint{octets, static_cast<std::uint16_t>(port.value)});
}

std::string format_direct_endpoint(NetworkEndpoint endpoint) {
    return std::to_string(endpoint.ipv4[0]) + "." +
           std::to_string(endpoint.ipv4[1]) + "." +
           std::to_string(endpoint.ipv4[2]) + "." +
           std::to_string(endpoint.ipv4[3]) + ":" +
           std::to_string(endpoint.port);
}

Result<void> OnlineSessionController::host(NetworkEndpoint local,
                                           DirectSessionTiming timing) {
    if (view_.state != OnlineSessionState::inactive) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "online session is already active");
    }

    auto bound = DirectUdpSession::bind(DirectSessionRole::host, local, timing);
    if (!bound) {
        set_fault(bound.error, bound.detail);
        return Result<void>::failure(bound.error, bound.detail);
    }

    session_.emplace(std::move(bound.value));
    view_.role = DirectSessionRole::host;
    refresh_view();
    return Result<void>::success();
}

Result<void> OnlineSessionController::join(NetworkEndpoint local,
                                           NetworkEndpoint remote,
                                           DirectSessionTiming timing,
                                           std::uint64_t now_ms) {
    if (view_.state != OnlineSessionState::inactive) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "online session is already active");
    }
    if (remote.port == 0u) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "online join remote port must be non-zero");
    }

    auto bound = DirectUdpSession::bind(DirectSessionRole::client, local, timing);
    if (!bound) {
        set_fault(bound.error, bound.detail);
        return Result<void>::failure(bound.error, bound.detail);
    }

    session_.emplace(std::move(bound.value));
    view_.role = DirectSessionRole::client;
    const auto connected = session_->connect(remote, now_ms);
    if (!connected) {
        set_fault(connected.error, connected.detail);
        return connected;
    }

    refresh_view();
    return Result<void>::success();
}

Result<std::vector<NetworkPacket>> OnlineSessionController::poll(
    std::uint64_t now_ms) {
    if (!session_) {
        return Result<std::vector<NetworkPacket>>::failure(
            ErrorCode::invalid_argument,
            "online poll requires active session");
    }

    auto result = session_->poll(now_ms);
    if (!result) {
        set_fault(result.error, result.detail);
        return Result<std::vector<NetworkPacket>>::failure(
            result.error, result.detail);
    }

    refresh_view();
    return Result<std::vector<NetworkPacket>>::success(std::move(result.value));
}

Result<void> OnlineSessionController::send(const NetworkPacket& packet,
                                           std::uint64_t now_ms) {
    if (view_.state != OnlineSessionState::connected || !session_) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "online gameplay send requires connection");
    }

    const auto result = session_->send(packet, now_ms);
    if (!result) {
        if (result.error == ErrorCode::invalid_argument) return result;
        set_fault(result.error, result.detail);
        return result;
    }

    refresh_view();
    return Result<void>::success();
}

Result<void> OnlineSessionController::disconnect(std::uint64_t now_ms) {
    if (view_.state != OnlineSessionState::connected || !session_) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "online disconnect requires connection");
    }

    const auto result = session_->disconnect(now_ms);
    if (!result) {
        if (result.error == ErrorCode::invalid_argument) return result;
        set_fault(result.error, result.detail);
        return result;
    }

    refresh_view();
    return Result<void>::success();
}

void OnlineSessionController::reset() noexcept {
    session_.reset();
    view_ = OnlineSessionViewState{};
}

void OnlineSessionController::refresh_view() noexcept {
    if (!session_) return;

    switch (session_->state()) {
    case DirectSessionState::idle:
        view_.state = view_.role == DirectSessionRole::host
            ? OnlineSessionState::waiting_for_peer
            : OnlineSessionState::inactive;
        break;
    case DirectSessionState::connecting:
        view_.state = OnlineSessionState::connecting;
        break;
    case DirectSessionState::connected:
        view_.state = OnlineSessionState::connected;
        break;
    case DirectSessionState::reconnecting:
        view_.state = OnlineSessionState::reconnecting;
        break;
    case DirectSessionState::disconnected:
        view_.state = OnlineSessionState::disconnected;
        break;
    }

    view_.local_endpoint = session_->local_endpoint();
    view_.remote_endpoint = session_->remote_endpoint();
    const auto& telemetry = session_->telemetry();
    view_.rtt_ms = telemetry.rtt_ms;
    view_.jitter_ms = telemetry.jitter_ms;
    view_.packet_loss_percent = telemetry.packet_loss_percent();
    view_.packets_sent = telemetry.packets_sent;
    view_.packets_received = telemetry.packets_received;
    view_.packets_lost = telemetry.packets_lost;
    view_.can_send_gameplay = view_.state == OnlineSessionState::connected;
}

void OnlineSessionController::set_fault(ErrorCode error, std::string detail) {
    view_.state = OnlineSessionState::faulted;
    view_.can_send_gameplay = false;
    view_.last_error = error;
    view_.last_error_detail = std::move(detail);
}

} // namespace jojo
