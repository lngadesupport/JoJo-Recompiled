#pragma once

#include "core/result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jojo::content {

struct XaDecodedPcm {
    std::uint32_t sample_rate_hz{};
    std::uint16_t channel_count{};
    std::vector<std::int16_t> samples;
};

[[nodiscard]] Result<XaDecodedPcm> decode_xa_adpcm(
    std::uint8_t coding,
    std::span<const std::uint8_t> payload);

} // namespace jojo::content
