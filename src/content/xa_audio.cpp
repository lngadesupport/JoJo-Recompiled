#include "content/xa_audio.h"

#include <algorithm>

namespace jojo::content {
namespace {

std::uint32_t sample_rate_from_coding(
    std::uint8_t coding) noexcept {
    return (coding & 0x04u) != 0u
        ? 18900u
        : 37800u;
}

std::uint32_t channel_count_from_coding(
    std::uint8_t coding) noexcept {
    return (coding & 0x01u) != 0u
        ? 2u
        : 1u;
}

XaChannelStream* find_stream(
    std::vector<XaChannelStream>& streams,
    std::uint8_t channel) {
    const auto it = std::find_if(
        streams.begin(),
        streams.end(),
        [channel](const XaChannelStream& stream) {
            return stream.channel == channel;
        });
    return it == streams.end() ? nullptr : &*it;
}

} // namespace

Result<XaAudioLayout> parse_xa_audio_sectors(
    std::span<const std::uint8_t> raw_sectors) {
    if (raw_sectors.size() % xa_raw_sector_size != 0u) {
        return Result<XaAudioLayout>::failure(
            ErrorCode::invalid_installation,
            "raw XA input is not a whole number of 2352-byte sectors");
    }

    XaAudioLayout result{};
    result.total_sectors =
        static_cast<std::uint32_t>(
            raw_sectors.size() / xa_raw_sector_size);

    for (std::uint32_t sector_index = 0u;
         sector_index < result.total_sectors;
         ++sector_index) {
        const auto* sector =
            raw_sectors.data() +
            static_cast<std::size_t>(sector_index) *
                xa_raw_sector_size;

        if (sector[15] != 2u) {
            return Result<XaAudioLayout>::failure(
                ErrorCode::invalid_installation,
                "XA source contains a non-Mode-2 sector");
        }

        // Mode-2 repeats the four-byte subheader twice.
        if (!std::equal(
                sector + 16u,
                sector + 20u,
                sector + 20u)) {
            return Result<XaAudioLayout>::failure(
                ErrorCode::invalid_installation,
                "XA Mode-2 subheader copies differ");
        }

        const auto channel = sector[17];
        const auto submode = sector[18];
        const auto coding = sector[19];

        // Audio + Form2. Data/padding sectors inside retail XA files are
        // intentionally skipped instead of entering the native audio stream.
        if ((submode & 0x24u) != 0x24u) {
            ++result.skipped_non_audio_sectors;
            continue;
        }

        auto* stream = find_stream(
            result.streams, channel);
        if (!stream) {
            XaChannelStream created{};
            created.channel = channel;
            created.coding = coding;
            created.sample_rate_hz =
                sample_rate_from_coding(coding);
            created.channel_count =
                channel_count_from_coding(coding);
            result.streams.push_back(
                std::move(created));
            stream = &result.streams.back();
        } else if (stream->coding != coding) {
            return Result<XaAudioLayout>::failure(
                ErrorCode::invalid_installation,
                "XA channel changes coding mode inside one source file");
        }

        stream->packets.push_back({
            sector_index,
            submode,
            (submode & 0x80u) != 0u,
        });
        const auto* payload =
            sector + xa_audio_payload_offset;
        stream->adpcm_payload.insert(
            stream->adpcm_payload.end(),
            payload,
            payload + xa_audio_payload_size);
        ++result.audio_sectors;
    }

    std::sort(
        result.streams.begin(),
        result.streams.end(),
        [](const XaChannelStream& lhs,
           const XaChannelStream& rhs) {
            return lhs.channel < rhs.channel;
        });

    return Result<XaAudioLayout>::success(
        std::move(result));
}

} // namespace jojo::content
