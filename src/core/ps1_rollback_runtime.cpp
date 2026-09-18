#include "core/ps1_rollback_runtime.h"

#include "core/ps1_commercial_frontier.h"
#include "core/ps1_timing.h"

#include <algorithm>
#include <array>
#include <string>

namespace jojo {
namespace {

constexpr std::array<std::uint8_t, 4> kSnapshotMagic{'J', 'R', 'S', '1'};

Ps1VideoTimingMode timing_mode_from_display(
    const Ps1GpuDisplayState& display) noexcept {
    if (display.pal) {
        return display.interlaced
            ? Ps1VideoTimingMode::pal_interlaced
            : Ps1VideoTimingMode::pal_non_interlaced;
    }
    return display.interlaced
        ? Ps1VideoTimingMode::ntsc_interlaced
        : Ps1VideoTimingMode::ntsc_non_interlaced;
}

} // namespace

RollbackInput ps1_rollback_input_from_active_low(
    std::uint16_t active_low_buttons) noexcept {
    return RollbackInput{
        static_cast<std::uint32_t>(
            static_cast<std::uint16_t>(~active_low_buttons)),
        0,
        0,
    };
}

std::uint16_t ps1_active_low_from_rollback_input(
    RollbackInput input) noexcept {
    return static_cast<std::uint16_t>(
        ~static_cast<std::uint16_t>(input.buttons));
}

Ps1RollbackSimulation::Ps1RollbackSimulation(
    Ps1CommercialEvidenceRunner& runner,
    std::uint32_t local_player_port,
    std::size_t snapshot_capacity) noexcept
    : runner_(runner),
      local_player_port_(local_player_port == 0u ? 0u : 1u),
      snapshot_capacity_(std::max<std::size_t>(snapshot_capacity, 16u)) {}

std::vector<std::uint8_t>
Ps1RollbackSimulation::encode_snapshot_id(std::uint64_t id) {
    std::vector<std::uint8_t> bytes(12u);
    std::copy(kSnapshotMagic.begin(), kSnapshotMagic.end(), bytes.begin());
    for (unsigned shift = 0u; shift < 64u; shift += 8u) {
        bytes[4u + shift / 8u] =
            static_cast<std::uint8_t>(id >> shift);
    }
    return bytes;
}

Result<std::uint64_t> Ps1RollbackSimulation::decode_snapshot_id(
    std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 12u ||
        !std::equal(
            kSnapshotMagic.begin(),
            kSnapshotMagic.end(),
            bytes.begin())) {
        return Result<std::uint64_t>::failure(
            ErrorCode::invalid_argument,
            "PS1 rollback snapshot token is invalid");
    }

    std::uint64_t id = 0u;
    for (unsigned shift = 0u; shift < 64u; shift += 8u) {
        id |= static_cast<std::uint64_t>(
                  bytes[4u + shift / 8u])
              << shift;
    }
    if (id == 0u) {
        return Result<std::uint64_t>::failure(
            ErrorCode::invalid_argument,
            "PS1 rollback snapshot token has a zero id");
    }
    return Result<std::uint64_t>::success(id);
}

void Ps1RollbackSimulation::prune_snapshots() const {
    while (snapshot_order_.size() > snapshot_capacity_) {
        const auto id = snapshot_order_.front();
        snapshot_order_.pop_front();
        snapshots_.erase(id);
    }
}

std::vector<std::uint8_t> Ps1RollbackSimulation::save_state() const {
    const auto id = next_snapshot_id_++;
    snapshots_[id] = SnapshotRecord{
        runner_.save_runtime_state(),
        timing_clock_.save_state(),
    };
    snapshot_order_.push_back(id);
    prune_snapshots();
    return encode_snapshot_id(id);
}

std::vector<std::uint8_t>
Ps1RollbackSimulation::state_hash_material() const {
    const auto runtime_hash = runner_.diagnostic_state_hash();
    const auto timing = timing_clock_.save_state();

    std::vector<std::uint8_t> bytes(17u);
    for (unsigned shift = 0u; shift < 64u; shift += 8u) {
        bytes[shift / 8u] =
            static_cast<std::uint8_t>(runtime_hash >> shift);
        bytes[9u + shift / 8u] =
            static_cast<std::uint8_t>(timing.remainder >> shift);
    }
    bytes[8u] = static_cast<std::uint8_t>(timing.mode);
    return bytes;
}

Result<void> Ps1RollbackSimulation::load_state(
    std::span<const std::uint8_t> state) {
    const auto decoded = decode_snapshot_id(state);
    if (!decoded) {
        return Result<void>::failure(decoded.error, decoded.detail);
    }
    const auto found = snapshots_.find(decoded.value);
    if (found == snapshots_.end()) {
        return Result<void>::failure(
            ErrorCode::file_not_found,
            "PS1 rollback snapshot is no longer retained");
    }

    const auto previous_timing = timing_clock_.save_state();
    if (!timing_clock_.load_state(found->second.timing)) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "PS1 rollback snapshot contains invalid video timing");
    }

    const auto restored = runner_.load_runtime_state(found->second.runtime);
    if (!restored) {
        (void)timing_clock_.load_state(previous_timing);
        return restored;
    }
    return Result<void>::success();
}

Result<void> Ps1RollbackSimulation::step_frame(
    RollbackInput local,
    RollbackInput remote,
    bool emit_side_effects) {
    const auto remote_player_port = 1u - local_player_port_;
    runner_.set_pad_buttons(
        local_player_port_,
        ps1_active_low_from_rollback_input(local));
    runner_.set_pad_buttons(
        remote_player_port,
        ps1_active_low_from_rollback_input(remote));

    const auto mode =
        timing_mode_from_display(runner_.gpu_display_state());
    timing_clock_.set_mode(mode);
    std::uint64_t remaining_ticks =
        timing_clock_.next_frame_ticks();

    while (remaining_ticks != 0u) {
        const auto slice_ticks =
            std::min<std::uint64_t>(remaining_ticks, 65536u);
        if (slice_ticks == 0u) {
            return Result<void>::failure(
                ErrorCode::backend_unavailable,
                "PS1 rollback frame produced an empty timing slice");
        }

        Ps1BootOptions options{};
        options.instruction_budget = slice_ticks;
        options.trace_capacity = 0u;
        options.mmio_event_capacity = 0u;
        options.bios_event_capacity = 0u;
        options.stagnation_instruction_limit = 0u;

        const auto segment = runner_.run_segment(options);
        const auto frontier =
            classify_ps1_commercial_frontier(segment);
        if (frontier !=
            Ps1CommercialFrontierClass::execution_budget) {
            return Result<void>::failure(
                ErrorCode::backend_unavailable,
                "PS1 rollback frame reached runtime frontier: " +
                    std::string(
                        ps1_commercial_frontier_class_name(frontier)));
        }

        if (segment.execution_steps == 0u ||
            segment.execution_steps > remaining_ticks) {
            return Result<void>::failure(
                ErrorCode::backend_unavailable,
                "PS1 rollback frame timing did not make progress");
        }
        remaining_ticks -= segment.execution_steps;
    }

    runner_.signal_vblank();

    if (!emit_side_effects) {
        (void)runner_.drain_audio_samples();
    }

    return Result<void>::success();
}

} // namespace jojo
