#include "core/network_protocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace jojo {
namespace {

constexpr std::array<std::uint8_t, 4> packet_magic{'J', 'R', 'B', 'K'};
constexpr std::uint8_t protocol_version = 1;
constexpr std::size_t fixed_packet_size = 42;
constexpr std::size_t max_payload_size = 1024;

bool valid_kind(NetworkPacketKind kind) noexcept {
    switch (kind) {
        case NetworkPacketKind::input:
        case NetworkPacketKind::ping:
        case NetworkPacketKind::pong:
        case NetworkPacketKind::session_hello:
        case NetworkPacketKind::session_accept:
        case NetworkPacketKind::disconnect:
            return true;
    }
    return false;
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

Result<std::uint16_t> read_u16(std::span<const std::uint8_t> bytes, std::size_t& offset) {
    if (offset + 2 > bytes.size()) {
        return Result<std::uint16_t>::failure(ErrorCode::unsupported_format, "network packet is truncated while reading u16");
    }
    const auto value = static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    offset += 2;
    return Result<std::uint16_t>::success(value);
}

Result<std::uint32_t> read_u32(std::span<const std::uint8_t> bytes, std::size_t& offset) {
    if (offset + 4 > bytes.size()) {
        return Result<std::uint32_t>::failure(ErrorCode::unsupported_format, "network packet is truncated while reading u32");
    }
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
    }
    return Result<std::uint32_t>::success(value);
}

Result<std::uint64_t> read_u64(std::span<const std::uint8_t> bytes, std::size_t& offset) {
    if (offset + 8 > bytes.size()) {
        return Result<std::uint64_t>::failure(ErrorCode::unsupported_format, "network packet is truncated while reading u64");
    }
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
    }
    return Result<std::uint64_t>::success(value);
}

}

bool is_reliable_control(NetworkPacketKind kind) noexcept {
    return kind == NetworkPacketKind::session_hello ||
        kind == NetworkPacketKind::session_accept ||
        kind == NetworkPacketKind::disconnect;
}

Result<std::vector<std::uint8_t>> serialize_network_packet(const NetworkPacket& packet) {
    if (!valid_kind(packet.kind)) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::invalid_argument, "network packet kind is invalid");
    }
    if (packet.payload.size() > max_payload_size || packet.payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::invalid_argument, "network packet payload exceeds 1024 bytes");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(fixed_packet_size + packet.payload.size());
    bytes.insert(bytes.end(), packet_magic.begin(), packet_magic.end());
    bytes.push_back(protocol_version);
    bytes.push_back(static_cast<std::uint8_t>(packet.kind));
    append_u16(bytes, 0);
    append_u32(bytes, packet.sequence);
    append_u32(bytes, packet.ack);
    append_u64(bytes, packet.frame);
    append_u64(bytes, packet.timestamp_ms);
    append_u32(bytes, packet.input.buttons);
    append_u16(bytes, static_cast<std::uint16_t>(packet.input.axis_x));
    append_u16(bytes, static_cast<std::uint16_t>(packet.input.axis_y));
    append_u16(bytes, static_cast<std::uint16_t>(packet.payload.size()));
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<NetworkPacket> parse_network_packet(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < fixed_packet_size) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet is shorter than fixed header");
    }
    if (!std::equal(packet_magic.begin(), packet_magic.end(), bytes.begin())) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet magic is invalid");
    }
    if (bytes[4] != protocol_version) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet protocol version is unsupported");
    }

    const auto raw_kind = bytes[5];
    if (raw_kind > static_cast<std::uint8_t>(NetworkPacketKind::disconnect)) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet kind is invalid");
    }
    const auto kind = static_cast<NetworkPacketKind>(raw_kind);
    if (!valid_kind(kind)) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet kind is invalid");
    }

    std::size_t offset = 6;
    const auto reserved = read_u16(bytes, offset);
    if (!reserved) return Result<NetworkPacket>::failure(reserved.error, reserved.detail);
    if (reserved.value != 0) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet reserved bits are non-zero");
    }

    const auto sequence = read_u32(bytes, offset);
    const auto ack = read_u32(bytes, offset);
    const auto frame = read_u64(bytes, offset);
    const auto timestamp = read_u64(bytes, offset);
    const auto buttons = read_u32(bytes, offset);
    const auto axis_x = read_u16(bytes, offset);
    const auto axis_y = read_u16(bytes, offset);
    const auto payload_size = read_u16(bytes, offset);
    if (!sequence || !ack || !frame || !timestamp || !buttons || !axis_x || !axis_y || !payload_size) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet contains a truncated field");
    }
    if (payload_size.value > max_payload_size) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet payload exceeds 1024 bytes");
    }
    if (offset + payload_size.value != bytes.size()) {
        return Result<NetworkPacket>::failure(ErrorCode::unsupported_format, "network packet length does not match payload size");
    }

    NetworkPacket packet{};
    packet.kind = kind;
    packet.sequence = sequence.value;
    packet.ack = ack.value;
    packet.frame = frame.value;
    packet.timestamp_ms = timestamp.value;
    packet.input.buttons = buttons.value;
    packet.input.axis_x = static_cast<std::int16_t>(axis_x.value);
    packet.input.axis_y = static_cast<std::int16_t>(axis_y.value);
    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    return Result<NetworkPacket>::success(std::move(packet));
}

Result<void> ControlReliabilityQueue::track(NetworkPacket packet, std::uint64_t now_ms) {
    if (!is_reliable_control(packet.kind)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "only session/control packets may enter reliable retransmission queue");
    }
    if (pending_.find(packet.sequence) != pending_.end()) {
        return Result<void>::failure(ErrorCode::invalid_argument, "reliable packet sequence is already pending");
    }
    pending_.emplace(packet.sequence, PendingPacket{std::move(packet), now_ms});
    return Result<void>::success();
}

void ControlReliabilityQueue::acknowledge(std::uint32_t sequence) noexcept {
    pending_.erase(sequence);
}

std::vector<NetworkPacket> ControlReliabilityQueue::due_retransmits(std::uint64_t now_ms) {
    std::vector<NetworkPacket> due;
    for (auto& [sequence, pending] : pending_) {
        (void)sequence;
        if (now_ms < pending.last_send_ms) continue;
        if (now_ms - pending.last_send_ms < retry_interval_ms_) continue;
        due.push_back(pending.packet);
        pending.last_send_ms = now_ms;
    }
    return due;
}

void NetworkTelemetry::record_rtt(double sample_ms) noexcept {
    if (sample_ms < 0.0 || !std::isfinite(sample_ms)) return;
    if (!has_rtt_sample_) {
        has_rtt_sample_ = true;
        previous_rtt_ms_ = sample_ms;
        rtt_ms = sample_ms;
        jitter_ms = 0.0;
        return;
    }
    const auto delta = std::abs(sample_ms - previous_rtt_ms_);
    jitter_ms = 0.75 * jitter_ms + 0.25 * delta;
    previous_rtt_ms_ = sample_ms;
    rtt_ms = sample_ms;
}

void NetworkTelemetry::record_rollback(std::uint32_t depth) noexcept {
    last_rollback_depth = depth;
    max_rollback_depth = std::max(max_rollback_depth, depth);
}

double NetworkTelemetry::packet_loss_percent() const noexcept {
    const auto denominator = packets_received + packets_lost;
    if (denominator == 0) return 0.0;
    return static_cast<double>(packets_lost) * 100.0 / static_cast<double>(denominator);
}

}
