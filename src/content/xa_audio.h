#pragma once

#include "core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

constexpr std::size_t xa_raw_sector_size = 2352u;
constexpr std::size_t xa_audio_payload_offset = 24u;
constexpr std::size_t xa_audio_payload_size = 2304u;

struct XaAudioPacket {
    std::uint32_t source_sector_index{};
    std::uint8_t submode{};
    bool end_of_file{};
};

struct XaChannelStream {
    std::uint8_t channel{};
    std::uint8_t coding{};
    std::uint32_t sample_rate_hz{};
    std::uint32_t channel_count{};
    std::vector<XaAudioPacket> packets;
    std::vector<std::uint8_t> adpcm_payload;
};

struct XaAudioLayout {
    std::uint32_t total_sectors{};
    std::uint32_t audio_sectors{};
    std::uint32_t skipped_non_audio_sectors{};
    std::vector<XaChannelStream> streams;
};

struct XaPcm16Audio {
    std::uint32_t sample_rate_hz{};
    std::uint32_t channel_count{};
    std::vector<std::int16_t> samples;
};

[[nodiscard]] Result<XaAudioLayout> parse_xa_audio_sectors(
    std::span<const std::uint8_t> raw_sectors);

[[nodiscard]] Result<XaPcm16Audio> decode_xa_adpcm_pcm16(
    const XaChannelStream& stream);

} // namespace jojo::content
