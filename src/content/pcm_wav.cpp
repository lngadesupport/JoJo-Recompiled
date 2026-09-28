#include "content/pcm_wav.h"

#include <limits>

namespace jojo::content {
namespace {

void put16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void put32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

} // namespace

Result<std::vector<std::uint8_t>> encode_pcm16_wav(
    std::uint32_t sample_rate_hz,
    std::uint16_t channel_count,
    std::span<const std::int16_t> interleaved_samples) {
    if (sample_rate_hz == 0u ||
        (channel_count != 1u && channel_count != 2u)) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_argument,
            "PCM WAV requires a non-zero sample rate and mono/stereo channels");
    }
    if (interleaved_samples.size() % channel_count != 0u) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_argument,
            "PCM WAV sample count is not aligned to the channel count");
    }

    constexpr std::size_t kHeaderBytes = 44u;
    const auto data_bytes_64 =
        static_cast<std::uint64_t>(interleaved_samples.size()) * 2u;
    if (data_bytes_64 >
            std::numeric_limits<std::uint32_t>::max() - 36u ||
        data_bytes_64 >
            std::numeric_limits<std::size_t>::max() - kHeaderBytes) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_argument,
            "PCM WAV payload is too large for RIFF/WAVE");
    }

    const auto data_bytes =
        static_cast<std::uint32_t>(data_bytes_64);
    const auto block_align =
        static_cast<std::uint16_t>(channel_count * 2u);
    const auto byte_rate_64 =
        static_cast<std::uint64_t>(sample_rate_hz) * block_align;
    if (byte_rate_64 > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_argument,
            "PCM WAV byte rate overflows");
    }

    std::vector<std::uint8_t> bytes(
        kHeaderBytes + static_cast<std::size_t>(data_bytes),
        0u);

    bytes[0] = 'R'; bytes[1] = 'I'; bytes[2] = 'F'; bytes[3] = 'F';
    put32(bytes, 4u, 36u + data_bytes);
    bytes[8] = 'W'; bytes[9] = 'A'; bytes[10] = 'V'; bytes[11] = 'E';

    bytes[12] = 'f'; bytes[13] = 'm'; bytes[14] = 't'; bytes[15] = ' ';
    put32(bytes, 16u, 16u);
    put16(bytes, 20u, 1u);
    put16(bytes, 22u, channel_count);
    put32(bytes, 24u, sample_rate_hz);
    put32(bytes, 28u, static_cast<std::uint32_t>(byte_rate_64));
    put16(bytes, 32u, block_align);
    put16(bytes, 34u, 16u);

    bytes[36] = 'd'; bytes[37] = 'a'; bytes[38] = 't'; bytes[39] = 'a';
    put32(bytes, 40u, data_bytes);

    for (std::size_t i = 0u; i < interleaved_samples.size(); ++i) {
        put16(
            bytes,
            kHeaderBytes + i * 2u,
            static_cast<std::uint16_t>(interleaved_samples[i]));
    }

    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

} // namespace jojo::content
