#include "core/training.h"

#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <utility>

namespace jojo {
namespace {

bool valid_box(const CollisionBox& box) noexcept {
    return box.width >= 0.0f && box.height >= 0.0f && !box.label.empty();
}

std::pair<std::string, std::string> meter_semantics(FramePhase phase) {
    switch (phase) {
        case FramePhase::startup: return {"Startup", "S"};
        case FramePhase::active: return {"Active", "A"};
        case FramePhase::recovery: return {"Recovery", "R"};
        case FramePhase::neutral: break;
    }
    return {"Neutral", "N"};
}

std::vector<std::uint8_t> canonical_snapshot_bytes(
    std::uint64_t frame_index,
    std::span<const std::uint8_t> state) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(8 + state.size());
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((frame_index >> shift) & 0xffu));
    }
    bytes.insert(bytes.end(), state.begin(), state.end());
    return bytes;
}

std::string snapshot_digest(std::uint64_t frame_index, std::span<const std::uint8_t> state) {
    const auto bytes = canonical_snapshot_bytes(frame_index, state);
    return sha256_hex(sha256(bytes));
}

}

int frame_advantage(const TrainingFrameSample& sample) noexcept {
    return sample.defender_stun_frames - sample.attacker_recovery_frames;
}

Result<void> TrainingTimeline::record(TrainingFrameSample sample) {
    if (capacity_ == 0) {
        return Result<void>::failure(ErrorCode::invalid_argument, "training timeline capacity must be greater than zero");
    }
    if (!samples_.empty() && sample.frame_index <= samples_.back().frame_index) {
        return Result<void>::failure(ErrorCode::invalid_argument, "training frame index must increase monotonically");
    }
    if (sample.hitstop_frames < 0 || sample.hitstun_frames < 0 || sample.blockstun_frames < 0 ||
        sample.attacker_recovery_frames < 0 || sample.defender_stun_frames < 0 ||
        sample.combo_count < 0 || sample.damage < 0 ||
        sample.scaling_percent < 0 || sample.scaling_percent > 100) {
        return Result<void>::failure(ErrorCode::invalid_argument, "training frame contains invalid combat counters");
    }
    for (const auto& box : sample.collision_boxes) {
        if (!valid_box(box)) {
            return Result<void>::failure(ErrorCode::invalid_argument, "training collision box is invalid");
        }
    }

    samples_.push_back(std::move(sample));
    while (samples_.size() > capacity_) samples_.pop_front();
    return Result<void>::success();
}

std::vector<FrameMeterSegment> TrainingTimeline::frame_meter() const {
    std::vector<FrameMeterSegment> segments;
    for (const auto& sample : samples_) {
        if (sample.phase == FramePhase::neutral) continue;
        if (segments.empty() || segments.back().phase != sample.phase ||
            segments.back().last_frame + 1 != sample.frame_index) {
            const auto [label, icon] = meter_semantics(sample.phase);
            segments.push_back(FrameMeterSegment{
                sample.phase, sample.frame_index, sample.frame_index, label, icon});
        } else {
            segments.back().last_frame = sample.frame_index;
        }
    }
    return segments;
}

std::vector<TrainingInputHistoryEntry> TrainingTimeline::input_history() const {
    std::vector<TrainingInputHistoryEntry> history;
    for (const auto& sample : samples_) {
        for (std::size_t player = 0; player < input_player_count; ++player) {
            TrainingInputHistoryEntry entry{};
            entry.frame_index = sample.frame_index;
            entry.player_index = player;
            for (const auto action : all_game_actions()) {
                if (sample.inputs[player].pressed(action)) entry.actions.push_back(action);
            }
            if (!entry.actions.empty()) history.push_back(std::move(entry));
        }
    }
    return history;
}

void TrainingPlaybackControl::set_paused(bool paused) noexcept {
    paused_ = paused;
    if (!paused_) queued_steps_ = 0;
}

Result<void> TrainingPlaybackControl::request_frame_step() noexcept {
    if (!paused_) {
        return Result<void>::failure(ErrorCode::invalid_argument, "frame step requires paused training playback");
    }
    if (queued_steps_ < max_queued_steps) ++queued_steps_;
    return Result<void>::success();
}

bool TrainingPlaybackControl::consume_frame_permission() noexcept {
    if (!paused_) return true;
    if (queued_steps_ == 0) return false;
    --queued_steps_;
    return true;
}

Result<TrainingStateSnapshot> make_training_snapshot(
    std::uint64_t frame_index,
    std::span<const std::uint8_t> state) {
    TrainingStateSnapshot snapshot{};
    snapshot.frame_index = frame_index;
    snapshot.state.assign(state.begin(), state.end());
    snapshot.digest_hex = snapshot_digest(frame_index, snapshot.state);
    return Result<TrainingStateSnapshot>::success(std::move(snapshot));
}

Result<void> validate_training_snapshot(const TrainingStateSnapshot& snapshot) {
    if (snapshot.digest_hex.empty()) {
        return Result<void>::failure(ErrorCode::invalid_argument, "training snapshot digest is empty");
    }
    if (snapshot.digest_hex != snapshot_digest(snapshot.frame_index, snapshot.state)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "training snapshot integrity check failed");
    }
    return Result<void>::success();
}

Result<void> TrainingStateSlots::save(std::size_t slot, const TrainingStateSnapshot& snapshot) {
    if (slot >= slot_count) {
        return Result<void>::failure(ErrorCode::invalid_argument, "training save slot is outside range 0..9");
    }
    const auto valid = validate_training_snapshot(snapshot);
    if (!valid) return Result<void>::failure(valid.error, valid.detail);
    slots_[slot].snapshot = snapshot;
    slots_[slot].occupied = true;
    return Result<void>::success();
}

Result<TrainingStateSnapshot> TrainingStateSlots::load(std::size_t slot) const {
    if (slot >= slot_count) {
        return Result<TrainingStateSnapshot>::failure(ErrorCode::invalid_argument, "training save slot is outside range 0..9");
    }
    if (!slots_[slot].occupied) {
        return Result<TrainingStateSnapshot>::failure(ErrorCode::file_not_found, "training save slot is empty");
    }
    const auto valid = validate_training_snapshot(slots_[slot].snapshot);
    if (!valid) return Result<TrainingStateSnapshot>::failure(valid.error, valid.detail);
    return Result<TrainingStateSnapshot>::success(slots_[slot].snapshot);
}

}
