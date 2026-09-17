#pragma once

#include "core/network_transport.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

enum class OnlineSessionState {
    inactive,
    waiting_for_peer,
    connecting,
    connected,
    reconnecting,
    disconnected,
    faulted,
};

struct OnlineSessionViewState {
    OnlineSessionState state{OnlineSessionState::inactive};
    std::optional<DirectSessionRole> role{};
    std::optional<NetworkEndpoint> local_endpoint{};
    std::optional<NetworkEndpoint> remote_endpoint{};
    double rtt_ms{};
    double jitter_ms{};
    double packet_loss_percent{};
    std::uint64_t packets_sent{};
    std::uint64_t packets_received{};
    std::uint64_t packets_lost{};
    bool can_send_gameplay{};
    ErrorCode last_error{ErrorCode::none};
    std::string last_error_detail{};
};

[[nodiscard]] Result<NetworkEndpoint> parse_direct_endpoint(std::string_view text);
[[nodiscard]] std::string format_direct_endpoint(NetworkEndpoint endpoint);

class OnlineSessionController {
public:
    [[nodiscard]] const OnlineSessionViewState& view() const noexcept { return view_; }

    [[nodiscard]] Result<void> host(
        NetworkEndpoint local,
        DirectSessionTiming timing = {});

    [[nodiscard]] Result<void> join(
        NetworkEndpoint local,
        NetworkEndpoint remote,
        DirectSessionTiming timing,
        std::uint64_t now_ms);

    [[nodiscard]] Result<std::vector<NetworkPacket>> poll(std::uint64_t now_ms);
    [[nodiscard]] Result<void> send(const NetworkPacket& packet,
                                    std::uint64_t now_ms);
    [[nodiscard]] Result<void> disconnect(std::uint64_t now_ms);
    void reset() noexcept;

private:
    void refresh_view() noexcept;
    void set_fault(ErrorCode error, std::string detail);

    std::optional<DirectUdpSession> session_{};
    OnlineSessionViewState view_{};
};

} // namespace jojo
