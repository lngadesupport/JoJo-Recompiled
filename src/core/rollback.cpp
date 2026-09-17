#include "core/rollback.h"

#include "core/sha256.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <span>
#include <utility>

namespace jojo {

std::uint32_t DeterministicRng::next_u32() noexcept {
    state_ += 0x9e3779b97f4a7c15ull;
    auto z = state_;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    return static_cast<std::uint32_t>(z >> 32);
}

RollbackSession::RollbackSession(
    IRollbackSimulation& simulation,
    std::uint32_t max_rollback_frames) noexcept
    : simulation_(simulation), max_rollback_frames_(max_rollback_frames) {}

RollbackInput RollbackSession::remote_for_frame(std::uint64_t frame) const noexcept {
    const auto upper = remote_inputs_.upper_bound(frame);
    if (upper == remote_inputs_.begin()) return {};
    return std::prev(upper)->second;
}

bool RollbackSession::has_exact_remote(std::uint64_t frame) const noexcept {
    return remote_inputs_.find(frame) != remote_inputs_.end();
}

std::string RollbackSession::hash_current_state() const {
    const auto state = simulation_.save_state();
    const auto bytes = std::span<const std::uint8_t>(state.data(), state.size());
    return sha256_hex(sha256(bytes));
}

Result<void> RollbackSession::advance(RollbackInput local) {
    const auto frame = current_frame_;
    const auto snapshot = simulation_.save_state();
    const auto remote = remote_for_frame(frame);
    const bool predicted = !has_exact_remote(frame);

    const auto stepped = simulation_.step_frame(local, remote, true);
    if (!stepped) {
        const auto restored = simulation_.load_state(snapshot);
        if (!restored) {
            return Result<void>::failure(restored.error, "frame step failed and rollback restore also failed: " + restored.detail);
        }
        return Result<void>::failure(stepped.error, stepped.detail);
    }

    frames_[frame] = FrameRecord{snapshot, local, remote, predicted};
    state_hashes_[frame] = hash_current_state();
    if (predicted) ++telemetry_.predicted_frames;
    ++current_frame_;
    recompute_desync();
    prune_rollback_history();
    return Result<void>::success();
}

Result<void> RollbackSession::submit_remote_input(std::uint64_t frame, RollbackInput remote) {
    if (frame >= current_frame_) {
        remote_inputs_[frame] = remote;
        return Result<void>::success();
    }

    const auto depth64 = current_frame_ - frame;
    if (depth64 > max_rollback_frames_) {
        return Result<void>::failure(ErrorCode::invalid_argument, "remote input arrived outside the retained rollback window");
    }

    const auto frame_it = frames_.find(frame);
    if (frame_it == frames_.end()) {
        return Result<void>::failure(ErrorCode::invalid_argument, "rollback snapshot is not retained for corrected frame");
    }

    if (frame_it->second.remote_used == remote) {
        remote_inputs_[frame] = remote;
        frame_it->second.predicted_remote = false;
        return Result<void>::success();
    }

    const auto original_state = simulation_.save_state();
    const auto original_frames = frames_;
    const auto original_remote_inputs = remote_inputs_;
    const auto original_hashes = state_hashes_;
    const auto original_desync = desync_frame_;
    const auto original_telemetry = telemetry_;

    remote_inputs_[frame] = remote;
    const auto restored = simulation_.load_state(frame_it->second.snapshot_before);
    if (!restored) {
        (void)simulation_.load_state(original_state);
        remote_inputs_ = original_remote_inputs;
        return Result<void>::failure(restored.error, restored.detail);
    }

    for (std::uint64_t replay_frame = frame; replay_frame < current_frame_; ++replay_frame) {
        auto replay_it = frames_.find(replay_frame);
        if (replay_it == frames_.end()) {
            (void)simulation_.load_state(original_state);
            frames_ = original_frames;
            remote_inputs_ = original_remote_inputs;
            state_hashes_ = original_hashes;
            desync_frame_ = original_desync;
            telemetry_ = original_telemetry;
            return Result<void>::failure(ErrorCode::invalid_argument, "rollback history is incomplete for replay");
        }

        replay_it->second.snapshot_before = simulation_.save_state();
        replay_it->second.remote_used = remote_for_frame(replay_frame);
        replay_it->second.predicted_remote = !has_exact_remote(replay_frame);
        const auto stepped = simulation_.step_frame(
            replay_it->second.local,
            replay_it->second.remote_used,
            false);
        if (!stepped) {
            (void)simulation_.load_state(original_state);
            frames_ = original_frames;
            remote_inputs_ = original_remote_inputs;
            state_hashes_ = original_hashes;
            desync_frame_ = original_desync;
            telemetry_ = original_telemetry;
            return Result<void>::failure(stepped.error, stepped.detail);
        }
        state_hashes_[replay_frame] = hash_current_state();
    }

    const auto depth = static_cast<std::uint32_t>(depth64);
    telemetry_.last_rollback_depth = depth;
    telemetry_.max_rollback_depth = std::max(telemetry_.max_rollback_depth, depth);
    recompute_desync();
    return Result<void>::success();
}

Result<void> RollbackSession::submit_remote_hash(std::uint64_t frame, std::string hash_hex) {
    if (hash_hex.size() != 64 || !std::all_of(hash_hex.begin(), hash_hex.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        })) {
        return Result<void>::failure(ErrorCode::invalid_argument, "remote state hash must be 64 hexadecimal characters");
    }
    remote_hashes_[frame] = std::move(hash_hex);
    recompute_desync();
    return Result<void>::success();
}

Result<std::string> RollbackSession::state_hash(std::uint64_t frame) const {
    const auto it = state_hashes_.find(frame);
    if (it == state_hashes_.end()) {
        return Result<std::string>::failure(ErrorCode::file_not_found, "state hash is unavailable for requested frame");
    }
    return Result<std::string>::success(it->second);
}

void RollbackSession::prune_rollback_history() {
    const auto first_retained = current_frame_ > max_rollback_frames_
        ? current_frame_ - max_rollback_frames_
        : 0;
    auto it = frames_.begin();
    while (it != frames_.end() && it->first < first_retained) {
        it = frames_.erase(it);
    }
}

void RollbackSession::recompute_desync() noexcept {
    desync_frame_.reset();
    for (const auto& [frame, remote_hash] : remote_hashes_) {
        const auto local = state_hashes_.find(frame);
        if (local != state_hashes_.end() && local->second != remote_hash) {
            desync_frame_ = frame;
            break;
        }
    }
}

}
