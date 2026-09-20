#include "content/fighter_overlay.h"

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

void put32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    bytes[offset + 0u] =
        static_cast<std::uint8_t>(value);
    bytes[offset + 1u] =
        static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 2u] =
        static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 3u] =
        static_cast<std::uint8_t>(value >> 24u);
}

} // namespace

int main() {
    std::vector<std::uint8_t> primary(0x100u, 0x55u);
    auto mirror = primary;

    const auto pointer =
        jojo::content::fighter_overlay_primary_base +
        0x80u;
    put32(primary, 0x10u, pointer);
    put32(
        mirror,
        0x10u,
        pointer +
            jojo::content::fighter_overlay_mirror_delta);

    // Simulate a separate code-immediate relocation that is not a direct
    // pointer word. The analyzer must keep it as a residual difference.
    mirror[0x40u] ^= 0x01u;

    const auto parsed =
        jojo::content::analyze_fighter_overlay_pair(
            primary, mirror);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.size_bytes == primary.size());
    CHECK(parsed.value.relocation_count == 1u);
    CHECK(parsed.value.relocations[0].field_offset == 0x10u);
    CHECK(parsed.value.relocations[0].target_offset == 0x80u);
    CHECK(parsed.value.residual_difference_bytes == 1u);

    mirror.resize(0x80u);
    CHECK(!jojo::content::analyze_fighter_overlay_pair(
        primary, mirror));

    std::cout << "fighter overlay relocation tests passed\n";
    return 0;
}
