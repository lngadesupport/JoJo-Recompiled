#include "content/pac_archive.h"

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
    // Mirrors the discovered retail PAC layout:
    // header sector + sector-aligned variable payloads.
    std::vector<std::uint8_t> bytes(0x1800u, 0u);
    put32(bytes, 0u, 2u);
    put32(bytes, 4u, 0x1800u);
    put32(bytes, 8u, 0x0803u);
    put32(bytes, 12u, 0x0200u);
    put32(bytes, 16u, 0x0101u);
    put32(bytes, 20u, 0x0040u);

    for (std::size_t i = 0u; i < 0x200u; ++i) {
        bytes[0x800u + i] =
            static_cast<std::uint8_t>(i);
    }
    for (std::size_t i = 0u; i < 0x40u; ++i) {
        bytes[0x1000u + i] =
            static_cast<std::uint8_t>(0x80u + i);
    }

    const auto parsed =
        jojo::content::parse_pac_archive(bytes);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.size() == 2u);
    CHECK(parsed.value[0].type == 0x0803u);
    CHECK(parsed.value[0].bytes.size() == 0x200u);
    CHECK(parsed.value[1].type == 0x0101u);
    CHECK(parsed.value[1].bytes.size() == 0x40u);
    CHECK(parsed.value[0].bytes.front() == 0u);
    CHECK(parsed.value[1].bytes.front() == 0x80u);

    auto malformed = bytes;
    put32(malformed, 4u, 0x2000u);
    CHECK(!jojo::content::parse_pac_archive(malformed));

    std::cout << "PAC archive tests passed\n";
    return 0;
}
