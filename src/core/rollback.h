#pragma once

#include "core/result.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

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
    [[nodiscard]] virtual std::vector<std::uint8_t> save_state() const = 0;
    [[nodiscard]] virtual Result<void> load_state(std::span<const std::uint8_t> state) = 0;
    [[nodiscard]] virtual Result<void> step_frame(
        RollbackInput local,
        RollbackInput remote,
        bool emit_side_effects) = 0;
};

class DeterministicRng {
public:
    explicit DeterministicRng(std::uint64_t seed) noexcept : state_(seed) {}

    [[nodiscard]] std::uint32_t next_u32() noexcept;
    [[nodiscard]] std::uint64_t state() const noexcept { return state_; }
    void set_state(std::uint64_t state) noexcept { state_ = state; }

private:
    std::uint64_t state_{};
};

struct RollbackTelemetry {
    std::uint64_t predicted_frames{};
    std::uint32_t last_rollback_depth{};
    std::uint32_t max_rollback_depth{};
};

class RollbackSession {
public:
    RollbackSession(IRollbackSimulation& simulation, std::uint32_t max_rollback_frames) noexcept;

    [[nodiscard]] Result<void> advance(RollbackInput local);
    [[nodiscard]] Result<void> submit_remote_input(std::uint64_t frame, RollbackInput remote);
    [[nodiscard]] Result<void> submit_remote_hash(std::uint64_t frame, std::string hash_hex);

    [[nodiscard]] std::uint64_t current_frame() const noexcept { return current_frame_; }
    [[nodiscard]] Result<std::string> state_hash(std::uint64_t frame) const;
    [[nodiscard]] std::optional<std::uint64_t> desync_frame() const noexcept { return desync_frame_; }
    [[nodiscard]] const RollbackTelemetry& telemetry() const noexcept { return telemetry_; }

private:
    struct FrameRecord {
        std::vector<std::uint8_t> snapshot_before;
        RollbackInput local{};
        RollbackInput remote_used{};
        bool predicted_remote{};
    };

    [[nodiscard]] RollbackInput remote_for_frame(std::uint64_t frame) const noexcept;
    [[nodiscard]] bool has_exact_remote(std::uint64_t frame) const noexcept;
    [[nodiscard]] std::string hash_current_state() const;
    void prune_rollback_history();
    void recompute_desync() noexcept;

    IRollbackSimulation& simulation_;
    std::uint32_t max_rollback_frames_{};
    std::uint64_t current_frame_{};
    std::map<std::uint64_t, FrameRecord> frames_;
    std::map<std::uint64_t, RollbackInput> remote_inputs_;
    std::map<std::uint64_t, std::string> state_hashes_;
    std::map<std::uint64_t, std::string> remote_hashes_;
    std::optional<std::uint64_t> desync_frame_;
    RollbackTelemetry telemetry_{};
};

}
