# M8 Rollback Networking Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement and verify the portable deterministic rollback/networking core required by M8.

**Architecture:** Keep simulation rollback separate from wire transport. `rollback.*` owns deterministic frame history, snapshots, prediction, re-simulation, RNG and state hashes; `network_protocol.*` owns versioned datagrams, selective control reliability and telemetry. Platform UDP sockets and online product services remain adapters outside M8.

**Tech Stack:** C++20, repository `Result<T>` and SHA-256 utilities, CMake/CTest, GitHub Actions Linux + Windows/MSVC.

**Spec:** `docs/superpowers/specs/2026-08-30-rollback-networking-core-design.md`

## Global Constraints

- No wall-clock dependency in gameplay simulation.
- Rollback re-simulation must call the simulation with side effects disabled.
- Input/ping/pong packets are never placed in the reliable retransmission queue.
- Control reliability uses caller-supplied monotonic network milliseconds, not simulation frame time.
- Packet serialization is explicit little-endian bytes; never serialize compiler struct memory.
- No proprietary offsets, assets, fingerprints or extracted commercial data.
- Linux and Windows/MSVC must build and pass CTest before merge and after merge.

---

### Task 1: Permanent rollback/network test contract and RED proof

**Files:**
- Create: `tests/test_rollback.cpp`
- Create temporarily: `.github/workflows/m8-red.yml`

**Interfaces the tests require:**

```cpp
namespace jojo {
struct RollbackInput {
    std::uint32_t buttons{};
    std::int16_t axis_x{};
    std::int16_t axis_y{};
    friend bool operator==(const RollbackInput&, const RollbackInput&) = default;
};

class IRollbackSimulation {
public:
    virtual ~IRollbackSimulation() = default;
    virtual std::vector<std::uint8_t> save_state() const = 0;
    virtual Result<void> load_state(std::span<const std::uint8_t>) = 0;
    virtual Result<void> step_frame(RollbackInput local, RollbackInput remote, bool emit_side_effects) = 0;
};

class DeterministicRng {
public:
    explicit DeterministicRng(std::uint64_t seed) noexcept;
    std::uint32_t next_u32() noexcept;
    std::uint64_t state() const noexcept;
    void set_state(std::uint64_t state) noexcept;
};

struct RollbackTelemetry {
    std::uint64_t predicted_frames{};
    std::uint32_t last_rollback_depth{};
    std::uint32_t max_rollback_depth{};
};

class RollbackSession {
public:
    RollbackSession(IRollbackSimulation& simulation, std::uint32_t max_rollback_frames);
    Result<void> advance(RollbackInput local);
    Result<void> submit_remote_input(std::uint64_t frame, RollbackInput remote);
    Result<void> submit_remote_hash(std::uint64_t frame, std::string hash_hex);
    std::uint64_t current_frame() const noexcept;
    Result<std::string> state_hash(std::uint64_t frame) const;
    std::optional<std::uint64_t> desync_frame() const noexcept;
    const RollbackTelemetry& telemetry() const noexcept;
};

enum class NetworkPacketKind : std::uint8_t { input, ping, pong, session_hello, session_accept, disconnect };
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

bool is_reliable_control(NetworkPacketKind) noexcept;
Result<std::vector<std::uint8_t>> serialize_network_packet(const NetworkPacket&);
Result<NetworkPacket> parse_network_packet(std::span<const std::uint8_t>);

class ControlReliabilityQueue {
public:
    explicit ControlReliabilityQueue(std::uint64_t retry_interval_ms);
    Result<void> track(NetworkPacket packet, std::uint64_t now_ms);
    void acknowledge(std::uint32_t sequence) noexcept;
    std::vector<NetworkPacket> due_retransmits(std::uint64_t now_ms);
    std::size_t pending_count() const noexcept;
};

enum class NetworkConnectionState { connected, reconnecting, disconnected };
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
    void record_packet_sent() noexcept;
    void record_packet_received() noexcept;
    void record_packet_lost() noexcept;
    void record_prediction() noexcept;
    void record_rollback(std::uint32_t depth) noexcept;
    double packet_loss_percent() const noexcept;
};
}
```

- [ ] **Step 1: Write the permanent test** covering deterministic RNG state restore, prediction, corrected rollback equivalence, suppressed replay side effects, too-old rollback rejection, state-hash/desync, packet round-trip/rejection, selective reliability and telemetry.
- [ ] **Step 2: Add Linux-only temporary RED workflow** that runs `g++ -std=c++20 -Isrc -fsyntax-only tests/test_rollback.cpp`.
- [ ] **Step 3: Run GitHub Actions and verify RED**. Expected failure: `core/rollback.h` or `core/network_protocol.h` missing. A typo, workflow-environment failure or unrelated compile error does not count.
- [ ] **Step 4: Preserve the RED run ID/SHA** for the verification document.

### Task 2: Deterministic rollback core

**Files:**
- Create: `src/core/rollback.h`
- Create: `src/core/rollback.cpp`
- Test: `tests/test_rollback.cpp`

