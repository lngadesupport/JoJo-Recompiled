#pragma once

#include <cstdint>
#include <vector>

namespace jojo {

class Ps1GpuIngress;

struct Ps1DisplayFrame {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint32_t> rgba8;
};

void capture_ps1_display_frame_into(
    const Ps1GpuIngress& gpu,
    Ps1DisplayFrame& frame);
[[nodiscard]] Ps1DisplayFrame capture_ps1_display_frame(const Ps1GpuIngress& gpu);

} // namespace jojo
