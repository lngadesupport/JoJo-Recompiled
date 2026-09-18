#include "core/ps1_gpu_ingress.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1GpuIngress gpu;

    // GP0(A0h): CPU -> VRAM image transfer. Three 16-bit pixels are packed
    // into two GP0 data words; the high half of the final word is padding.
    CHECK(gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp0((20u << 16u) | 10u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp0((1u << 16u) | 3u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp0(0x22221111u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp0(0xDEAD3333u).status == jojo::R3000aBusStatus::ok);

    CHECK(gpu.vram_pixel(10u, 20u) == 0x1111u);
    CHECK(gpu.vram_pixel(11u, 20u) == 0x2222u);
    CHECK(gpu.vram_pixel(12u, 20u) == 0x3333u);
    CHECK(gpu.vram_pixel(13u, 20u) == 0x0000u);
    CHECK(gpu.vram_write_count() == 3u);

    // The transfer must be complete; a subsequent ordinary GP0 NOP is parsed
    // as a command rather than being consumed as image payload.
    CHECK(gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.gp0_word_count() == 6u);

    // GP0(80h): VRAM -> VRAM copy. Source pixels are copied from a
    // snapshot so overlapping rectangles do not self-feed while writing.
    {
        jojo::Ps1GpuIngress copy_gpu;
        CHECK(copy_gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(copy_gpu.write_gp0((40u << 16u) | 20u).status == jojo::R3000aBusStatus::ok);
        CHECK(copy_gpu.write_gp0((1u << 16u) | 3u).status == jojo::R3000aBusStatus::ok);
        CHECK(copy_gpu.write_gp0(0x22221111u).status == jojo::R3000aBusStatus::ok);
        CHECK(copy_gpu.write_gp0(0x00003333u).status == jojo::R3000aBusStatus::ok);

        const auto before_copy_writes = copy_gpu.vram_write_count();
        CHECK(copy_gpu.write_gp0(0x80000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(copy_gpu.write_gp0((40u << 16u) | 20u).status == jojo::R3000aBusStatus::ok);
        CHECK(copy_gpu.write_gp0((42u << 16u) | 30u).status == jojo::R3000aBusStatus::ok);
        CHECK(copy_gpu.write_gp0((1u << 16u) | 3u).status == jojo::R3000aBusStatus::ok);

        CHECK(copy_gpu.vram_pixel(30u, 42u) == 0x1111u);
        CHECK(copy_gpu.vram_pixel(31u, 42u) == 0x2222u);
        CHECK(copy_gpu.vram_pixel(32u, 42u) == 0x3333u);
        CHECK(copy_gpu.vram_write_count() == before_copy_writes + 3u);
    }

    // GP0(02h): Fill Rectangle. Keep X/width aligned here so this test isolates
    // packet assembly, color conversion and deterministic in-bounds raster writes.
    {
        jojo::Ps1GpuIngress fill_gpu;
        CHECK(fill_gpu.write_gp0(0x020000F8u).status == jojo::R3000aBusStatus::ok); // red
        CHECK(fill_gpu.write_gp0((30u << 16u) | 32u).status == jojo::R3000aBusStatus::ok);
        CHECK(fill_gpu.write_gp0((2u << 16u) | 16u).status == jojo::R3000aBusStatus::ok);

        for (std::uint32_t y = 30u; y < 32u; ++y) {
            for (std::uint32_t x = 32u; x < 48u; ++x) {
                CHECK(fill_gpu.vram_pixel(x, y) == 0x001Fu);
            }
        }
        CHECK(fill_gpu.vram_pixel(31u, 30u) == 0u);
        CHECK(fill_gpu.vram_pixel(48u, 30u) == 0u);
        CHECK(fill_gpu.vram_write_count() == 32u);
        CHECK(fill_gpu.gp0_word_count() == 3u);
    }

    return failures ? 1 : 0;
}
