#pragma once

#include "core/ps1_commercial_evidence.h"
#include "core/ps1_timing.h"
#include "core/rollback.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <vector>

namespace jojo {

[[nodiscard]] RollbackInput ps1_rollback_input_from_active_low(
    std::uint16_t active_low_buttons) noexcept;

[[nodiscard]] std::uint16_t ps1_active_low_from_rollback_input(
    RollbackInput input) noexcept;

class Ps1RollbackSimulation final : public IRollbackSimulation {
public:
    explicit Ps1RollbackSimulation(
        Ps1CommercialEvidenceRunner& runner,
        std::uint32_t local_player_port,
        std::size_t snapshot_capacity = 96u) noexcept;

    [[nodiscard]] std::vector<std::uint8_t> save_state() const override;
    [[nodiscard]] std::vector<std::uint8_t> state_hash_material() const override;
    [[nodiscard]] Result<void> load_state(
        std::span<const std::uint8_t> state) override;
    [[nodiscard]] Result<void> step_frame(
        RollbackInput local,
        RollbackInput remote,
        bool emit_side_effects) override;

    [[nodiscard]] std::size_t retained_snapshot_count() const noexcept {
        return snapshots_.size();
    }

private:
    struct SnapshotRecord {
        Ps1BootRuntimeState runtime{};
        Ps1VideoReferenceClockState timing{};
    };

    [[nodiscard]] static std::vector<std::uint8_t> encode_snapshot_id(
        std::uint64_t id);
    [[nodiscard]] static Result<std::uint64_t> decode_snapshot_id(
        std::span<const std::uint8_t> bytes);
    void prune_snapshots() const;

    Ps1CommercialEvidenceRunner& runner_;
    std::uint32_t local_player_port_{};
    std::size_t snapshot_capacity_{96u};
    Ps1VideoReferenceClock timing_clock_{};
    mutable std::uint64_t next_snapshot_id_{1u};
    mutable std::map<std::uint64_t, SnapshotRecord> snapshots_{};
    mutable std::deque<std::uint64_t> snapshot_order_{};
};

} // namespace jojo
