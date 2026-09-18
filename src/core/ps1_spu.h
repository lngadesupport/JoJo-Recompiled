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

    [[nodiscard]] const Ps1SpuVoiceState& voice(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t endx_flags() const noexcept;
    [[nodiscard]] std::uint32_t transfer_current_address() const noexcept;
    [[nodiscard]] std::uint8_t sound_ram_byte(std::uint32_t address) const noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

private:
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

    std::array<Ps1SpuVoiceState, voice_count> voices_{};
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
};

} // namespace jojo
