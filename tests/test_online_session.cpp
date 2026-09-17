#include "core/online_session.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

jojo::DirectSessionTiming fast_timing() {
    jojo::DirectSessionTiming timing{};
    timing.retry_interval_ms = 5;
    timing.heartbeat_interval_ms = 10;
    timing.liveness_timeout_ms = 30;
    timing.reconnect_timeout_ms = 80;
    return timing;
}

bool drive_connected(jojo::OnlineSessionController& host,
                     jojo::OnlineSessionController& client) {
    for (std::uint64_t now = 1; now <= 20; ++now) {
        const auto host_poll = host.poll(now);
        const auto client_poll = client.poll(now);
        if (!host_poll || !client_poll) return false;
        if (host.view().state == jojo::OnlineSessionState::connected &&
            client.view().state == jojo::OnlineSessionState::connected) {
            return true;
        }
    }
    return false;
}

bool start_connected_pair(jojo::OnlineSessionController& host,
                          jojo::OnlineSessionController& client) {
    const auto hosted = host.host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    if (!hosted || !host.view().local_endpoint) return false;
    const auto joined = client.join(jojo::NetworkEndpoint::loopback(0),
                                    *host.view().local_endpoint,
                                    fast_timing(), 0);
    return static_cast<bool>(joined) && drive_connected(host, client);
}

void expect_reset_state(const jojo::OnlineSessionController& controller,
                        const char* label) {
    const auto& view = controller.view();
    expect(view.state == jojo::OnlineSessionState::inactive, label);
    expect(!view.role, "reset clears role");
    expect(!view.local_endpoint, "reset clears local endpoint");
    expect(!view.remote_endpoint, "reset clears remote endpoint");
    expect(view.packets_sent == 0 && view.packets_received == 0 &&
               view.packets_lost == 0,
           "reset clears telemetry counters");
    expect(view.last_error == jojo::ErrorCode::none &&
               view.last_error_detail.empty(),
           "reset clears error");
    expect(!view.can_send_gameplay, "reset closes gameplay gate");
}

void test_endpoint_parser() {
    const auto parsed = jojo::parse_direct_endpoint("127.0.0.1:34567");
    expect(static_cast<bool>(parsed), "canonical endpoint parses");
    if (parsed) {
        expect(parsed.value == jojo::NetworkEndpoint::loopback(34567),
               "parsed endpoint value matches");
        expect(jojo::format_direct_endpoint(parsed.value) == "127.0.0.1:34567",
               "endpoint formats canonically");
    }

    for (const std::string bad : {
             "", "127.0.0.1", "127.0.0.1:", "127.0.0.1:0",
             "127.0.0.1:65536", "256.0.0.1:1234", "127.0.0:1234",
             "127.0.0.1.2:1234", "host:1234", "127.0.0.1 :1234",
             "127.0.0.1: 1234", "::1:1234"}) {
        const auto result = jojo::parse_direct_endpoint(bad);
        const auto message = "reject endpoint: " + bad;
        expect(!result, message.c_str());
        if (!result) {
            expect(result.error == jojo::ErrorCode::invalid_argument,
                   "malformed endpoint uses invalid_argument");
            expect(!result.detail.empty(), "malformed endpoint has detail");
        }
    }
}

