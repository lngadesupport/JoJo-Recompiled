#include "content/kpln_graphics.h"

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
    std::size_t word,
    std::uint16_t value) {
    const auto offset = word * 2u;
    bytes[offset + 0u] =
        static_cast<std::uint8_t>(value);
    bytes[offset + 1u] =
        static_cast<std::uint8_t>(value >> 8u);
}

} // namespace

int main() {
    // Two six-word records; lists begin at word 12.
    std::vector<std::uint8_t> groups(18u * 2u, 0u);
    put16(groups, 0u, 12u);
    put16(groups, 1u, 0x0206u);
    put16(groups, 2u, 80u);
    put16(groups, 3u, 91u);
    put16(groups, 4u, 12u);
    put16(groups, 5u, 0u);

    put16(groups, 6u, 16u);
    put16(groups, 7u, 0x0305u);
    put16(groups, 8u, 46u);
    put16(groups, 9u, 106u);
    put16(groups, 10u, 13u);
    put16(groups, 11u, 0u);

    put16(groups, 12u, 0u);
    put16(groups, 13u, 1u);
    put16(groups, 14u, 2u);
    put16(groups, 15u, 0xFFFFu);
    put16(groups, 16u, 23u);
    put16(groups, 17u, 24u); // final EOF-terminated retail-style list

    const auto parsed_groups =
        jojo::content::parse_kpln_group_table_0800(groups);
    CHECK(static_cast<bool>(parsed_groups));
    CHECK(parsed_groups.value.records.size() == 2u);
    CHECK(parsed_groups.value.records[0].layout_low == 6u);
    CHECK(parsed_groups.value.records[0].layout_high == 2u);
    CHECK(parsed_groups.value.records[0].indices.size() == 3u);
    CHECK(parsed_groups.value.records[1].indices.size() == 2u);
    CHECK(parsed_groups.value.records[1].indices[0] == 23u);

    std::vector<std::uint8_t> palettes(64u, 0u);
    put16(palettes, 0u, 0x001Fu);
    put16(palettes, 16u, 0x03E0u);
    const auto parsed_palettes =
        jojo::content::parse_kpln_palette_bank(palettes);
    CHECK(static_cast<bool>(parsed_palettes));
    CHECK(parsed_palettes.value.palettes.size() == 2u);
    CHECK(parsed_palettes.value.palettes[0].bgr555[0] == 0x001Fu);
    CHECK(parsed_palettes.value.palettes[1].bgr555[0] == 0x03E0u);

    std::vector<std::uint8_t> page(
        1024u * 256u / 2u, 0u);
    page[0] = 0xA3u;
    const auto parsed_page =
        jojo::content::parse_kpln_indexed_page_0202(page);
    CHECK(static_cast<bool>(parsed_page));
    CHECK(parsed_page.value.width == 1024u);
    CHECK(parsed_page.value.height == 256u);
    CHECK(parsed_page.value.indices.size() == 1024u * 256u);
    CHECK(parsed_page.value.indices[0] == 3u);
    CHECK(parsed_page.value.indices[1] == 10u);

    page.resize(page.size() - 1u);
    CHECK(!jojo::content::parse_kpln_indexed_page_0202(page));

    std::cout << "KPLN graphics structure tests passed\n";
    return 0;
}
