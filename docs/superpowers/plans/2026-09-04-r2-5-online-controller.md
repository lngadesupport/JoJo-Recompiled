# R2.5 Online Session Controller Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a UI-independent `OnlineSessionController` that wraps the existing direct UDP session with direct IPv4 endpoint parsing, stable product lifecycle/view state, gameplay gating, reconnect/disconnect/reset behavior, and user-presentable errors.

**Architecture:** `OnlineSessionController` owns one optional `DirectUdpSession` and never reimplements protocol, reliability, liveness, spoof protection, or telemetry math. A pure endpoint parser/formatter and immutable `OnlineSessionViewState` form the future UI boundary; controller methods translate product actions into `DirectUdpSession` calls and derive product state from transport state using caller-supplied time only.

**Tech Stack:** C++20, existing `jojo_core`, `DirectUdpSession`, real loopback IPv4 UDP sockets, CMake/CTest, GitHub Actions Linux + Windows/MSVC.

**Spec:** `docs/superpowers/specs/2026-09-04-r2-5-online-controller-design.md`

## Global Constraints

- Work on `feature/r2-5-online-controller` until the package is reviewed and GREEN.
- Do not modify the first-run Win32 preparation UI or `SettingsMenuSession` for this package.
- Do not implement or claim matchmaking, casual/ranked backend services, public rooms, invitations, accounts, profiles/history, NAT traversal, relay, DNS/hostname resolution, encryption/authentication, spectator mode, replays, game-specific commercial online integration, or complete M9 UI.
- Keep all time-dependent behavior caller-driven through `now_ms`; do not read wall-clock time in the controller.
- Keep handshake, retransmission, heartbeat/liveness, reconnect, spoof protection, disconnect control, and telemetry math owned by `DirectUdpSession`.
- Keep gameplay send/delivery allowed only while the product state is `connected`.
- Validation/lifecycle misuse must not poison an otherwise healthy state; operational I/O/runtime failures must enter `faulted` and require `reset()` before a new session.
- `reset()` is local-only, valid from every product state, and may cancel waiting/connecting/reconnecting without pretending to notify a peer.
- Keep R2.5 `implemented-unverified`; generic tests do not prove commercial-game online compatibility.
- Preserve R2.2/R2.4 external-evidence blockers.
- Keep one shipping executable: `JOJO-Recompiled.exe`.
- Mods remain deferred until base-game production completion.

## File Structure

- Create `src/core/online_session.h` — public product-facing online state, endpoint parser/formatter declarations, view snapshot, controller API.
- Create `src/core/online_session.cpp` — endpoint parsing/formatting, lifecycle translation, view refresh, fault handling, and delegation to `DirectUdpSession`.
- Create `tests/test_online_session.cpp` — focused product-layer contract using real loopback UDP sockets and deterministic caller-supplied time.
- Modify `CMakeLists.txt` — compile `online_session.cpp` into `jojo_core` and register `jojo_online_session_tests` as a normal CTest target.
- Modify `PROJECT-STATE.md` only at the final checkpoint — record the GREEN controller evidence without changing R2.5 verification status.
- Modify `docs/NEXT-MILESTONES.md` only at the final checkpoint — record that the direct-session product controller exists while keeping service/UI non-goals explicit.
- Modify `docs/architecture/PRODUCTION-READINESS.tsv` only at the final checkpoint — update R2.5 evidence to the new full package CI run while retaining `implemented-unverified`.

---

### Task 1: Endpoint parser, product types, and build registration

**Files:**
- Create: `src/core/online_session.h`
- Create: `src/core/online_session.cpp`
- Create: `tests/test_online_session.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `jojo::NetworkEndpoint`, `jojo::DirectSessionRole`, `jojo::DirectSessionTiming`, `jojo::NetworkPacket`, `jojo::ErrorCode`, `jojo::Result<T>`.
- Produces:

```cpp
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
```

- [ ] **Step 1: Register the new source and test target in CMake before implementation**

Add `src/core/online_session.cpp` to `add_library(jojo_core STATIC ...)` immediately after `src/core/network_protocol.cpp`.

Add:

```cmake
add_executable(jojo_online_session_tests tests/test_online_session.cpp)
target_link_libraries(jojo_online_session_tests PRIVATE jojo_core)
add_test(NAME jojo_online_session_tests COMMAND jojo_online_session_tests)
```

- [ ] **Step 2: Write the initial RED parser contract**

Create `tests/test_online_session.cpp`:

```cpp
#include "core/online_session.h"

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
} // namespace

