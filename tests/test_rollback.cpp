#include "core/rollback.h"
#include "core/network_protocol.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

namespace {

jojo::RollbackInput input(std::uint32_t buttons, std::int16_t x = 0, std::int16_t y = 0) {
    return jojo::RollbackInput{buttons, x, y};
}

std::vector<std::uint8_t> encode_i64(std::int64_t value) {
    std::vector<std::uint8_t> bytes(8);
    const auto raw = static_cast<std::uint64_t>(value);
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes[shift / 8] = static_cast<std::uint8_t>((raw >> shift) & 0xffu);
    }
    return bytes;
}

std::int64_t decode_i64(std::span<const std::uint8_t> bytes) {
    std::uint64_t raw = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        raw |= static_cast<std::uint64_t>(bytes[shift / 8]) << shift;
    }
    return static_cast<std::int64_t>(raw);
}

class TestSimulation final : public jojo::IRollbackSimulation {
public:
    std::vector<std::uint8_t> save_state() const override {
        return encode_i64(value);
    }

    jojo::Result<void> load_state(std::span<const std::uint8_t> state) override {
        if (state.size() != 8) {
            return jojo::Result<void>::failure(jojo::ErrorCode::invalid_argument, "test state must contain eight bytes");
        }
        if (fail_next_load) {
            fail_next_load = false;
            value = -777777;
            return jojo::Result<void>::failure(jojo::ErrorCode::io_error, "injected restore failure after mutation");
        }
        value = decode_i64(state);
        return jojo::Result<void>::success();
    }

    jojo::Result<void> step_frame(
        jojo::RollbackInput local,
        jojo::RollbackInput remote,
        bool emit_side_effects) override {
        value += static_cast<std::int64_t>(local.buttons);
        value += static_cast<std::int64_t>(remote.buttons) * 10;
        value += local.axis_x;
        value += remote.axis_y;
        side_effect_flags.push_back(emit_side_effects);
        if (emit_side_effects) ++emitted_side_effects;
        return jojo::Result<void>::success();
    }

    std::int64_t value{};
    int emitted_side_effects{};
    std::vector<bool> side_effect_flags;
    bool fail_next_load{};
};

void test_rng_is_repeatable_and_state_restorable() {
    jojo::DeterministicRng a(0x123456789abcdef0ull);
    jojo::DeterministicRng b(0x123456789abcdef0ull);
    for (int i = 0; i < 8; ++i) CHECK(a.next_u32() == b.next_u32());

    jojo::DeterministicRng source(99);
    (void)source.next_u32();
    (void)source.next_u32();
    const auto saved = source.state();
    const auto expected_1 = source.next_u32();
    const auto expected_2 = source.next_u32();

    jojo::DeterministicRng restored(1);
    restored.set_state(saved);
    CHECK(restored.next_u32() == expected_1);
    CHECK(restored.next_u32() == expected_2);
}

void test_prediction_and_late_input_rollback_match_authoritative_run() {
    TestSimulation predicted{};
    jojo::RollbackSession session(predicted, 8);

    CHECK(session.current_frame() == 0);
    CHECK(session.advance(input(1)));
    CHECK(session.current_frame() == 1);
    CHECK(predicted.value == 1);
    CHECK(session.telemetry().predicted_frames == 1);
    CHECK(predicted.emitted_side_effects == 1);

    CHECK(session.submit_remote_input(0, input(2)));
    CHECK(predicted.value == 21);
    CHECK(session.telemetry().last_rollback_depth == 1);
    CHECK(session.telemetry().max_rollback_depth == 1);
    CHECK(predicted.emitted_side_effects == 1);
    CHECK(predicted.side_effect_flags.size() == 2);
    if (predicted.side_effect_flags.size() == 2) {
        CHECK(predicted.side_effect_flags[0]);
        CHECK(!predicted.side_effect_flags[1]);
    }

    TestSimulation authoritative{};
    jojo::RollbackSession exact(authoritative, 8);
    CHECK(exact.submit_remote_input(0, input(2)));
    CHECK(exact.advance(input(1)));
    CHECK(authoritative.value == predicted.value);

    const auto predicted_hash = session.state_hash(0);
    const auto exact_hash = exact.state_hash(0);
    CHECK(predicted_hash && exact_hash);
    if (predicted_hash && exact_hash) CHECK(predicted_hash.value == exact_hash.value);
}

