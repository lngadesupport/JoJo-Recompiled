#pragma once

#include "core/game_source_binding.h"
#include "core/ps1_boot_runtime.h"
#include "core/ps1_commercial_frontier.h"
#include "core/ps1_disc_session.h"
#include "core/result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
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

struct Ps1CommercialEvidenceReport {
    GameSourceBinding source{};
    Ps1CommercialFrontierClass frontier{Ps1CommercialFrontierClass::none};
    Ps1BootReport boot{};
    std::uint64_t total_instructions_retired{};
    std::uint32_t execution_segments{};
    std::optional<Ps1CommercialFrameEvidence> first_frame{};
    std::vector<Ps1CommercialDiagnosticDecision> diagnostic_decisions;
};

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
    void signal_vblank() noexcept;

    [[nodiscard]] const Ps1DiscSession& disc_session() const noexcept;
    [[nodiscard]] Ps1DisplayFrame display_frame() const;
    [[nodiscard]] Ps1GpuDisplayState gpu_display_state() const noexcept;
    [[nodiscard]] std::vector<std::int16_t> drain_audio_samples();
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
