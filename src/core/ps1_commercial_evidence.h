#pragma once

#include "core/game_source_binding.h"
#include "core/ps1_boot_runtime.h"
#include "core/ps1_commercial_frontier.h"
#include "core/ps1_disc_session.h"
#include "core/result.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace jojo {

struct Ps1CommercialFrameEvidence {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t frame_hash_fnv1a64{};
    std::uint64_t non_black_pixels{};
};

[[nodiscard]] std::optional<Ps1CommercialFrameEvidence>
make_ps1_commercial_frame_evidence(const Ps1DisplayFrame& frame) noexcept;

class Ps1CommercialFrameProgress {
public:
    void observe(const Ps1DisplayFrame& frame) noexcept;
    void reset() noexcept;

    [[nodiscard]] std::uint64_t observed_non_black_frames() const noexcept;
    [[nodiscard]] std::uint64_t frame_change_count() const noexcept;
    [[nodiscard]] const std::optional<Ps1CommercialFrameEvidence>&
    first_frame() const noexcept;

private:
    std::uint64_t observed_non_black_frames_{};
    std::uint64_t frame_change_count_{};
    std::optional<std::uint64_t> last_frame_hash_{};
    std::optional<Ps1CommercialFrameEvidence> first_frame_{};
};

struct Ps1CommercialDiagnosticDecision {
    std::uint32_t bios_table{};
    std::uint32_t bios_selector{};
    Ps1BiosFallback fallback{Ps1BiosFallback::return_zero};
};

struct Ps1CommercialEvidenceOptions {
    Ps1BootOptions boot{};
    std::uint32_t max_execution_segments{1u};
    std::vector<Ps1BiosFallback> diagnostic_bios_fallbacks;
};

enum class Ps1CommercialSessionTermination : std::uint8_t {
    bounded_run,
    frontier_stop,
    manual_stop,
    periodic_checkpoint,
};

[[nodiscard]] std::string_view ps1_commercial_session_termination_name(
    Ps1CommercialSessionTermination termination) noexcept;

struct Ps1CommercialRuntimeCounters {
    std::array<std::uint64_t, 2> pad_poll_count{};
    std::array<std::uint64_t, 2> pad_pressed_poll_count{};
    std::array<std::uint64_t, 2> memory_card_read_sector_count{};
    std::array<std::uint64_t, 2> memory_card_write_sector_count{};
    std::array<std::uint64_t, 2> memory_card_changed_write_sector_count{};
    std::uint64_t dma_transfer_count{};
    std::uint64_t cdrom_command_count{};
    std::uint64_t gpu_gp0_word_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t vram_write_count{};
    std::uint64_t vblank_count{};
    std::uint64_t spu_sample_frames{};
    std::uint64_t spu_nonzero_samples{};
};

struct Ps1CommercialEvidenceReport {
    GameSourceBinding source{};
    Ps1CommercialFrontierClass frontier{Ps1CommercialFrontierClass::none};
    Ps1CommercialSessionTermination session_termination{
        Ps1CommercialSessionTermination::bounded_run};
    Ps1BootReport boot{};
    std::uint64_t total_execution_steps{};
    std::uint64_t total_instructions_retired{};
    std::uint64_t total_native_x64_instructions_retired{};
    std::uint64_t total_reference_instructions_retired{};
    std::uint64_t total_native_x64_cache_compilations{};
    std::uint64_t total_native_x64_cache_reuses{};
    std::uint64_t total_native_x64_cache_invalidations{};
    std::uint64_t total_native_x64_cache_evictions{};
    std::uint32_t execution_segments{};
    std::uint64_t completed_frames{};
    std::uint64_t observed_non_black_frames{};
    std::uint64_t frame_change_count{};
    std::array<std::uint64_t, 2> pad_poll_count{};
    std::array<std::uint64_t, 2> pad_pressed_poll_count{};
    std::array<std::uint64_t, 2> memory_card_read_sector_count{};
    std::array<std::uint64_t, 2> memory_card_write_sector_count{};
    std::array<std::uint64_t, 2> memory_card_changed_write_sector_count{};
    std::uint64_t session_dma_transfer_count{};
    std::uint64_t session_cdrom_command_count{};
    std::uint64_t session_gpu_gp0_word_count{};
    std::uint64_t session_gpu_gp1_command_count{};
    std::uint64_t session_vram_write_count{};
    std::uint64_t session_vblank_count{};
    std::uint64_t spu_sample_frames{};
    std::uint64_t spu_nonzero_samples{};
    Ps1GpuDisplayState gpu_display{};
    std::uint64_t gpu_nonzero_vram_words{};
    std::uint64_t gpu_display_region_nonzero_words{};
    bool gpu_nonzero_bounds_valid{};
    std::uint32_t gpu_nonzero_min_x{};
    std::uint32_t gpu_nonzero_min_y{};
    std::uint32_t gpu_nonzero_max_x{};
    std::uint32_t gpu_nonzero_max_y{};
    std::optional<Ps1CommercialFrameEvidence> first_frame{};
    std::vector<Ps1CommercialDiagnosticDecision> diagnostic_decisions;
};

struct Ps1GameplayValidationSummary {
    bool frame_observed{};
    bool dynamic_video_observed{};
    bool controller_poll_observed{};
    bool controller_input_observed{};
    bool audio_non_silent_observed{};
    bool memory_card_read_observed{};
    bool memory_card_write_observed{};
    bool memory_card_content_change_observed{};
};

[[nodiscard]] Ps1GameplayValidationSummary
summarize_ps1_gameplay_validation(
    const Ps1CommercialEvidenceReport& report) noexcept;

class Ps1CommercialEvidenceRunner {
public:
    Ps1CommercialEvidenceRunner() = default;

    [[nodiscard]] static Result<Ps1CommercialEvidenceRunner> open(
        const std::filesystem::path& source,
        const Ps1DiscOpenOptions& open_options = {});

    [[nodiscard]] Ps1CommercialEvidenceReport run(
        const Ps1CommercialEvidenceOptions& options) noexcept;
    [[nodiscard]] Ps1BootReport run_segment(
        const Ps1BootOptions& options) noexcept;
    void set_native_x64_enabled(bool enabled) noexcept;
    [[nodiscard]] bool native_x64_enabled() const noexcept;
    void signal_vblank() noexcept;

    [[nodiscard]] const Ps1DiscSession& disc_session() const noexcept;
    [[nodiscard]] Ps1DisplayFrame display_frame() const;
    [[nodiscard]] Ps1GpuDisplayState gpu_display_state() const noexcept;
    [[nodiscard]] Ps1CommercialRuntimeCounters validation_counters() const noexcept;
    [[nodiscard]] std::vector<Ps1CdromCommandSummary>
    recent_cdrom_commands() const;
    [[nodiscard]] std::vector<std::int16_t> drain_audio_samples();
    [[nodiscard]] Ps1BootRuntimeState save_runtime_state() const;
    [[nodiscard]] Result<void> load_runtime_state(
        const Ps1BootRuntimeState& state);
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
    void set_pad_buttons(
        std::uint32_t port,
        std::uint16_t active_low_buttons) noexcept;
    [[nodiscard]] Result<void> load_or_create_memory_card(
        std::uint32_t port,
        const std::filesystem::path& path);
    [[nodiscard]] Result<void> flush_memory_cards();

private:
    Ps1DiscSession disc_{};
    Ps1BootRuntime runtime_{};
};

} // namespace jojo
