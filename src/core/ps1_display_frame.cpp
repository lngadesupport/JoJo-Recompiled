#include "core/ps1_display_frame.h"

#include "core/ps1_gpu_ingress.h"

#include <algorithm>
#include <array>

namespace jojo {
namespace {

const std::array<std::uint32_t, 32768>& bgr555_rgba_lut() noexcept {
    static const std::array<std::uint32_t, 32768> table = [] {
        std::array<std::uint32_t, 32768> out{};
        for (std::uint32_t pixel = 0u; pixel < out.size(); ++pixel) {
            const auto expand5 = [](std::uint32_t value) noexcept {
                value &= 0x1Fu;
                return static_cast<std::uint8_t>(
                    (value << 3u) | (value >> 2u));
            };
            const auto red = expand5(pixel);
            const auto green = expand5(pixel >> 5u);
            const auto blue = expand5(pixel >> 10u);
            out[pixel] =
                static_cast<std::uint32_t>(red) |
                (static_cast<std::uint32_t>(green) << 8u) |
                (static_cast<std::uint32_t>(blue) << 16u) |
                0xFF000000u;
        }
        return out;
    }();
    return table;
}

} // namespace

void capture_ps1_display_frame_into(
    const Ps1GpuIngress& gpu,
    Ps1DisplayFrame& frame) {
    const auto state = gpu.display_state();
    if (!state.enabled || state.rgb24 ||
        state.width == 0u || state.height == 0u) {
        frame.width = 0u;
        frame.height = 0u;
        frame.rgba8.clear();
        return;
    }

    frame.width = state.width;
    frame.height = state.height;
    frame.rgba8.resize(
        static_cast<std::size_t>(frame.width) * frame.height);

    const auto vram = gpu.vram_pixels();
    const auto& lut = bgr555_rgba_lut();
    const auto start_x =
        state.start_x & (Ps1GpuIngress::vram_width - 1u);
    const auto first_run = std::min<std::uint32_t>(
        frame.width,
        Ps1GpuIngress::vram_width - start_x);

    for (std::uint32_t y = 0u; y < frame.height; ++y) {
        const auto source_y =
            (state.start_y + y) &
            (Ps1GpuIngress::vram_height - 1u);
        const auto* row =
            vram.data() +
            static_cast<std::size_t>(source_y) *
                Ps1GpuIngress::vram_width;
        auto* destination =
            frame.rgba8.data() +
            static_cast<std::size_t>(y) * frame.width;

        for (std::uint32_t x = 0u; x < first_run; ++x) {
            destination[x] =
                lut[row[start_x + x] & 0x7FFFu];
        }
        for (std::uint32_t x = first_run; x < frame.width; ++x) {
            destination[x] =
                lut[row[x - first_run] & 0x7FFFu];
        }
    }
}

Ps1DisplayFrame capture_ps1_display_frame(const Ps1GpuIngress& gpu) {
    Ps1DisplayFrame frame{};
    capture_ps1_display_frame_into(gpu, frame);
    return frame;
}

} // namespace jojo
