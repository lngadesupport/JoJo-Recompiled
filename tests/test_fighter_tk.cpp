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
    std::vector<std::uint8_t> tkd(0x200u, 0u);

    // All slots share one small pointer list in this synthetic fixture.
    for (std::size_t i = 0u;
         i < jojo::content::fighter_tk_slot_count;
         ++i) {
        put32(
            tkc,
            i * 4u,
            jojo::content::fighter_tkc_load_base + 0x80u);
        put32(
            tkd,
            i * 4u,
            static_cast<std::uint32_t>(
                0x6Cu + i * 4u));
    }
    put32(tkc, 4u, 0xFFFFFFFFu);

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

    const auto parsed =
        jojo::content::parse_fighter_tk_roots(tkc, tkd);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.slots.size() == 27u);
    CHECK(parsed.value.slots[0].tkc_offset == 0x80u);
    CHECK(parsed.value.slots[0].tkd_value == 0x6Cu);
    CHECK(parsed.value.slots[0].tkc_records.size() == 1u);
    CHECK(parsed.value.slots[0].tkc_records[0].source_offset == 0x90u);
    CHECK(parsed.value.slots[0].tkc_records[0].fields[0] == 0x828Au);
    CHECK(parsed.value.slots[0].tkc_records[0].fields[1] == 0x0055u);
    CHECK(parsed.value.slots[0].tkc_records[0].fields[3] == 0x0001u);
    CHECK(parsed.value.slots[0].tkc_records[0].fields[4] == 0x0002u);
    CHECK(parsed.value.slots[1].tkc_null);
    CHECK(parsed.value.tkc_size == tkc.size());
    CHECK(parsed.value.tkd_size == tkd.size());

    put32(
        tkc,
        0u,
        jojo::content::fighter_tkc_load_base +
            static_cast<std::uint32_t>(
                tkc.size() + 4u));
    CHECK(!jojo::content::parse_fighter_tk_roots(tkc, tkd));

    std::cout << "fighter TK root tests passed\n";
    return 0;
}
