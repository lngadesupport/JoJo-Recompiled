#include "content/hit_table.h"

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
    std::int16_t value) {
    const auto raw =
        static_cast<std::uint16_t>(value);
    bytes[offset] =
        static_cast<std::uint8_t>(raw);
    bytes[offset + 1u] =
        static_cast<std::uint8_t>(raw >> 8u);
}

} // namespace

int main() {
    std::vector<std::uint8_t> bytes(4096u, 0u);
    put16(bytes, 8u + 0u, -20);
    put16(bytes, 8u + 2u, 24);
    put16(bytes, 8u + 4u, 95);
    put16(bytes, 8u + 6u, 16);

    put16(bytes, 16u + 0u, -28);
    put16(bytes, 16u + 2u, 47);
    put16(bytes, 16u + 4u, 30);
    put16(bytes, 16u + 6u, 67);

    const auto parsed =
        jojo::content::parse_hit_table(bytes);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.records.size() == 512u);
    CHECK(parsed.value.nonzero_records == 2u);
    CHECK(parsed.value.records[0].empty());
    CHECK(parsed.value.records[1].a == -20);
    CHECK(parsed.value.records[1].b == 24);
    CHECK(parsed.value.records[1].c == 95);
    CHECK(parsed.value.records[1].d == 16);
    CHECK(parsed.value.records[2].a == -28);

    bytes.resize(4095u);
    CHECK(!jojo::content::parse_hit_table(bytes));

    std::cout << "HIT table tests passed\n";
    return 0;
}