**Implementation contract:**

- `DeterministicRng` uses fixed 64-bit integer arithmetic only. Identical seed/state must produce identical `next_u32()` output on every platform.
- `RollbackSession` indexes frames starting at 0. `current_frame()` is the next frame to simulate.
- Before simulating frame N, save a snapshot for N and remember the local input and the remote input actually used.
- Missing remote input predicts the latest known remote input, initially all-zero.
- After frame N, save/hash the resulting state under hash key N.
- A late exact remote input equal to the already-used prediction does not rollback.
- A differing late input for an already simulated retained frame restores that frame's snapshot, replaces the remote input and replays through `current_frame()-1` with `emit_side_effects=false`.
- Rollback depth is `current_frame() - corrected_frame` and may not exceed `max_rollback_frames`.
- A too-old correction returns `invalid_argument` without changing simulation state/history.
- State hashes use `sha256_hex(sha256(save_state()))`.
- `submit_remote_hash` records the earliest frame whose local hash is known and differs.

- [ ] **Step 1: Create `rollback.h` with the exact public types above.**
- [ ] **Step 2: Implement deterministic RNG.**
- [ ] **Step 3: Implement forward frame history/prediction/hash capture.**
- [ ] **Step 4: Implement late-input rollback/re-simulation and bounded retention.**
- [ ] **Step 5: Implement remote hash/desync detection.**

### Task 3: Datagram protocol, selective reliability and telemetry

**Files:**
- Create: `src/core/network_protocol.h`
- Create: `src/core/network_protocol.cpp`
- Test: `tests/test_rollback.cpp`

**Wire format:**

```
magic[4] = 'J','R','B','K'
version u8 = 1
kind u8
reserved u16 = 0
sequence u32 LE
ack u32 LE
frame u64 LE
timestamp_ms u64 LE
buttons u32 LE
axis_x i16 LE
axis_y i16 LE
payload_size u16 LE
payload[payload_size]
```

Maximum payload: 1024 bytes. The parser requires exact packet length and rejects unknown kind/version, wrong magic, reserved != 0 and payload > 1024.

**Reliability:**

- `session_hello`, `session_accept`, `disconnect` are reliable control.
- `input`, `ping`, `pong` are unreliable.
- `track` rejects unreliable kinds and duplicate pending sequences.
- `due_retransmits(now)` returns packets whose elapsed time is at least `retry_interval_ms`, and moves their last-send time to `now`.
- acknowledgements remove exactly the matching sequence.

**Telemetry:**

- RTT stores the latest non-negative sample.
- Jitter starts at 0; subsequent RTT samples update `jitter = 0.75*jitter + 0.25*abs(sample - previous_rtt)`.
- Packet loss percentage is `lost / (received + lost) * 100`, zero when denominator is zero.
- Rollback records last depth and maximum depth.

- [ ] **Step 1: Implement packet-kind reliability classification.**
- [ ] **Step 2: Implement deterministic serializer/parser with validation.**
- [ ] **Step 3: Implement selective reliability queue.**
- [ ] **Step 4: Implement telemetry metrics and lifecycle state container.**

### Task 4: Build integration and GREEN proof

**Files:**
- Modify: `CMakeLists.txt`
- Delete: `.github/workflows/m8-red.yml`

- [ ] **Step 1: Add `src/core/rollback.cpp` and `src/core/network_protocol.cpp` to `jojo_core`.**
- [ ] **Step 2: Add `jojo_rollback_tests` target linked to `jojo_core` and register it with CTest.**
- [ ] **Step 3: Delete the temporary RED workflow.**
- [ ] **Step 4: Run the normal GitHub build workflow.**
- [ ] **Step 5: Require Portable core/Linux Configure+Build+Test success.**
- [ ] **Step 6: Require Windows x64/MSVC Configure+Build Release+Test Release+artifact upload success.**
- [ ] **Step 7: Record workflow ID, commit SHA, Windows artifact ID and SHA-256 digest.**

### Task 5: Verification, roadmap and branch completion

**Files:**
- Create: `docs/superpowers/plans/2026-08-30-rollback-networking-core-verification.md`
- Modify: `docs/architecture/PRODUCTION-ROADMAP.md`

- [ ] **Step 1: Document RED and GREEN evidence, test coverage and readiness boundary.**
- [ ] **Step 2: Mark M8 `Complete (100%)` only for the reusable rollback/networking-core contract and list each closed criterion.**
- [ ] **Step 3: Run final branch CI after documentation changes.**
- [ ] **Step 4: Compare branch to `main`; require no temporary workflow and no unrelated files.**
- [ ] **Step 5: Open PR to `main`; confirm the PR head SHA matches the verified head.**
- [ ] **Step 6: Merge using `expected_head_sha`.**
- [ ] **Step 7: Require a fresh post-merge `main` GitHub Actions run with Linux and Windows success plus Windows executable artifact.**
- [ ] **Step 8: Only after Step 7 may M8 be reported complete.**
