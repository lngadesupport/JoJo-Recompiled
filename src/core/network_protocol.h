#pragma once

#include "core/result.h"
#include "core/rollback.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace jojo {

enum class NetworkPacketKind : std::uint8_t {
    input,
    ping,
    pong,
    session_hello,
    session_accept,
    disconnect
};

struct NetworkPacket {
    NetworkPacketKind kind{NetworkPacketKind::input};
    std::uint32_t sequence{};
    std::uint32_t ack{};
    std::uint64_t frame{};
    std::uint64_t timestamp_ms{};
    RollbackInput input{};
    std::vector<std::uint8_t> payload;
    friend bool operator==(const NetworkPacket&, const NetworkPacket&) = default;
};

[[nodiscard]] bool is_reliable_control(NetworkPacketKind kind) noexcept;
[[nodiscard]] Result<std::vector<std::uint8_t>> serialize_network_packet(const NetworkPacket& packet);
[[nodiscard]] Result<NetworkPacket> parse_network_packet(std::span<const std::uint8_t> bytes);

class ControlReliabilityQueue {
public:
    explicit ControlReliabilityQueue(std::uint64_t retry_interval_ms) noexcept
        : retry_interval_ms_(retry_interval_ms) {}

    [[nodiscard]] Result<void> track(NetworkPacket packet, std::uint64_t now_ms);
    void acknowledge(std::uint32_t sequence) noexcept;
    [[nodiscard]] std::vector<NetworkPacket> due_retransmits(std::uint64_t now_ms);
    [[nodiscard]] std::size_t pending_count() const noexcept { return pending_.size(); }

private:
    struct PendingPacket {
        NetworkPacket packet;
        std::uint64_t last_send_ms{};
    };

    std::uint64_t retry_interval_ms_{};
    std::map<std::uint32_t, PendingPacket> pending_;
};

enum class NetworkConnectionState {
    connected,
    reconnecting,
    disconnected
};

struct NetworkTelemetry {
    double rtt_ms{};
    double jitter_ms{};
    std::uint64_t packets_sent{};
    std::uint64_t packets_received{};
    std::uint64_t packets_lost{};
    std::uint64_t predicted_frames{};
    std::uint32_t last_rollback_depth{};
    std::uint32_t max_rollback_depth{};
    NetworkConnectionState state{NetworkConnectionState::connected};

    void record_rtt(double sample_ms) noexcept;
    void record_packet_sent() noexcept { ++packets_sent; }
    void record_packet_received() noexcept { ++packets_received; }
    void record_packet_lost() noexcept { ++packets_lost; }
    void record_prediction() noexcept { ++predicted_frames; }
    void record_rollback(std::uint32_t depth) noexcept;
    [[nodiscard]] double packet_loss_percent() const noexcept;

private:
    bool has_rtt_sample_{};
    double previous_rtt_ms_{};
};

}
