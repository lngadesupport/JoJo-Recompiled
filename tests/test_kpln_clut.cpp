#include "content/kpln_clut.h"

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
    bytes[offset + 0u] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

} // namespace

int main() {
    std::vector<std::uint8_t> p0803(0x200u, 0u);
    std::vector<std::uint8_t> p0804(0x80u, 0u);
    std::vector<std::uint8_t> p0805(0x80u, 0u);
    std::vector<std::uint8_t> p0806(0x80u, 0u);
    std::vector<std::uint8_t> p0807(0x600u, 0u);

    put16(p0803, 0x000u, 0x001Fu);
    put16(p0803, 0x100u, 0x03E0u);
    put16(p0804, 0x000u, 0x7C00u);
    put16(p0804, 0x040u, 0x7FFFu);
    put16(p0805, 0x000u, 0x4210u);
    put16(p0806, 0x000u, 0x1234u);
    put16(p0807, 0x000u, 0x1111u);
    put16(p0807, 0x300u, 0x2222u);

    const auto parsed =
        jojo::content::build_kpln_clut_windows(
            p0803, p0804, p0805, p0806, p0807);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.palette_count == 2u);
    CHECK(parsed.value.windows.size() == 2u);
    CHECK(parsed.value.windows[0].width == 0x180u);
    CHECK(parsed.value.windows[0].height == 0x18u);

    const auto& p0 = parsed.value.windows[0];
    const auto& p1 = parsed.value.windows[1];

    CHECK(p0.at(0u, 0x1e8u) == 0x001Fu);
    CHECK(p0.at(0u, 0x1e9u) == 0x001Fu);
    CHECK(p1.at(0u, 0x1e8u) == 0x03E0u);
    CHECK(p0.at(0u, 0x1efu) == 0x7C00u);
    CHECK(p1.at(0u, 0x1efu) == 0x7FFFu);
    CHECK(p0.at(0x80u, 0x1e8u) == 0x4210u);
    CHECK(p0.at(0u, 0x1f1u) == 0x1234u);
    CHECK(p0.at(0u, 499u) == 0x1111u);
    CHECK(p0.at(0u, 500u) == 0x2222u);
    CHECK(p0.at(0u, 501u) == 0x1111u);
    CHECK(p0.at(0u, 502u) == 0x2222u);

    // CLUT selector 0 at row base 0x1e8 addresses the first 16 colors.
    CHECK(
        jojo::content::sample_kpln_clut(
            p0, 0x1e8u, 0u, 0u) ==
        0x001Fu);

    // Selector 8 starts at x=128 on the same row.
    CHECK(
        jojo::content::sample_kpln_clut(
            p0, 0x1e8u, 8u, 0u) ==
        0x4210u);

    std::cout << "KPLN CLUT layout tests passed\n";
    return 0;
}
