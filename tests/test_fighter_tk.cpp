#include "content/fighter_tk.h"

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
    const auto raw = static_cast<std::uint16_t>(value);
    bytes[offset + 0u] =
        static_cast<std::uint8_t>(raw);
    bytes[offset + 1u] =
        static_cast<std::uint8_t>(raw >> 8u);
}

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
    std::vector<std::uint8_t> tkc(0x180u, 0u);
    std::vector<std::uint8_t> tkd(
        108u + 26u * 16u, 0u);

    // TKC: 26 roots plus a 27th end pointer.
    for (std::size_t i = 0u;
         i < jojo::content::fighter_tk_slot_count;
         ++i) {
        put32(
            tkc,
            i * 4u,
            jojo::content::fighter_tkc_load_base + 0x80u);
    }
    put32(
        tkc,
        jojo::content::fighter_tk_slot_count * 4u,
        jojo::content::fighter_tkc_load_base +
            static_cast<std::uint32_t>(tkc.size()));

    put32(
        tkc,
        0x80u,
        jojo::content::fighter_tkc_load_base + 0x90u);
    put32(tkc, 0x84u, 0xFFFFFFFFu);
    tkc[0x90u] = 0x8Au;
    tkc[0x91u] = 0x82u;
    tkc[0x92u] = 0x55u;
    tkc[0x93u] = 0x00u;
    tkc[0x94u] = 0x00u;
    tkc[0x95u] = 0x00u;
    tkc[0x96u] = 0x01u;
    tkc[0x97u] = 0x00u;
    tkc[0x98u] = 0x02u;
    tkc[0x99u] = 0x00u;

    // TKD: block size followed by 26 slot offsets.
    put32(tkd, 0u, 16u);
    for (std::size_t i = 0u;
         i < jojo::content::fighter_tk_slot_count;
         ++i) {
        const auto offset =
            static_cast<std::uint32_t>(
                108u + i * 16u);
        put32(tkd, (i + 1u) * 4u, offset);
        put16(tkd, offset + 0u, -20);
        put16(tkd, offset + 2u, 24);
        put16(tkd, offset + 4u, 95);
        put16(tkd, offset + 6u, 16);
        put16(tkd, offset + 8u, -28);
        put16(tkd, offset + 10u, 47);
        put16(tkd, offset + 12u, 30);
        put16(tkd, offset + 14u, 67);
    }

    const auto parsed =
        jojo::content::parse_fighter_tk_roots(tkc, tkd);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.slots.size() == 26u);
    CHECK(parsed.value.tkc_end_offset == tkc.size());
    CHECK(parsed.value.tkd_block_size == 16u);
    CHECK(parsed.value.slots[0].tkc_offset == 0x80u);
    CHECK(parsed.value.slots[0].tkc_records.size() == 1u);
    CHECK(parsed.value.slots[0].tkc_records[0].source_offset == 0x90u);
    CHECK(parsed.value.slots[0].tkc_records[0].fields[0] == 0x828Au);
    CHECK(parsed.value.slots[0].tkc_records[0].fields[1] == 0x0055u);
    CHECK(parsed.value.slots[0].tkc_records[0].fields[3] == 0x0001u);
    CHECK(parsed.value.slots[0].tkc_records[0].fields[4] == 0x0002u);
    CHECK(parsed.value.slots[0].tkd_offset == 108u);
    CHECK(parsed.value.slots[0].tkd_records.size() == 2u);
    CHECK(parsed.value.slots[0].tkd_records[0].fields[0] == -20);
    CHECK(parsed.value.slots[0].tkd_records[0].fields[2] == 95);
    CHECK(parsed.value.slots[0].tkd_records[1].fields[0] == -28);
    CHECK(parsed.value.slots[25].tkd_offset == 108u + 25u * 16u);

    put32(tkd, 0u, 10u);
    CHECK(!jojo::content::parse_fighter_tk_roots(tkc, tkd));

    std::cout << "fighter TK structure tests passed\n";
    return 0;
}
