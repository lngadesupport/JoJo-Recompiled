#include "content/xa_adpcm_decoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace jojo::content {
namespace {

constexpr std::size_t kXaSectorPayloadBytes = 2304u;
constexpr std::size_t kXaGroupBytes = 128u;
constexpr std::size_t kXaGroupsPerSector = 18u;
constexpr std::size_t kSamplesPerUnit = 28u;

constexpr std::array<int, 4> kPositive{
    0, 60, 115, 98
};
constexpr std::array<int, 4> kNegative{
    0, 0, -52, -55
};

struct History {
    int old{};
    int older{};
};

int sign_extend_nibble(std::uint8_t value) noexcept {
    value &= 0x0Fu;
    return value >= 8u
        ? static_cast<int>(value) - 16
        : static_cast<int>(value);
}

std::int16_t clamp16(int value) noexcept {
    return static_cast<std::int16_t>(
        std::clamp(value, -32768, 32767));
}

Result<std::array<std::int16_t, kSamplesPerUnit>>
decode_unit(
    const std::uint8_t* group,
    std::size_t block,
    std::size_t nibble,
    History& history) {
    const auto header =
        group[4u + block * 2u + nibble];

    auto shift_code =
        static_cast<unsigned>(header & 0x0Fu);
    if (shift_code >= 13u) {
        // Reserved XA values 13..15 decode like shift code 9.
        shift_code = 9u;
    }
    const auto filter =
        static_cast<unsigned>((header >> 4u) & 0x03u);
    const int shift = 12 - static_cast<int>(shift_code);

    std::array<std::int16_t, kSamplesPerUnit> decoded{};
    for (std::size_t sample_index = 0u;
         sample_index < kSamplesPerUnit;
         ++sample_index) {
        const auto packed =
            group[16u + block + sample_index * 4u];
        const auto nibble_value =
            nibble == 0u
                ? static_cast<std::uint8_t>(packed & 0x0Fu)
                : static_cast<std::uint8_t>(packed >> 4u);

        const int source =
            sign_extend_nibble(nibble_value) << shift;
        const int predicted =
            (history.old * kPositive[filter] +
             history.older * kNegative[filter] +
             32) /
            64;
        const int reconstructed =
            source + predicted;
        const auto sample =
            clamp16(reconstructed);

        history.older = history.old;
        history.old = sample;
        decoded[sample_index] = sample;
    }

    return Result<std::array<std::int16_t, kSamplesPerUnit>>::success(
        decoded);
}

} // namespace

Result<XaDecodedPcm> decode_xa_adpcm(
    std::uint8_t coding,
    std::span<const std::uint8_t> payload) {
    if (payload.empty() ||
        payload.size() % kXaSectorPayloadBytes != 0u) {
        return Result<XaDecodedPcm>::failure(
            ErrorCode::invalid_argument,
            "XA ADPCM payload must contain whole 2304-byte sectors");
    }

    const auto channel_mode =
        static_cast<unsigned>(coding & 0x03u);
    if (channel_mode > 1u) {
        return Result<XaDecodedPcm>::failure(
            ErrorCode::unsupported_format,
            "XA ADPCM channel mode is reserved");
    }
    if ((coding & 0x30u) != 0u) {
        return Result<XaDecodedPcm>::failure(
            ErrorCode::unsupported_format,
            "XA 8-bit/reserved ADPCM coding is not supported");
    }

    XaDecodedPcm output{};
    output.channel_count =
        channel_mode == 1u ? 2u : 1u;
    output.sample_rate_hz =
        (coding & 0x04u) != 0u
            ? 18900u
            : 37800u;

    const auto sector_count =
        payload.size() / kXaSectorPayloadBytes;
    const std::size_t samples_per_sector =
        output.channel_count == 2u
            ? 2016u * 2u
            : 4032u;

    if (sector_count >
        std::numeric_limits<std::size_t>::max() /
            samples_per_sector) {
        return Result<XaDecodedPcm>::failure(
            ErrorCode::invalid_argument,
            "decoded XA sample count overflows");
    }

    output.samples.reserve(
        sector_count * samples_per_sector);

    History left{};
    History right{};

    for (std::size_t sector = 0u;
         sector < sector_count;
         ++sector) {
        const auto* sector_payload =
            payload.data() +
            sector * kXaSectorPayloadBytes;

        for (std::size_t group_index = 0u;
             group_index < kXaGroupsPerSector;
             ++group_index) {
            const auto* group =
                sector_payload +
                group_index * kXaGroupBytes;

            for (std::size_t block = 0u;
                 block < 4u;
                 ++block) {
                if (output.channel_count == 2u) {
                    const auto decoded_left =
                        decode_unit(
                            group, block, 0u, left);
                    if (!decoded_left) {
                        return Result<XaDecodedPcm>::failure(
                            decoded_left.error,
                            decoded_left.detail);
                    }
                    const auto decoded_right =
                        decode_unit(
                            group, block, 1u, right);
                    if (!decoded_right) {
                        return Result<XaDecodedPcm>::failure(
                            decoded_right.error,
                            decoded_right.detail);
                    }

                    for (std::size_t sample = 0u;
                         sample < kSamplesPerUnit;
                         ++sample) {
                        output.samples.push_back(
                            decoded_left.value[sample]);
                        output.samples.push_back(
                            decoded_right.value[sample]);
                    }
                } else {
                    for (std::size_t nibble = 0u;
                         nibble < 2u;
                         ++nibble) {
                        const auto decoded =
                            decode_unit(
                                group,
                                block,
                                nibble,
                                left);
                        if (!decoded) {
                            return Result<XaDecodedPcm>::failure(
                                decoded.error,
                                decoded.detail);
                        }
                        output.samples.insert(
                            output.samples.end(),
                            decoded.value.begin(),
                            decoded.value.end());
                    }
                }
            }
        }
    }

    return Result<XaDecodedPcm>::success(
        std::move(output));
}

} // namespace jojo::content
