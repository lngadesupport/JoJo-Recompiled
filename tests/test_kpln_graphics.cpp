#include "content/kpln_graphics.h"

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
    bytes[offset + 0u] =
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
    std::vector<std::uint8_t> page(
        1024u * 256u / 2u, 0u);
    page[0] = 0xA3u;
    page[1] = 0x51u;

    const auto parsed =
        jojo::content::parse_kpln_indexed_page_0202(page);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.width == 1024u);
    CHECK(parsed.value.height == 256u);
    CHECK(parsed.value.indices.size() == 1024u * 256u);
    CHECK(parsed.value.indices[0] == 3u);
    CHECK(parsed.value.indices[1] == 10u);
    CHECK(parsed.value.indices[2] == 1u);
    CHECK(parsed.value.indices[3] == 5u);

    page.resize(page.size() - 1u);
    CHECK(!jojo::content::parse_kpln_indexed_page_0202(page));

    // 0x0802: two 12-byte descriptors. The first block begins at dword 6,
    // exactly after the 24-byte descriptor table.
    std::vector<std::uint8_t> elements(48u, 0u);
    put16(elements, 0u, 6u);
    put16(elements, 2u, 0x0806u);
    put16(elements, 4u, 112u);
    put16(elements, 6u, 96u);
    put16(elements, 8u, 2u);
    put16(elements, 10u, 0u);

    put16(elements, 12u, 10u);
    put16(elements, 14u, 0x0404u);
    put16(elements, 16u, 32u);
    put16(elements, 18u, 16u);
    put16(elements, 20u, 1u);
    put16(elements, 22u, 0u);

    put32(elements, 24u, 0x00000003u);
    put32(elements, 28u, 0x00000000u);
    put32(elements, 32u, 0x12000005u);
    put32(elements, 36u, 0x00000020u);

    put32(elements, 40u, 0x00000001u);
    put32(elements, 44u, 0x41000040u);

    const auto parsed_elements =
        jojo::content::parse_kpln_element_table_0802(
            elements, 0x100u);
    CHECK(static_cast<bool>(parsed_elements));
    CHECK(parsed_elements.value.descriptor_count == 2u);
    CHECK(parsed_elements.value.first_block_dword_offset == 6u);
    CHECK(parsed_elements.value.records.size() == 2u);
    CHECK(parsed_elements.value.records[0].layout_width == 6u);
    CHECK(parsed_elements.value.records[0].layout_height == 8u);
    CHECK(parsed_elements.value.records[0].raw_field2 == 112u);
    CHECK(parsed_elements.value.records[0].raw_field3 == 96u);
    CHECK(parsed_elements.value.records[0].mask_dwords.size() == 2u);
    CHECK(parsed_elements.value.records[0].cells.size() == 2u);
    CHECK(parsed_elements.value.records[0].cells[0].source_offset == 5u);
    CHECK(parsed_elements.value.records[0].cells[0].flags == 0x12u);
    CHECK(parsed_elements.value.records[1].cells[0].source_offset == 0x40u);
    CHECK(parsed_elements.value.records[1].cells[0].flags == 0x41u);

    put32(elements, 44u, 0x41000100u);
    CHECK(!jojo::content::parse_kpln_element_table_0802(
        elements, 0x100u));

    std::cout << "KPLN graphics structure tests passed\n";
    return 0;
}
