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
    std::size_t snapshot_capacity) noexcept
    : runner_(runner),
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
    snapshots_[id] = runner_.save_runtime_state();
    snapshot_order_.push_back(id);
    prune_snapshots();
    return encode_snapshot_id(id);
}

std::vector<std::uint8_t>
Ps1RollbackSimulation::state_hash_material() const {
    const auto hash = runner_.diagnostic_state_hash();
    std::vector<std::uint8_t> bytes(8u);
    for (unsigned shift = 0u; shift < 64u; shift += 8u) {
        bytes[shift / 8u] =
            static_cast<std::uint8_t>(hash >> shift);
    }
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
    return runner_.load_runtime_state(found->second);
}

Result<void> Ps1RollbackSimulation::step_frame(
    RollbackInput local,
    RollbackInput remote,
    bool emit_side_effects) {
    runner_.set_pad_buttons(
        0u, ps1_active_low_from_rollback_input(local));
    runner_.set_pad_buttons(
        1u, ps1_active_low_from_rollback_input(remote));

    Ps1FrameSliceBudget budget{65536u};
    const auto mode =
        timing_mode_from_display(runner_.gpu_display_state());
    (void)budget.begin_frame(mode);

    while (!budget.frame_complete()) {
        const auto slice_ticks = budget.next_slice_ticks();
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
            !budget.consume(segment.execution_steps)) {
            return Result<void>::failure(
                ErrorCode::backend_unavailable,
                "PS1 rollback frame timing did not make progress");
        }
    }

    runner_.signal_vblank();

    if (!emit_side_effects) {
        (void)runner_.drain_audio_samples();
    }

    return Result<void>::success();
}

} // namespace jojo
