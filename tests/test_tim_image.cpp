#include "content/tim_image.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "CHECK failed at line "
              << line << ": " << expression << "\n";
    std::exit(1);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

void put16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    bytes[offset] =
        static_cast<std::uint8_t>(value);
    bytes[offset + 1u] =
        static_cast<std::uint8_t>(value >> 8u);
}

void put32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    put16(
        bytes,
        offset,
        static_cast<std::uint16_t>(value));
    put16(
        bytes,
        offset + 2u,
        static_cast<std::uint16_t>(value >> 16u));
}

} // namespace

int main() {
    // 4bpp TIM: two 16-color palettes, 8x2 pixels.
    std::vector<std::uint8_t> tim(2048u, 0u);
    put32(tim, 0u, 0x10u);
    put32(tim, 4u, 0x08u);

    put32(tim, 8u, 12u + 32u * 2u);
    put16(tim, 12u, 0u);
    put16(tim, 14u, 480u);
    put16(tim, 16u, 32u);
    put16(tim, 18u, 1u);

    // palette 0: transparent, red, green...
    put16(tim, 20u + 0u * 2u, 0x0000u);
    put16(tim, 20u + 1u * 2u, 0x001Fu);
    put16(tim, 20u + 2u * 2u, 0x03E0u);
    // palette 1: transparent, blue, white...
    put16(tim, 20u + 16u * 2u + 0u * 2u, 0x0000u);
    put16(tim, 20u + 16u * 2u + 1u * 2u, 0x7C00u);
    put16(tim, 20u + 16u * 2u + 2u * 2u, 0x7FFFu);

    constexpr std::size_t image = 84u;
    put32(tim, image + 0u, 12u + 8u);
    put16(tim, image + 4u, 512u);
    put16(tim, image + 6u, 0u);
    put16(tim, image + 8u, 2u);
    put16(tim, image + 10u, 2u);

    // 16 pixels: 0,1,2,1 repeated.
    for (std::size_t i = 0u; i < 8u; ++i) {
        const auto lo =
            static_cast<std::uint8_t>(
                (i % 2u) == 0u ? 0u : 2u);
        const auto hi = 1u;
        tim[image + 12u + i] =
            static_cast<std::uint8_t>(
                lo | (hi << 4u));
    }

    const auto decoded =
        jojo::content::decode_sector_aligned_tim_images(tim);
    CHECK(static_cast<bool>(decoded));
    CHECK(decoded.value.size() == 2u);
    CHECK(decoded.value[0].width == 8u);
    CHECK(decoded.value[0].height == 2u);
    CHECK(decoded.value[0].palette_count == 2u);
    CHECK(decoded.value[0].rgba8.size() == 16u);
    CHECK((decoded.value[0].rgba8[0] >> 24u) == 0u);
    CHECK((decoded.value[0].rgba8[1] & 0xFFu) == 255u);
    CHECK(((decoded.value[1].rgba8[1] >> 16u) & 0xFFu) == 255u);

    // False sector-aligned TIM signature inside heterogeneous PAC data:
    // magic/flags look valid but the following block length is impossible.
    std::vector<std::uint8_t> mixed(4096u, 0u);
    put32(mixed, 0u, 0x10u);
    put32(mixed, 4u, 0x08u);
    put32(mixed, 8u, 0xFFFFFFF0u);
    const auto scanned_false =
        jojo::content::decode_sector_aligned_tim_images(mixed);
    CHECK(static_cast<bool>(scanned_false));
    CHECK(scanned_false.value.empty());

    std::cout << "TIM decoder tests passed\n";
    return 0;
}
