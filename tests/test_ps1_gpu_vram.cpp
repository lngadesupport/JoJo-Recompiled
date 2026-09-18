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

    // Drawing environment + GP0(60h): variable monochrome rectangle.
    // The draw offset is applied before clipping to the E3/E4 drawing area.
    {
        jojo::Ps1GpuIngress draw_gpu;
        CHECK(draw_gpu.write_gp0(0xE3000000u | 11u | (21u << 10u)).status ==
              jojo::R3000aBusStatus::ok); // top-left = (11,21)
        CHECK(draw_gpu.write_gp0(0xE4000000u | 13u | (22u << 10u)).status ==
              jojo::R3000aBusStatus::ok); // bottom-right = (13,22)
        CHECK(draw_gpu.write_gp0(0xE5000000u | 1u | (1u << 11u)).status ==
              jojo::R3000aBusStatus::ok); // offset = (+1,+1)

        CHECK(draw_gpu.write_gp0(0x600000F8u).status == jojo::R3000aBusStatus::ok);
        CHECK(draw_gpu.write_gp0((20u << 16u) | 10u).status == jojo::R3000aBusStatus::ok);
        CHECK(draw_gpu.write_gp0((3u << 16u) | 4u).status == jojo::R3000aBusStatus::ok);

        for (std::uint32_t y = 21u; y <= 22u; ++y) {
            for (std::uint32_t x = 11u; x <= 13u; ++x) {
                CHECK(draw_gpu.vram_pixel(x, y) == 0x001Fu);
            }
        }
        CHECK(draw_gpu.vram_pixel(10u, 20u) == 0u);
        CHECK(draw_gpu.vram_pixel(14u, 22u) == 0u);
        CHECK(draw_gpu.vram_write_count() == 6u);
    }

    // GP0(65h): raw-textured variable rectangle in 15-bit texture mode.
    // Texture source lives in VRAM and zero texels remain transparent.
    {
        jojo::Ps1GpuIngress sprite_gpu;

        CHECK(sprite_gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0((50u << 16u) | 100u).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0((2u << 16u) | 2u).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0(0x03E0001Fu).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0(0x7FFF7C00u).status == jojo::R3000aBusStatus::ok);

        CHECK(sprite_gpu.write_gp0(0xE1000100u).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0(0xE3000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0(0xE4000000u | 1023u | (511u << 10u)).status ==
              jojo::R3000aBusStatus::ok);

        const auto writes_before_sprite = sprite_gpu.vram_write_count();
        CHECK(sprite_gpu.write_gp0(0x65FFFFFFu).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0((30u << 16u) | 20u).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0((50u << 8u) | 100u).status == jojo::R3000aBusStatus::ok);
        CHECK(sprite_gpu.write_gp0((2u << 16u) | 2u).status == jojo::R3000aBusStatus::ok);

        CHECK(sprite_gpu.vram_pixel(20u, 30u) == 0x001Fu);
        CHECK(sprite_gpu.vram_pixel(21u, 30u) == 0x03E0u);
        CHECK(sprite_gpu.vram_pixel(20u, 31u) == 0x7C00u);
        CHECK(sprite_gpu.vram_pixel(21u, 31u) == 0x7FFFu);
        CHECK(sprite_gpu.vram_write_count() == writes_before_sprite + 4u);
    }

    // GP0(65h): raw 4bpp indexed sprite with CLUT. Index zero is
    // transparent; non-zero palette entries are copied exactly.
    {
        jojo::Ps1GpuIngress indexed_gpu;

        // One VRAM word carries four 4bpp texels: 1,2,0,1.
        CHECK(indexed_gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0((1u << 16u) | 1u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0(0x00001021u).status == jojo::R3000aBusStatus::ok);

        // CLUT at (16,1): transparent, red, green.
        CHECK(indexed_gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0((1u << 16u) | 16u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0((1u << 16u) | 3u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0(0x001F0000u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0(0x000003E0u).status == jojo::R3000aBusStatus::ok);

        CHECK(indexed_gpu.write_gp0(0xE1000000u).status == jojo::R3000aBusStatus::ok); // 4bpp
        CHECK(indexed_gpu.write_gp0(0xE3000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0(0xE4000000u | 1023u | (511u << 10u)).status ==
              jojo::R3000aBusStatus::ok);

        const auto writes_before_sprite = indexed_gpu.vram_write_count();
        CHECK(indexed_gpu.write_gp0(0x65FFFFFFu).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0((10u << 16u) | 10u).status == jojo::R3000aBusStatus::ok);
        const std::uint32_t clut = 1u | (1u << 6u);
        CHECK(indexed_gpu.write_gp0(clut << 16u).status == jojo::R3000aBusStatus::ok);
        CHECK(indexed_gpu.write_gp0((1u << 16u) | 4u).status == jojo::R3000aBusStatus::ok);

        CHECK(indexed_gpu.vram_pixel(10u, 10u) == 0x001Fu);
        CHECK(indexed_gpu.vram_pixel(11u, 10u) == 0x03E0u);
        CHECK(indexed_gpu.vram_pixel(12u, 10u) == 0x0000u);
        CHECK(indexed_gpu.vram_pixel(13u, 10u) == 0x001Fu);
        CHECK(indexed_gpu.vram_write_count() == writes_before_sprite + 3u);
    }

    // Raw-textured rectangle with 4-bit CLUT sampling.
    {
        jojo::Ps1GpuIngress clut_gpu;

        // CLUT at VRAM (0,60): index 1=red, 2=green, 3=blue, 4=white.
        CHECK(clut_gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0((60u << 16u) | 0u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0((1u << 16u) | 16u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0(0x001F0000u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0(0x7C0003E0u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0(0x00007FFFu).status == jojo::R3000aBusStatus::ok);
        for (int i = 0; i < 5; ++i) {
            CHECK(clut_gpu.write_gp0(0u).status == jojo::R3000aBusStatus::ok);
        }

        // Four 4-bit texel indices packed into one VRAM word: 1,2,3,4.
        CHECK(clut_gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0((100u << 16u) | 0u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0((1u << 16u) | 1u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0(0x00004321u).status == jojo::R3000aBusStatus::ok);

        CHECK(clut_gpu.write_gp0(0xE1000000u).status == jojo::R3000aBusStatus::ok); // 4-bit
        CHECK(clut_gpu.write_gp0(0xE3000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0(0xE4000000u | 1023u | (511u << 10u)).status ==
              jojo::R3000aBusStatus::ok);

        const std::uint32_t clut_id = 60u << 6u;
        CHECK(clut_gpu.write_gp0(0x65FFFFFFu).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0((40u << 16u) | 20u).status == jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0((clut_id << 16u) | (100u << 8u)).status ==
              jojo::R3000aBusStatus::ok);
        CHECK(clut_gpu.write_gp0((1u << 16u) | 4u).status == jojo::R3000aBusStatus::ok);

        CHECK(clut_gpu.vram_pixel(20u, 40u) == 0x001Fu);
        CHECK(clut_gpu.vram_pixel(21u, 40u) == 0x03E0u);
        CHECK(clut_gpu.vram_pixel(22u, 40u) == 0x7C00u);
        CHECK(clut_gpu.vram_pixel(23u, 40u) == 0x7FFFu);
    }

    // GP0(E2h) texture window + GP0(6Dh) raw-textured 1x1 sprite.
    // Mask bit 0 with offset bit 0 forces texture U bit 3 high: U=0 -> U=8.
    {
        jojo::Ps1GpuIngress window_gpu;

        CHECK(window_gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0((1u << 16u) | 9u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0x000003E0u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0x0000001Fu).status == jojo::R3000aBusStatus::ok);

        CHECK(window_gpu.write_gp0(0xE1000100u).status == jojo::R3000aBusStatus::ok); // 15bpp
        CHECK(window_gpu.write_gp0(0xE2000401u).status == jojo::R3000aBusStatus::ok); // maskX=1, offX=1
        CHECK(window_gpu.write_gp0(0xE3000000u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0xE4000000u | 1023u | (511u << 10u)).status ==
              jojo::R3000aBusStatus::ok);

        const auto writes_before = window_gpu.vram_write_count();
        CHECK(window_gpu.write_gp0(0x6DFFFFFFu).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0((70u << 16u) | 70u).status == jojo::R3000aBusStatus::ok);
        CHECK(window_gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);

        CHECK(window_gpu.vram_pixel(70u, 70u) == 0x001Fu);
        CHECK(window_gpu.vram_write_count() == writes_before + 1u);
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