int main() {
    test_endpoint_parser();
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Run the new target and verify RED**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target jojo_online_session_tests --parallel 2
```

Expected: FAIL because `core/online_session.h` does not exist.

- [ ] **Step 4: Add the minimal public header**

Create `src/core/online_session.h`:

```cpp
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

class OnlineSessionController;

} // namespace jojo
```

- [ ] **Step 5: Implement the pure parser/formatter minimally**

Create `src/core/online_session.cpp` with a manual decimal parser so no hostname/DNS or locale behavior can enter the contract:

```cpp
#include "core/online_session.h"

#include <array>
#include <cctype>
#include <limits>

namespace jojo {
namespace {
Result<unsigned> parse_decimal(std::string_view text, unsigned maximum,
                               const char* detail) {
    if (text.empty()) {
        return Result<unsigned>::failure(ErrorCode::invalid_argument, detail);
    }
    unsigned value = 0;
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
        colon + 1u >= text.size() || text.find(':', colon + 1u) != std::string_view::npos) {
        return Result<NetworkEndpoint>::failure(
            ErrorCode::invalid_argument,
            "direct endpoint must use A.B.C.D:PORT");
    }

    std::array<std::uint8_t, 4> octets{};
    std::string_view address = text.substr(0, colon);
    std::size_t begin = 0;
    for (std::size_t index = 0; index < octets.size(); ++index) {
        const auto dot = address.find('.', begin);
        const bool last = index + 1u == octets.size();
        if ((last && dot != std::string_view::npos) ||
            (!last && dot == std::string_view::npos)) {
            return Result<NetworkEndpoint>::failure(
                ErrorCode::invalid_argument,
                "direct endpoint must use A.B.C.D:PORT");
        }
        const auto end = last ? address.size() : dot;
        const auto value = parse_decimal(address.substr(begin, end - begin), 255u,
                                         "direct endpoint IPv4 octet is invalid");
        if (!value) {
            return Result<NetworkEndpoint>::failure(value.error, value.detail);
        }
        octets[index] = static_cast<std::uint8_t>(value.value);
        begin = end + 1u;
    }

