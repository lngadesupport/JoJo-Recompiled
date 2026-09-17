#include "core/ps1_display_frame.h"

#include "core/ps1_gpu_ingress.h"

namespace jojo {
namespace {

std::uint8_t expand5(std::uint16_t value) noexcept {
    const auto v = static_cast<std::uint8_t>(value & 0x1Fu);
    return static_cast<std::uint8_t>((v << 3u) | (v >> 2u));
}

std::uint32_t bgr555_to_rgba8(std::uint16_t pixel) noexcept {
    const auto red = expand5(pixel);
    const auto green = expand5(pixel >> 5u);
    const auto blue = expand5(pixel >> 10u);
    return static_cast<std::uint32_t>(red) |
           (static_cast<std::uint32_t>(green) << 8u) |
           (static_cast<std::uint32_t>(blue) << 16u) |
           0xFF000000u;
}

} // namespace

Ps1DisplayFrame capture_ps1_display_frame(const Ps1GpuIngress& gpu) {
    const auto state = gpu.display_state();
    Ps1DisplayFrame frame{};
    if (!state.enabled || state.rgb24 || state.width == 0u || state.height == 0u) {
        return frame;
    }

    frame.width = state.width;
    frame.height = state.height;
    frame.rgba8.resize(static_cast<std::size_t>(frame.width) * frame.height);

    for (std::uint32_t y = 0u; y < frame.height; ++y) {
        const auto source_y = (state.start_y + y) & (Ps1GpuIngress::vram_height - 1u);
        for (std::uint32_t x = 0u; x < frame.width; ++x) {
            const auto source_x = (state.start_x + x) & (Ps1GpuIngress::vram_width - 1u);
            frame.rgba8[static_cast<std::size_t>(y) * frame.width + x] =
                bgr555_to_rgba8(gpu.vram_pixel(source_x, source_y));
        }
    }
    return frame;
}

} // namespace jojo
