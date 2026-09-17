#pragma once

#include "core/input.h"
#include "core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace jojo {

enum class FramePhase {
    neutral,
    startup,
    active,
    recovery
};

enum class CollisionBoxKind {
    attack,
    vulnerable,
    push
};

struct CollisionBox {
    float x{};
    float y{};
    float width{};
    float height{};
    CollisionBoxKind kind{CollisionBoxKind::vulnerable};
    std::string label;
    friend bool operator==(const CollisionBox&, const CollisionBox&) = default;
};

struct TrainingFrameSample {
    std::uint64_t frame_index{};
    FramePhase phase{FramePhase::neutral};
    int hitstop_frames{};
    int hitstun_frames{};
    int blockstun_frames{};
    int attacker_recovery_frames{};
    int defender_stun_frames{};
    bool cancel_window{};
    int combo_count{};
    int damage{};
    int scaling_percent{100};
    ResolvedInputFrame inputs{};
    std::vector<CollisionBox> collision_boxes;
};

struct FrameMeterSegment {
    FramePhase phase{FramePhase::neutral};
    std::uint64_t first_frame{};
    std::uint64_t last_frame{};
    std::string label;
    std::string icon;
};

struct TrainingInputHistoryEntry {
    std::uint64_t frame_index{};
    std::size_t player_index{};
    std::vector<GameAction> actions;
};

[[nodiscard]] int frame_advantage(const TrainingFrameSample& sample) noexcept;

class TrainingTimeline {
public:
    explicit TrainingTimeline(std::size_t capacity) noexcept : capacity_(capacity) {}

    [[nodiscard]] Result<void> record(TrainingFrameSample sample);
    [[nodiscard]] const std::deque<TrainingFrameSample>& samples() const noexcept { return samples_; }
    [[nodiscard]] std::vector<FrameMeterSegment> frame_meter() const;
    [[nodiscard]] std::vector<TrainingInputHistoryEntry> input_history() const;

private:
    std::size_t capacity_{};
    std::deque<TrainingFrameSample> samples_;
};

class TrainingPlaybackControl {
public:
    void set_paused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] Result<void> request_frame_step() noexcept;
    [[nodiscard]] bool consume_frame_permission() noexcept;

private:
    static constexpr unsigned max_queued_steps = 8;
    bool paused_{};
    unsigned queued_steps_{};
};

struct TrainingStateSnapshot {
    std::uint64_t frame_index{};
    std::vector<std::uint8_t> state;
    std::string digest_hex;
};

[[nodiscard]] Result<TrainingStateSnapshot> make_training_snapshot(
    std::uint64_t frame_index,
    std::span<const std::uint8_t> state);
[[nodiscard]] Result<void> validate_training_snapshot(const TrainingStateSnapshot& snapshot);

class TrainingStateSlots {
public:
    static constexpr std::size_t slot_count = 10;

    [[nodiscard]] Result<void> save(std::size_t slot, const TrainingStateSnapshot& snapshot);
    [[nodiscard]] Result<TrainingStateSnapshot> load(std::size_t slot) const;

private:
    struct Slot {
        bool occupied{};
        TrainingStateSnapshot snapshot;
    };
    std::array<Slot, slot_count> slots_{};
};

}