    const auto port = parse_decimal(text.substr(colon + 1u), 65535u,
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

} // namespace jojo
```

- [ ] **Step 6: Run parser target GREEN**

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit Task 1**

```bash
git add CMakeLists.txt src/core/online_session.h src/core/online_session.cpp tests/test_online_session.cpp
git commit -m "feat: add direct online endpoint contract"
```

---

### Task 2: Host/join lifecycle and stable view snapshot

**Files:**
- Modify: `src/core/online_session.h`
- Modify: `src/core/online_session.cpp`
- Modify: `tests/test_online_session.cpp`

**Interfaces:**
- Consumes: Task 1 types/parser and the existing `DirectUdpSession` API.
- Produces:

```cpp
class OnlineSessionController {
public:
    [[nodiscard]] const OnlineSessionViewState& view() const noexcept;
    [[nodiscard]] Result<void> host(NetworkEndpoint local,
                                    DirectSessionTiming timing = {});
    [[nodiscard]] Result<void> join(NetworkEndpoint local,
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
```

- [ ] **Step 1: Add RED tests for host/join, inactive poll, and view state**

Add:

```cpp
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
```

Before starting a session:

```cpp
jojo::OnlineSessionController idle;
const auto idle_poll = idle.poll(0);
expect(!idle_poll && idle_poll.error == jojo::ErrorCode::invalid_argument,
       "poll without active session is validation failure");
expect(idle.view().state == jojo::OnlineSessionState::inactive,
       "inactive poll does not fault controller");
```

Host/join contract:

```cpp
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

expect(drive_connected(host, client), "controllers connect through polling");
expect(host.view().remote_endpoint == client.view().local_endpoint,
       "host exposes pinned client endpoint");
expect(host.view().can_send_gameplay && client.view().can_send_gameplay,
       "gameplay gate opens when connected");
```

Also call `host()`/`join()` a second time on an active controller and assert `invalid_argument` with no lifecycle change.

- [ ] **Step 2: Run and verify RED**

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
```

Expected: FAIL because `OnlineSessionController` methods do not exist.

- [ ] **Step 3: Implement host/join/poll and view derivation**

Add the class declaration from the Interfaces block to `online_session.h`.

Implement these preconditions:

```cpp
const OnlineSessionViewState& OnlineSessionController::view() const noexcept {
    return view_;
}

Result<std::vector<NetworkPacket>> OnlineSessionController::poll(std::uint64_t now_ms) {
    if (!session_) {
        return Result<std::vector<NetworkPacket>>::failure(
            ErrorCode::invalid_argument, "online poll requires active session");
    }
    const auto result = session_->poll(now_ms);
    if (!result) {
        set_fault(result.error, result.detail);
        return Result<std::vector<NetworkPacket>>::failure(result.error, result.detail);
    }
    refresh_view();
    return Result<std::vector<NetworkPacket>>::success(result.value);
}
```

Implement `host()`:

```cpp
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
```

Implement `join()`:

```cpp
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
```

Implement `refresh_view()` so it maps transport state exactly:

```cpp
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
```

Do not clear `last_error` in `refresh_view()`.

- [ ] **Step 4: Run Task 2 GREEN**

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit Task 2**

```bash
git add src/core/online_session.h src/core/online_session.cpp tests/test_online_session.cpp
git commit -m "feat: add online host join lifecycle"
```

---

### Task 3: Gameplay, reconnect mapping, disconnect, reset, and spoof resistance

**Files:**
- Modify: `src/core/online_session.cpp`
- Modify: `tests/test_online_session.cpp`

**Interfaces:**
- Consumes: Task 2 controller API and existing `DirectUdpSession` behavior.
- Produces: working `send`, `disconnect`, `reset`, telemetry refresh, reconnect/disconnected mapping, and stable peer presentation.

- [ ] **Step 1: Add RED gameplay and misuse tests**

After `drive_connected()`:

```cpp
jojo::NetworkPacket packet{};
packet.kind = jojo::NetworkPacketKind::input;
packet.sequence = 77;
packet.frame = 42;
packet.payload = {1, 2, 3, 4};

const auto sent = client.send(packet, 21);
expect(static_cast<bool>(sent), "connected client sends gameplay");
const auto delivered = host.poll(22);
expect(static_cast<bool>(delivered), "host poll succeeds after gameplay send");
expect(delivered && delivered.value.size() == 1,
       "host receives exactly one gameplay packet");
if (delivered && delivered.value.size() == 1) {
    expect(delivered.value.front() == packet, "gameplay packet preserved");
}
```

Before connection:

```cpp
jojo::OnlineSessionController idle_controller;
const auto bad_send = idle_controller.send(packet, 0);
expect(!bad_send && bad_send.error == jojo::ErrorCode::invalid_argument,
       "send while inactive is validation failure");
expect(idle_controller.view().state == jojo::OnlineSessionState::inactive,
       "send misuse does not fault inactive controller");
```

- [ ] **Step 2: Add RED reconnect/recovery and reconnect-timeout tests**

Use `fast_timing()` and monotonic caller times. After connection, stop polling the peer and advance one side beyond the 30 ms liveness timeout. Assert `reconnecting`, unchanged pinned endpoint, and closed gameplay gate.

Use this sequence; if a queued heartbeat is consumed at `60`, perform one drain poll there and then use `100` for the liveness transition. Do not change timing constants:

```cpp
const auto pinned = host.view().remote_endpoint;
expect(static_cast<bool>(host.poll(60)), "host liveness poll succeeds");
if (host.view().state != jojo::OnlineSessionState::reconnecting) {
    expect(static_cast<bool>(host.poll(100)), "host second liveness poll succeeds");
}
expect(host.view().state == jojo::OnlineSessionState::reconnecting,
       "host maps silence to reconnecting");
expect(host.view().remote_endpoint == pinned, "host keeps pinned peer");
expect(!host.view().can_send_gameplay, "reconnect suppresses gameplay");

expect(static_cast<bool>(client.poll(100)), "client advances toward reconnect");
expect(static_cast<bool>(client.poll(140)), "client starts pinned reconnect");
expect(static_cast<bool>(host.poll(141)), "host accepts pinned reconnect hello");
expect(static_cast<bool>(client.poll(142)), "client accepts reconnect response");
expect(host.view().state == jojo::OnlineSessionState::connected,
       "host recovers connected");
expect(client.view().state == jojo::OnlineSessionState::connected,
       "client recovers connected");
```

For timeout, use a fresh connected pair, enter reconnecting, then advance the reconnecting side by more than `fast_timing().reconnect_timeout_ms` without a valid pinned-peer hello/accept. Assert `disconnected` and `last_error == ErrorCode::none`.

- [ ] **Step 3: Add RED explicit disconnect and reset tests**

```cpp
const auto disconnected = client.disconnect(30);
expect(static_cast<bool>(disconnected), "local disconnect succeeds");
expect(client.view().state == jojo::OnlineSessionState::disconnected,
       "local disconnect maps immediately");
expect(!client.view().can_send_gameplay, "disconnect closes gameplay gate");

const auto peer_poll = host.poll(31);
expect(static_cast<bool>(peer_poll), "peer consumes disconnect");
expect(host.view().state == jojo::OnlineSessionState::disconnected,
       "remote disconnect maps normally");
```

Test `reset()` from `waiting_for_peer`, `connecting`, `reconnecting`, and `disconnected`. Task 4 separately adds the `faulted` reset case. Every reset must restore:

```cpp
expect(controller.view().state == jojo::OnlineSessionState::inactive,
       "reset returns inactive");
expect(!controller.view().role, "reset clears role");
expect(!controller.view().local_endpoint, "reset clears local endpoint");
expect(!controller.view().remote_endpoint, "reset clears remote endpoint");
expect(controller.view().packets_sent == 0 &&
       controller.view().packets_received == 0 &&
       controller.view().packets_lost == 0,
       "reset clears telemetry counters");
expect(controller.view().last_error == jojo::ErrorCode::none &&
       controller.view().last_error_detail.empty(),
       "reset clears error");
```

Then start a fresh host on the reset controller.

- [ ] **Step 4: Add RED product-layer spoof test**

Bind a third raw `UdpNetworkTransport`, send a valid ping to the host, and assert the product peer is unchanged and silence still drives liveness into reconnecting:

```cpp
auto attacker = jojo::UdpNetworkTransport::bind(jojo::NetworkEndpoint::loopback(0));
expect(static_cast<bool>(attacker), "attacker transport binds");
if (attacker) {
    jojo::NetworkPacket spoof{};
    spoof.kind = jojo::NetworkPacketKind::ping;
    spoof.sequence = 900;
    spoof.timestamp_ms = 40;
    expect(static_cast<bool>(attacker.value.send_packet(
        *host.view().local_endpoint, spoof)), "attacker sends spoof ping");
    expect(static_cast<bool>(host.poll(40)), "host ignores spoof without error");
    expect(host.view().remote_endpoint == pinned,
           "spoof cannot replace product peer");
}
```

Advance beyond liveness timeout without valid peer traffic and assert `reconnecting`.

- [ ] **Step 5: Run tests and verify RED**

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: FAIL for missing `send`, `disconnect`, `reset`, or lifecycle refresh behavior.

- [ ] **Step 6: Implement gameplay, disconnect, and reset delegation**

```cpp
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
```

`poll()` from Task 2 already refreshes view after every successful transport poll, including zero gameplay delivery; keep that behavior unchanged.

- [ ] **Step 7: Run Task 3 GREEN**

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit Task 3**

```bash
git add src/core/online_session.cpp tests/test_online_session.cpp
git commit -m "feat: complete direct online session lifecycle"
```

---

### Task 4: Operational fault contract, full regression, and package checkpoint

**Files:**
- Modify: `src/core/online_session.cpp`
- Modify: `tests/test_online_session.cpp`
- Modify after GREEN evidence: `PROJECT-STATE.md`
- Modify after GREEN evidence: `docs/NEXT-MILESTONES.md`
- Modify after GREEN evidence: `docs/architecture/PRODUCTION-READINESS.tsv`

**Interfaces:**
- Consumes: completed controller from Tasks 1–3 and current GitHub Actions workflow.
- Produces: deterministic `faulted` behavior, full Linux/Windows evidence, and truth documents pointing at that evidence without changing R2.5 status.

- [ ] **Step 1: Add RED operational-fault and faulted-reset tests**

Use a deterministic bind collision:

```cpp
jojo::OnlineSessionController first;
expect(static_cast<bool>(first.host(jojo::NetworkEndpoint::loopback(0), fast_timing())),
       "first host binds");
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

const auto retry_without_reset = second.host(jojo::NetworkEndpoint::loopback(0), fast_timing());
expect(!retry_without_reset && retry_without_reset.error == jojo::ErrorCode::invalid_argument,
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
```

Also assert `join(... remote.port = 0 ...)` leaves an inactive controller `inactive` with no stored last error, so corrected input can be retried without reset.

- [ ] **Step 2: Run and verify RED if fault persistence is incomplete**

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: FAIL until fault persistence/restart gating matches the contract.

- [ ] **Step 3: Implement the fault helper and preserve validation semantics**

```cpp
void OnlineSessionController::set_fault(ErrorCode error, std::string detail) {
    view_.state = OnlineSessionState::faulted;
    view_.can_send_gameplay = false;
    view_.last_error = error;
    view_.last_error_detail = std::move(detail);
}
```

Keep the existing `host()`/`join()` non-`inactive` precondition. In `faulted`, it must return:

```cpp
Result<void>::failure(ErrorCode::invalid_argument,
                      "online session is already active")
```

without changing the preserved fault snapshot.

Operational failures from bind/connect/poll/gameplay send/disconnect call `set_fault()` and return the original error/detail. Validation failures do not call `set_fault()`.

- [ ] **Step 4: Run focused controller GREEN**

```bash
cmake --build build --target jojo_online_session_tests --parallel 2
ctest --test-dir build -R jojo_online_session_tests --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Run the existing transport contract locally**

```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc \
  tests/test_network_transport.cpp src/core/network_protocol.cpp \
  -o network_transport_tests
./network_transport_tests
```

Expected: PASS. Do not alter the standalone R2.5 transport test or workflow command.

- [ ] **Step 6: Run full local regression and readiness gates**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadiness.cmake
cmake -DJOJO_SOURCE_DIR="$PWD" -P cmake/CheckProductionReadinessNegative.cmake
```

Expected: all CTests, including `jojo_online_session_tests`, plus both readiness scripts pass.

- [ ] **Step 7: Commit the completed implementation before CI**

```bash
git add CMakeLists.txt src/core/online_session.h src/core/online_session.cpp tests/test_online_session.cpp
git commit -m "feat: add R2.5 online session controller"
```

- [ ] **Step 8: Push and require full GitHub Actions GREEN**

Push `feature/r2-5-online-controller`. Existing `.github/workflows/build.yml` already triggers on `feature/**`; do not modify it for this package.

Require on the same implementation HEAD:

- Linux: Configure, Build, Production readiness gate, CTest, standalone R2.5 direct UDP transport contract — success.
- Windows/MSVC: Configure, Build Release, Production readiness gate, CTest Release, standalone R2.5 transport contract, single-executable artifact upload — success.
- `jojo_online_session_tests` passes inside normal CTest on both operating systems.
- Artifact remains `JOJO-Recompiled-Windows-x64` and contains the shipping `JOJO-Recompiled.exe` output.

Record workflow run ID, implementation SHA, artifact ID, and artifact SHA-256 digest.

- [ ] **Step 9: Update truth documents only after GREEN evidence exists**

Update `PROJECT-STATE.md` with branch, design path, implementation SHA, GREEN workflow ID, artifact digest, implemented controller scope, explicit service/UI non-goals, and the unchanged `implemented-unverified` status.

Update `docs/NEXT-MILESTONES.md` so R2.5 records the product-facing direct-session controller and names the next user-facing increment as in-game Online presentation/integration, still without service claims.

Update only R2.5 evidence in `docs/architecture/PRODUCTION-READINESS.tsv`:

```tsv
R2.5	implemented-unverified	github-actions:run-<NEW_GREEN_RUN_ID>	none
```

Leave every other workstream status/blocker unchanged.

- [ ] **Step 10: Commit checkpoint documents**

```bash
git add PROJECT-STATE.md docs/NEXT-MILESTONES.md docs/architecture/PRODUCTION-READINESS.tsv
git commit -m "checkpoint: record R2.5 online controller evidence"
```

- [ ] **Step 11: Require final full CI on the documentation checkpoint HEAD**

Push the checkpoint commit and require the same Linux/Windows/readiness/CTest/standalone-R2.5/artifact gates again. Confirm final branch HEAD equals the tested checkpoint SHA and record the final artifact digest.

- [ ] **Step 12: Final truth check**

```text
feature/r2-5-online-controller HEAD == final GREEN checkpoint SHA
main unchanged from package start
R2.5 status == implemented-unverified
R2.2/R2.4 blockers unchanged
R2.6 status unchanged
single executable policy unchanged
no commercial fingerprints/assets/data committed
```

Expected: every statement is true.