void test_host_join_view() {
    jojo::OnlineSessionController idle;
    const auto idle_poll = idle.poll(0);
    expect(!idle_poll && idle_poll.error == jojo::ErrorCode::invalid_argument,
           "poll without active session is validation failure");
    expect(idle.view().state == jojo::OnlineSessionState::inactive,
           "inactive poll does not fault controller");

    jojo::OnlineSessionController host;
    const auto host_started = host.host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(static_cast<bool>(host_started), "host binds");
    expect(host.view().state == jojo::OnlineSessionState::waiting_for_peer,
           "host waits for peer");
    expect(host.view().role == jojo::DirectSessionRole::host, "host role exposed");
    expect(host.view().local_endpoint && host.view().local_endpoint->port != 0,
           "ephemeral host port resolved");
    expect(!host.view().remote_endpoint, "host has no peer before hello");
    expect(!host.view().can_send_gameplay, "host cannot send while waiting");

    const auto host_state = host.view().state;
    const auto duplicate_host = host.host(jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(!duplicate_host && duplicate_host.error == jojo::ErrorCode::invalid_argument,
           "second host start is validation failure");
    expect(host.view().state == host_state, "second host start preserves lifecycle");

    jojo::OnlineSessionController client;
    const auto joined = client.join(jojo::NetworkEndpoint::loopback(0),
                                    *host.view().local_endpoint,
                                    fast_timing(), 0);
    expect(static_cast<bool>(joined), "client join starts");
    expect(client.view().state == jojo::OnlineSessionState::connecting,
           "client exposes connecting");
    expect(client.view().role == jojo::DirectSessionRole::client,
           "client role exposed");
    expect(client.view().remote_endpoint == host.view().local_endpoint,
           "client target exposed immediately");

    const auto client_state = client.view().state;
    const auto duplicate_join = client.join(jojo::NetworkEndpoint::loopback(0),
                                            *host.view().local_endpoint,
                                            fast_timing(), 0);
    expect(!duplicate_join && duplicate_join.error == jojo::ErrorCode::invalid_argument,
           "second join is validation failure");
    expect(client.view().state == client_state, "second join preserves lifecycle");

    expect(drive_connected(host, client), "controllers connect through polling");
    expect(host.view().state == jojo::OnlineSessionState::connected,
           "host reaches connected");
    expect(client.view().state == jojo::OnlineSessionState::connected,
           "client reaches connected");
    expect(host.view().remote_endpoint == client.view().local_endpoint,
           "host exposes pinned client endpoint");
    expect(host.view().can_send_gameplay && client.view().can_send_gameplay,
           "gameplay gate opens when connected");
}

void test_gameplay_and_validation() {
    jojo::NetworkPacket packet{};
    packet.kind = jojo::NetworkPacketKind::input;
    packet.sequence = 77;
    packet.frame = 42;
    packet.payload = {1, 2, 3, 4};

    jojo::OnlineSessionController idle;
    const auto bad_send = idle.send(packet, 0);
    expect(!bad_send && bad_send.error == jojo::ErrorCode::invalid_argument,
           "send while inactive is validation failure");
    expect(idle.view().state == jojo::OnlineSessionState::inactive,
           "send misuse does not fault inactive controller");
    const auto bad_disconnect = idle.disconnect(0);
    expect(!bad_disconnect && bad_disconnect.error == jojo::ErrorCode::invalid_argument,
           "disconnect while inactive is validation failure");
    expect(idle.view().state == jojo::OnlineSessionState::inactive,
           "disconnect misuse does not fault inactive controller");

    jojo::OnlineSessionController host;
    jojo::OnlineSessionController client;
    expect(start_connected_pair(host, client), "gameplay pair connects");
    if (host.view().state != jojo::OnlineSessionState::connected ||
        client.view().state != jojo::OnlineSessionState::connected) return;

    const auto sent = client.send(packet, 21);
    expect(static_cast<bool>(sent), "connected client sends gameplay");
    const auto delivered = host.poll(22);
    expect(static_cast<bool>(delivered), "host poll succeeds after gameplay send");
    expect(delivered && delivered.value.size() == 1,
           "host receives exactly one gameplay packet");
    if (delivered && delivered.value.size() == 1) {
        expect(delivered.value.front() == packet, "gameplay packet preserved");
    }
    expect(client.view().packets_sent > 0, "send refreshes packet telemetry");
    expect(host.view().packets_received > 0, "receive refreshes packet telemetry");
}

void test_reconnect_recovery_and_timeout() {
    jojo::OnlineSessionController host;
    jojo::OnlineSessionController client;
    expect(start_connected_pair(host, client), "reconnect pair connects");
    if (!host.view().remote_endpoint) return;
    const auto pinned = host.view().remote_endpoint;

    expect(static_cast<bool>(host.poll(60)), "host liveness drain succeeds");
    if (host.view().state != jojo::OnlineSessionState::reconnecting) {
        expect(static_cast<bool>(host.poll(100)), "host second liveness poll succeeds");
    }
    expect(host.view().state == jojo::OnlineSessionState::reconnecting,
           "host maps silence to reconnecting");
    expect(host.view().remote_endpoint == pinned, "host keeps pinned peer");
    expect(!host.view().can_send_gameplay, "reconnect suppresses gameplay");

    jojo::NetworkPacket packet{};
    packet.kind = jojo::NetworkPacketKind::input;
    const auto blocked = host.send(packet, 100);
    expect(!blocked && blocked.error == jojo::ErrorCode::invalid_argument,
           "gameplay send is rejected while reconnecting");
    expect(host.view().state == jojo::OnlineSessionState::reconnecting,
           "reconnect send misuse preserves state");

    expect(static_cast<bool>(client.poll(100)), "client advances toward reconnect");
    expect(static_cast<bool>(client.poll(140)), "client starts pinned reconnect");
    expect(static_cast<bool>(host.poll(141)), "host accepts pinned reconnect hello");
    expect(static_cast<bool>(client.poll(142)), "client accepts reconnect response");
    expect(host.view().state == jojo::OnlineSessionState::connected,
           "host recovers connected");
    expect(client.view().state == jojo::OnlineSessionState::connected,
           "client recovers connected");
    expect(host.view().remote_endpoint == pinned, "recovery keeps same peer");

    jojo::OnlineSessionController timeout_host;
    jojo::OnlineSessionController timeout_client;
    expect(start_connected_pair(timeout_host, timeout_client), "timeout pair connects");
    expect(static_cast<bool>(timeout_host.poll(60)), "timeout host drains peer traffic");
    if (timeout_host.view().state != jojo::OnlineSessionState::reconnecting) {
        expect(static_cast<bool>(timeout_host.poll(100)), "timeout host enters reconnecting");
    }
    expect(timeout_host.view().state == jojo::OnlineSessionState::reconnecting,
           "timeout host is reconnecting");
    expect(static_cast<bool>(timeout_host.poll(181)), "reconnect timeout poll succeeds");
    expect(timeout_host.view().state == jojo::OnlineSessionState::disconnected,
           "reconnect timeout maps to disconnected");
    expect(timeout_host.view().last_error == jojo::ErrorCode::none,
           "reconnect timeout is not a fault");
}

void test_disconnect_and_reset() {
    jojo::OnlineSessionController host;
    jojo::OnlineSessionController client;
    expect(start_connected_pair(host, client), "disconnect pair connects");
    const auto disconnected = client.disconnect(30);
    expect(static_cast<bool>(disconnected), "local disconnect succeeds");
    expect(client.view().state == jojo::OnlineSessionState::disconnected,
           "local disconnect maps immediately");
    expect(!client.view().can_send_gameplay, "disconnect closes gameplay gate");

    const auto peer_poll = host.poll(31);
    expect(static_cast<bool>(peer_poll), "peer consumes disconnect");
    expect(host.view().state == jojo::OnlineSessionState::disconnected,
           "remote disconnect maps normally");

    client.reset();
    expect_reset_state(client, "reset from disconnected returns inactive");
    expect(static_cast<bool>(client.host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
           "reset controller can host again");
    client.reset();

    jojo::OnlineSessionController waiting;
    expect(static_cast<bool>(waiting.host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
           "waiting reset host starts");
    waiting.reset();
    expect_reset_state(waiting, "reset from waiting returns inactive");

    jojo::OnlineSessionController connecting;
    expect(static_cast<bool>(connecting.join(jojo::NetworkEndpoint::loopback(0),
                                             jojo::NetworkEndpoint::loopback(65534),
                                             fast_timing(), 0)),
           "connecting reset client starts");
    expect(connecting.view().state == jojo::OnlineSessionState::connecting,
           "client remains connecting without peer");
    connecting.reset();
    expect_reset_state(connecting, "reset from connecting returns inactive");

    jojo::OnlineSessionController reconnect_host;
    jojo::OnlineSessionController reconnect_client;
    expect(start_connected_pair(reconnect_host, reconnect_client),
           "reset reconnect pair connects");
    expect(static_cast<bool>(reconnect_host.poll(60)), "reset reconnect drain succeeds");
    if (reconnect_host.view().state != jojo::OnlineSessionState::reconnecting) {
        expect(static_cast<bool>(reconnect_host.poll(100)),
               "reset reconnect transition succeeds");
    }
    expect(reconnect_host.view().state == jojo::OnlineSessionState::reconnecting,
           "reset source reaches reconnecting");
    reconnect_host.reset();
    expect_reset_state(reconnect_host, "reset from reconnecting returns inactive");
}

void test_spoof_does_not_refresh_product_liveness() {
    jojo::OnlineSessionController host;
    jojo::OnlineSessionController client;
    expect(start_connected_pair(host, client), "spoof pair connects");
    if (!host.view().remote_endpoint || !host.view().local_endpoint) return;
    const auto pinned = host.view().remote_endpoint;

    expect(static_cast<bool>(host.poll(3)), "host drains legitimate handshake traffic");

    auto attacker = jojo::UdpNetworkTransport::bind(jojo::NetworkEndpoint::loopback(0));
    expect(static_cast<bool>(attacker), "attacker transport binds");
    if (attacker) {
        jojo::NetworkPacket spoof{};
        spoof.kind = jojo::NetworkPacketKind::ping;
        spoof.sequence = 900;
        spoof.timestamp_ms = 20;
        expect(static_cast<bool>(attacker.value.send_packet(
                   *host.view().local_endpoint, spoof)),
               "attacker sends spoof ping");
        expect(static_cast<bool>(host.poll(20)), "host ignores spoof without error");
        expect(host.view().remote_endpoint == pinned,
               "spoof cannot replace product peer");
        expect(host.view().state == jojo::OnlineSessionState::connected,
               "spoof does not prematurely alter state");
    }

    expect(static_cast<bool>(host.poll(40)), "host advances beyond valid-peer liveness");
    expect(host.view().state == jojo::OnlineSessionState::reconnecting,
           "spoof does not refresh product liveness");
    expect(host.view().remote_endpoint == pinned,
           "reconnect retains original pinned peer after spoof");
}

void test_operational_fault_contract() {
    jojo::OnlineSessionController first;
    expect(static_cast<bool>(first.host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
           "first host binds");
    if (!first.view().local_endpoint) return;
    const auto occupied = *first.view().local_endpoint;

    jojo::OnlineSessionController second;
    const auto collision = second.host(occupied, fast_timing());
    expect(!collision, "second host bind collision fails");
    expect(collision.error == jojo::ErrorCode::io_error,
           "bind collision preserves transport io_error");
    expect(second.view().state == jojo::OnlineSessionState::faulted,
           "operational bind failure faults controller");
    expect(second.view().last_error == collision.error,
           "fault snapshot preserves error code");
    expect(second.view().last_error_detail == collision.detail &&
               !second.view().last_error_detail.empty(),
           "fault snapshot preserves detail");

    const auto retry_without_reset = second.host(
        jojo::NetworkEndpoint::loopback(0), fast_timing());
    expect(!retry_without_reset &&
               retry_without_reset.error == jojo::ErrorCode::invalid_argument,
           "faulted controller requires reset before restart");
    expect(second.view().last_error == collision.error,
           "lifecycle misuse does not overwrite preserved fault");

    second.reset();
    expect(second.view().state == jojo::OnlineSessionState::inactive,
           "reset clears faulted state");
    expect(second.view().last_error == jojo::ErrorCode::none,
           "reset clears fault code");
    expect(static_cast<bool>(second.host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
           "reset permits host after operational fault");

    jojo::OnlineSessionController corrected;
    const auto bad_join = corrected.join(jojo::NetworkEndpoint::loopback(0),
                                         jojo::NetworkEndpoint::loopback(0),
                                         fast_timing(), 0);
    expect(!bad_join && bad_join.error == jojo::ErrorCode::invalid_argument,
           "zero remote port is validation failure");
    expect(corrected.view().state == jojo::OnlineSessionState::inactive,
           "bad join input leaves controller inactive");
    expect(corrected.view().last_error == jojo::ErrorCode::none &&
               corrected.view().last_error_detail.empty(),
           "bad join input stores no fault");
    expect(static_cast<bool>(corrected.join(jojo::NetworkEndpoint::loopback(0),
                                            occupied, fast_timing(), 0)),
           "corrected join input retries without reset");
}
} // namespace

int main() {
    test_endpoint_parser();
    test_host_join_view();
    test_gameplay_and_validation();
    test_reconnect_recovery_and_timeout();
    test_disconnect_and_reset();
    test_spoof_does_not_refresh_product_liveness();
    test_operational_fault_contract();
    return failures == 0 ? 0 : 1;
}
