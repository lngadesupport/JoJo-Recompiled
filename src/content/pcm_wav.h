#pragma once

#include "core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

[[nodiscard]] Result<std::vector<std::uint8_t>> encode_pcm16_wav(
    std::uint32_t sample_rate_hz,
    std::uint16_t channel_count,
    std::span<const std::int16_t> interleaved_samples);

} // namespace jojo::content
