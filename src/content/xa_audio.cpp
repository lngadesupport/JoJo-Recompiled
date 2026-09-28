#include "content/xa_audio.h"

#include <algorithm>
#include <array>
#include <limits>

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

struct XaPredictorHistory {
    std::int32_t sample1{};
    std::int32_t sample2{};
};

std::int16_t clip_pcm16(std::int32_t value) noexcept {
    value = std::clamp<std::int32_t>(
        value,
        std::numeric_limits<std::int16_t>::min(),
        std::numeric_limits<std::int16_t>::max());
    return static_cast<std::int16_t>(value);
}

std::int32_t sign_extend_4bit(std::uint8_t value) noexcept {
    value &= 0x0Fu;
    return (value & 0x08u) != 0u
        ? static_cast<std::int32_t>(value) - 16
        : static_cast<std::int32_t>(value);
}

std::array<std::int16_t, 28> decode_xa_sound_unit_4bit(
    const std::uint8_t* group,
    std::size_t block,
    std::size_t nibble,
    XaPredictorHistory& history) noexcept {
    static constexpr std::array<std::int32_t, 4> kFilter0{
        0, 60, 115, 98};
    static constexpr std::array<std::int32_t, 4> kFilter1{
        0, 0, -52, -55};

    const auto parameter =
        group[4u + block * 2u + nibble];
    auto range = static_cast<std::uint8_t>(
        parameter & 0x0Fu);
    if (range > 12u) {
        // Reserved 13..15 behave like range 9 on retail hardware.
        range = 9u;
    }
    const auto filter =
        static_cast<std::size_t>((parameter >> 4u) & 0x03u);
    const auto left_shift =
        static_cast<unsigned>(12u - range);

    std::array<std::int16_t, 28> decoded{};
    for (std::size_t sample = 0u;
         sample < decoded.size();
         ++sample) {
        const auto packed =
            group[16u + block + sample * 4u];
        const auto raw_nibble =
            static_cast<std::uint8_t>(
                (packed >> (nibble * 4u)) & 0x0Fu);
        const auto source =
            sign_extend_4bit(raw_nibble) << left_shift;
        const auto predicted =
            (history.sample1 * kFilter0[filter] +
             history.sample2 * kFilter1[filter] +
             32) >>
            6;
        const auto output =
            clip_pcm16(source + predicted);
        decoded[sample] = output;
        history.sample2 = history.sample1;
        history.sample1 = output;
    }
    return decoded;
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

Result<XaPcm16Audio> decode_xa_adpcm_pcm16(
    const XaChannelStream& stream) {
    if (stream.adpcm_payload.size() %
            xa_audio_payload_size !=
        0u) {
        return Result<XaPcm16Audio>::failure(
            ErrorCode::invalid_installation,
            "XA ADPCM payload is not a whole number of audio sectors");
    }

    const auto bits_mode =
        static_cast<std::uint8_t>(
            (stream.coding >> 4u) & 0x03u);
    if (bits_mode != 0u) {
        return Result<XaPcm16Audio>::failure(
            ErrorCode::unsupported_format,
            "native XA conversion currently requires retail 4-bit ADPCM");
    }

    const bool stereo =
        (stream.coding & 0x01u) != 0u;
    const auto expected_channels =
        stereo ? 2u : 1u;
    if (stream.channel_count != expected_channels) {
        return Result<XaPcm16Audio>::failure(
            ErrorCode::invalid_installation,
            "XA stream channel metadata disagrees with coding info");
    }

    XaPcm16Audio result{};
    result.sample_rate_hz =
        stream.sample_rate_hz != 0u
            ? stream.sample_rate_hz
            : sample_rate_from_coding(stream.coding);
    result.channel_count = expected_channels;

    const auto sector_count =
        stream.adpcm_payload.size() /
        xa_audio_payload_size;
    constexpr std::size_t kSamplesPerSector = 4032u;
    if (sector_count >
        std::numeric_limits<std::size_t>::max() /
            kSamplesPerSector) {
        return Result<XaPcm16Audio>::failure(
            ErrorCode::invalid_argument,
            "XA PCM output size overflows");
    }
    result.samples.reserve(
        sector_count * kSamplesPerSector);

    XaPredictorHistory left_history{};
    XaPredictorHistory right_history{};

    for (std::size_t sector = 0u;
         sector < sector_count;
         ++sector) {
        const auto* payload =
            stream.adpcm_payload.data() +
            sector * xa_audio_payload_size;

        for (std::size_t group_index = 0u;
             group_index < 18u;
             ++group_index) {
            const auto* group =
                payload + group_index * 128u;

            for (std::size_t block = 0u;
                 block < 4u;
                 ++block) {
                if (stereo) {
                    const auto left =
                        decode_xa_sound_unit_4bit(
                            group,
                            block,
                            0u,
                            left_history);
                    const auto right =
                        decode_xa_sound_unit_4bit(
                            group,
                            block,
                            1u,
                            right_history);
                    for (std::size_t sample = 0u;
                         sample < left.size();
                         ++sample) {
                        result.samples.push_back(
                            left[sample]);
                        result.samples.push_back(
                            right[sample]);
                    }
                } else {
                    const auto low =
                        decode_xa_sound_unit_4bit(
                            group,
                            block,
                            0u,
                            left_history);
                    result.samples.insert(
                        result.samples.end(),
                        low.begin(),
                        low.end());
                    const auto high =
                        decode_xa_sound_unit_4bit(
                            group,
                            block,
                            1u,
                            left_history);
                    result.samples.insert(
                        result.samples.end(),
                        high.begin(),
                        high.end());
                }
            }
        }
    }

    return Result<XaPcm16Audio>::success(
        std::move(result));
}

} // namespace jojo::content
