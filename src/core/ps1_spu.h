#pragma once

#include "core/r3000a_bus.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace jojo {

struct Ps1SpuAdpcmHistory {
    std::int32_t previous{};
    std::int32_t older{};
};

struct Ps1SpuDecodedBlock {
    std::array<std::int16_t, 28> samples{};
    std::uint8_t flags{};
};

struct Ps1SpuVoiceState {
    std::uint16_t volume_left{};
    std::uint16_t volume_right{};
    std::uint16_t pitch{};
    std::uint16_t start_address{};
    std::uint16_t adsr1{};
    std::uint16_t adsr2{};
    std::uint16_t adsr_volume{};
    std::uint16_t repeat_address{};
    bool keyed_on{};
    bool releasing{};
    std::uint32_t current_address{};
};

class Ps1Spu {
public:
    static constexpr std::size_t voice_count = 24u;
    static constexpr std::size_t sound_ram_size = 512u * 1024u;
    static constexpr std::uint32_t mmio_base = 0x1F801C00u;
    static constexpr std::uint32_t mmio_end = 0x1F801E7Fu;

    Ps1Spu();

    [[nodiscard]] static Ps1SpuDecodedBlock decode_adpcm_block(
        std::span<const std::uint8_t, 16> block,
        Ps1SpuAdpcmHistory& history) noexcept;

    [[nodiscard]] R3000aBusResult read16(std::uint32_t physical) noexcept;
    [[nodiscard]] R3000aBusResult write16(
        std::uint32_t physical,
        std::uint16_t value) noexcept;

    [[nodiscard]] bool dma_write_words(std::span<const std::uint32_t> words) noexcept;
    [[nodiscard]] bool dma_read_words(std::span<std::uint32_t> words) noexcept;

    void step(std::uint32_t cpu_cycles) noexcept;
    [[nodiscard]] std::vector<std::int16_t> drain_audio_samples();
    [[nodiscard]] std::uint64_t generated_sample_frames() const noexcept;
    [[nodiscard]] std::uint64_t nonzero_sample_count() const noexcept;

    [[nodiscard]] const Ps1SpuVoiceState& voice(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t endx_flags() const noexcept;
    [[nodiscard]] std::uint32_t transfer_current_address() const noexcept;
    [[nodiscard]] std::uint8_t sound_ram_byte(std::uint32_t address) const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

private:
    enum class EnvelopePhase : std::uint8_t {
        off,
        attack,
        decay,
        sustain,
        release,
    };

    struct VoiceRuntime {
        Ps1SpuAdpcmHistory history{};
        Ps1SpuDecodedBlock decoded{};
        std::size_t sample_index{28u};
        bool block_loaded{};
        std::uint32_t pitch_accumulator{};
        std::int32_t current_sample{};
        EnvelopePhase envelope_phase{EnvelopePhase::off};
        std::uint32_t envelope_counter{};
    };

    [[nodiscard]] bool decode_voice_register(
        std::uint32_t physical,
        std::size_t& voice_index,
        std::uint32_t& offset) const noexcept;
    [[nodiscard]] std::uint16_t read_flag_half(
        std::uint32_t flags,
        bool high) const noexcept;
    void write_flag_half(
        std::uint32_t& flags,
        bool high,
        std::uint16_t value) noexcept;
    void key_on(bool high, std::uint16_t mask) noexcept;
    void key_off(bool high, std::uint16_t mask) noexcept;
    void write_sound_ram16(std::uint16_t value) noexcept;
    [[nodiscard]] std::uint16_t read_sound_ram16() noexcept;
    [[nodiscard]] bool decode_voice_block(std::size_t voice_index) noexcept;
    [[nodiscard]] bool advance_voice_sample(std::size_t voice_index) noexcept;
    void mix_sample_frame() noexcept;
    void step_envelope(std::size_t voice_index) noexcept;
    void apply_envelope_rate(
        Ps1SpuVoiceState& voice_state,
        VoiceRuntime& runtime,
        std::uint32_t shift,
        std::uint32_t step_value,
        bool exponential,
        bool decreasing) noexcept;
    [[nodiscard]] static std::int32_t fixed_volume_gain(std::uint16_t value) noexcept;
    [[nodiscard]] static std::int32_t apply_gain(
        std::int32_t sample,
        std::int32_t gain) noexcept;

    std::array<Ps1SpuVoiceState, voice_count> voices_{};
    std::array<VoiceRuntime, voice_count> voice_runtime_{};
    std::vector<std::uint8_t> sound_ram_;

    std::uint16_t main_volume_left_{};
    std::uint16_t main_volume_right_{};
    std::uint16_t reverb_volume_left_{};
    std::uint16_t reverb_volume_right_{};
    std::uint32_t key_on_latch_{};
    std::uint32_t key_off_latch_{};
    std::uint32_t pitch_modulation_flags_{};
    std::uint32_t noise_flags_{};
    std::uint32_t reverb_flags_{};
    std::uint32_t endx_flags_{};
    std::uint16_t reverb_work_start_{};
    std::uint16_t irq_address_{};
    std::uint16_t transfer_address_{};
    std::uint32_t transfer_current_address_{};
    std::uint16_t control_{};
    std::uint16_t transfer_control_{};
    std::uint16_t status_{};
    std::uint16_t cd_volume_left_{};
    std::uint16_t cd_volume_right_{};
    std::uint16_t external_volume_left_{};
    std::uint16_t external_volume_right_{};
    std::array<std::uint16_t, 32> reverb_registers_{};
    std::uint32_t sample_cycle_accumulator_{};
    std::uint64_t generated_sample_frames_{};
    std::uint64_t nonzero_sample_count_{};
    std::vector<std::int16_t> audio_samples_{};
};

} // namespace jojo
