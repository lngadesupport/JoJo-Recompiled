#include "core/ps1_gpu.h"

#include <algorithm>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1Gpu gpu;

    CHECK(gpu.vram().size() ==
          static_cast<std::size_t>(jojo::Ps1Gpu::vram_width) * jojo::Ps1Gpu::vram_height);
    CHECK(std::all_of(gpu.vram().begin(), gpu.vram().end(),
                      [](std::uint16_t pixel) { return pixel == 0u; }));
    CHECK(gpu.vram_write_count() == 0u);

    // GP0(02h) Quick Rectangle Fill is a three-word packet. The first two
    // words must not mutate VRAM before the packet is complete.
    CHECK(gpu.write_gp0(0x020000FFu).status == jojo::R3000aBusStatus::ok); // red
    CHECK(gpu.vram_write_count() == 0u);
    CHECK(gpu.write_gp0((8u << 16u) | 16u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.vram_write_count() == 0u);
    CHECK(gpu.write_gp0((2u << 16u) | 16u).status == jojo::R3000aBusStatus::ok);

    // 24-bit RGB 0x0000FF maps to PS1 15-bit red 0x001F. Use an already
    // 16-pixel-aligned rectangle so this contract is independent of the
    // command's horizontal rounding rules.
    CHECK(gpu.vram_write_count() == 32u);
    for (std::uint32_t y = 8u; y < 10u; ++y) {
        for (std::uint32_t x = 16u; x < 32u; ++x) {
            const auto index = static_cast<std::size_t>(y) * jojo::Ps1Gpu::vram_width + x;
            CHECK(gpu.vram()[index] == 0x001Fu);
        }
    }
    CHECK(gpu.vram()[8u * jojo::Ps1Gpu::vram_width + 15u] == 0u);
    CHECK(gpu.vram()[8u * jojo::Ps1Gpu::vram_width + 32u] == 0u);
    CHECK(gpu.vram()[7u * jojo::Ps1Gpu::vram_width + 16u] == 0u);
    CHECK(gpu.vram()[10u * jojo::Ps1Gpu::vram_width + 16u] == 0u);

    const auto writes_before = gpu.vram_write_count();
    const auto unsupported = gpu.write_gp0(0x20000000u);
    CHECK(unsupported.status == jojo::R3000aBusStatus::unsupported);
    CHECK(gpu.last_unsupported_command().has_value());
    CHECK(gpu.last_unsupported_command() && *gpu.last_unsupported_command() == 0x20u);
    CHECK(gpu.vram_write_count() == writes_before);

    return failures ? 1 : 0;
}
