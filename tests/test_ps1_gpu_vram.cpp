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

    return failures ? 1 : 0;
}