void test_multiple_frame_rollback_replays_without_side_effects() {
    TestSimulation simulation{};
    jojo::RollbackSession session(simulation, 6);
    CHECK(session.advance(input(1)));
    CHECK(session.advance(input(2)));
    CHECK(session.advance(input(3)));
    CHECK(simulation.emitted_side_effects == 3);

    CHECK(session.submit_remote_input(1, input(4)));
    CHECK(session.current_frame() == 3);
    CHECK(session.telemetry().last_rollback_depth == 2);
    CHECK(simulation.emitted_side_effects == 3);
    CHECK(simulation.side_effect_flags.size() == 5);
    if (simulation.side_effect_flags.size() == 5) {
        CHECK(!simulation.side_effect_flags[3]);
        CHECK(!simulation.side_effect_flags[4]);
    }
}

void test_too_old_correction_is_rejected_without_mutation() {
    TestSimulation simulation{};
    jojo::RollbackSession session(simulation, 2);
    CHECK(session.advance(input(1)));
    CHECK(session.advance(input(2)));
    CHECK(session.advance(input(3)));
    const auto before = simulation.value;
    const auto before_frame = session.current_frame();

    const auto correction = session.submit_remote_input(0, input(9));
    CHECK(!correction);
    CHECK(correction.error == jojo::ErrorCode::invalid_argument);
    CHECK(simulation.value == before);
    CHECK(session.current_frame() == before_frame);
}

void test_failed_snapshot_restore_is_transactional() {
    TestSimulation simulation{};
    jojo::RollbackSession session(simulation, 8);
    CHECK(session.advance(input(1)));
    CHECK(session.advance(input(2)));
    const auto before = simulation.value;
    const auto before_frame = session.current_frame();
    const auto before_hash = session.state_hash(1);
    CHECK(before_hash);

    simulation.fail_next_load = true;
    const auto correction = session.submit_remote_input(0, input(9));
    CHECK(!correction);
    CHECK(correction.error == jojo::ErrorCode::io_error);
    CHECK(simulation.value == before);
    CHECK(session.current_frame() == before_frame);
    const auto after_hash = session.state_hash(1);
    CHECK(after_hash);
    if (before_hash && after_hash) CHECK(before_hash.value == after_hash.value);
}

void test_state_hashes_detect_desync() {
    TestSimulation simulation{};
    jojo::RollbackSession session(simulation, 4);
    CHECK(session.advance(input(7)));
    const auto local = session.state_hash(0);
    CHECK(local);
    if (!local) return;

    CHECK(session.submit_remote_hash(0, local.value));
    CHECK(!session.desync_frame().has_value());
    CHECK(session.submit_remote_hash(0, std::string(64, '0')));
    CHECK(session.desync_frame().has_value());
    if (session.desync_frame()) CHECK(*session.desync_frame() == 0);
    CHECK(!session.state_hash(99));
}

jojo::NetworkPacket packet_for(jojo::NetworkPacketKind kind, std::uint32_t sequence) {
    jojo::NetworkPacket packet{};
    packet.kind = kind;
    packet.sequence = sequence;
    packet.ack = 17;
    packet.frame = 0x0102030405060708ull;
    packet.timestamp_ms = 123456789ull;
    packet.input = input(0x1234u, -123, 456);
    packet.payload = {0x41, 0x42, 0x43, 0x00, 0xff};
    return packet;
}

void test_packet_serialization_is_deterministic_and_round_trips() {
    const std::vector<jojo::NetworkPacketKind> kinds{
        jojo::NetworkPacketKind::input,
        jojo::NetworkPacketKind::ping,
        jojo::NetworkPacketKind::pong,
        jojo::NetworkPacketKind::session_hello,
        jojo::NetworkPacketKind::session_accept,
        jojo::NetworkPacketKind::disconnect,
    };

    std::uint32_t sequence = 1;
    for (const auto kind : kinds) {
        const auto packet = packet_for(kind, sequence++);
        const auto a = jojo::serialize_network_packet(packet);
        const auto b = jojo::serialize_network_packet(packet);
        CHECK(a && b);
        if (!a || !b) continue;
        CHECK(a.value == b.value);
        const auto parsed = jojo::parse_network_packet(a.value);
        CHECK(parsed);
        if (parsed) CHECK(parsed.value == packet);
    }
}

void test_packet_validation_rejects_malformed_data() {
    const auto packet = packet_for(jojo::NetworkPacketKind::input, 3);
    const auto encoded = jojo::serialize_network_packet(packet);
    CHECK(encoded);
    if (!encoded) return;

    auto bad_magic = encoded.value;
    bad_magic[0] ^= 0xff;
    CHECK(!jojo::parse_network_packet(bad_magic));

    auto bad_version = encoded.value;
    bad_version[4] = 99;
    CHECK(!jojo::parse_network_packet(bad_version));

    auto bad_kind = encoded.value;
    bad_kind[5] = 99;
    CHECK(!jojo::parse_network_packet(bad_kind));

    auto bad_reserved = encoded.value;
    bad_reserved[6] = 1;
    CHECK(!jojo::parse_network_packet(bad_reserved));

    auto truncated = encoded.value;
    truncated.pop_back();
    CHECK(!jojo::parse_network_packet(truncated));

    auto oversized = packet;
    oversized.payload.assign(1025, 0x5a);
    CHECK(!jojo::serialize_network_packet(oversized));
}

