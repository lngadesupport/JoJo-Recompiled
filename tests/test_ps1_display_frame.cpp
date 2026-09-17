#include "core/ps1_display_frame.h"
#include "core/ps1_gpu_ingress.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1GpuIngress gpu;

    // Upload a 2x2 BGR555 image into VRAM at (4, 6).
    CHECK(gpu.write_gp0(0xA0000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp0((6u << 16u) | 4u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp0((2u << 16u) | 2u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp0(0x03E0001Fu).status == jojo::R3000aBusStatus::ok); // red, green
    CHECK(gpu.write_gp0(0x7FFF7C00u).status == jojo::R3000aBusStatus::ok); // blue, white

    // Display starts at that VRAM location. Mode 01h = 320x240, 15-bit,
    // non-interlaced NTSC. Display-enable parameter 0 enables output.
    CHECK(gpu.write_gp1(0x05001804u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp1(0x08000001u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.write_gp1(0x03000000u).status == jojo::R3000aBusStatus::ok);

    const auto state = gpu.display_state();
    CHECK(state.enabled);
    CHECK(state.start_x == 4u);
    CHECK(state.start_y == 6u);
    CHECK(state.width == 320u);
    CHECK(state.height == 240u);
    CHECK(!state.rgb24);

    const auto frame = jojo::capture_ps1_display_frame(gpu);
    CHECK(frame.width == 320u);
    CHECK(frame.height == 240u);
    CHECK(frame.rgba8.size() == 320u * 240u);
    CHECK(frame.rgba8[0] == 0xFF0000FFu); // RGBA bytes packed as 0xAABBGGRR on LE
    CHECK(frame.rgba8[1] == 0xFF00FF00u);
    CHECK(frame.rgba8[320u] == 0xFFFF0000u);
    CHECK(frame.rgba8[321u] == 0xFFFFFFFFu);

    return failures ? 1 : 0;
}