void test_reliability_is_control_only_and_acknowledged() {
    CHECK(!jojo::is_reliable_control(jojo::NetworkPacketKind::input));
    CHECK(!jojo::is_reliable_control(jojo::NetworkPacketKind::ping));
    CHECK(!jojo::is_reliable_control(jojo::NetworkPacketKind::pong));
    CHECK(jojo::is_reliable_control(jojo::NetworkPacketKind::session_hello));
    CHECK(jojo::is_reliable_control(jojo::NetworkPacketKind::session_accept));
    CHECK(jojo::is_reliable_control(jojo::NetworkPacketKind::disconnect));

    jojo::ControlReliabilityQueue queue(50);
    CHECK(!queue.track(packet_for(jojo::NetworkPacketKind::input, 1), 100));
    CHECK(queue.track(packet_for(jojo::NetworkPacketKind::session_hello, 42), 100));
    CHECK(queue.pending_count() == 1);
    CHECK(!queue.track(packet_for(jojo::NetworkPacketKind::session_accept, 42), 101));
    CHECK(queue.due_retransmits(149).empty());
    const auto first = queue.due_retransmits(150);
    CHECK(first.size() == 1);
    if (first.size() == 1) CHECK(first[0].sequence == 42);
    CHECK(queue.due_retransmits(199).empty());
    CHECK(queue.due_retransmits(200).size() == 1);
    queue.acknowledge(42);
    CHECK(queue.pending_count() == 0);
    CHECK(queue.due_retransmits(1000).empty());
}

void test_network_telemetry_tracks_all_required_metrics() {
    jojo::NetworkTelemetry telemetry{};
    CHECK(telemetry.state == jojo::NetworkConnectionState::connected);
    telemetry.record_rtt(40.0);
    CHECK(std::abs(telemetry.rtt_ms - 40.0) < 0.0001);
    CHECK(std::abs(telemetry.jitter_ms) < 0.0001);
    telemetry.record_rtt(60.0);
    CHECK(std::abs(telemetry.rtt_ms - 60.0) < 0.0001);
    CHECK(std::abs(telemetry.jitter_ms - 5.0) < 0.0001);
    telemetry.record_rtt(50.0);
    CHECK(std::abs(telemetry.jitter_ms - 6.25) < 0.0001);
    telemetry.record_rtt(-1.0);
    CHECK(std::abs(telemetry.rtt_ms - 50.0) < 0.0001);

    for (int i = 0; i < 10; ++i) telemetry.record_packet_sent();
    for (int i = 0; i < 8; ++i) telemetry.record_packet_received();
    for (int i = 0; i < 2; ++i) telemetry.record_packet_lost();
    CHECK(telemetry.packets_sent == 10);
    CHECK(telemetry.packets_received == 8);
    CHECK(telemetry.packets_lost == 2);
    CHECK(std::abs(telemetry.packet_loss_percent() - 20.0) < 0.0001);

    telemetry.record_prediction();
    telemetry.record_prediction();
    CHECK(telemetry.predicted_frames == 2);
    telemetry.record_rollback(3);
    telemetry.record_rollback(1);
    CHECK(telemetry.last_rollback_depth == 1);
    CHECK(telemetry.max_rollback_depth == 3);
    telemetry.state = jojo::NetworkConnectionState::reconnecting;
    CHECK(telemetry.state == jojo::NetworkConnectionState::reconnecting);
    telemetry.state = jojo::NetworkConnectionState::disconnected;
    CHECK(telemetry.state == jojo::NetworkConnectionState::disconnected);
}

}

int main() {
    test_rng_is_repeatable_and_state_restorable();
    test_prediction_and_late_input_rollback_match_authoritative_run();
    test_multiple_frame_rollback_replays_without_side_effects();
    test_too_old_correction_is_rejected_without_mutation();
    test_failed_snapshot_restore_is_transactional();
    test_state_hashes_detect_desync();
    test_packet_serialization_is_deterministic_and_round_trips();
    test_packet_validation_rejects_malformed_data();
    test_reliability_is_control_only_and_acknowledged();
    test_network_telemetry_tracks_all_required_metrics();

    if (failures) {
        std::cerr << failures << " rollback/network assertion(s) failed\n";
        return 1;
    }
    std::cout << "rollback/network assertions passed\n";
    return 0;
}
