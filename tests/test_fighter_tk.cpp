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

    for (std::size_t i = 0u;
         i < jojo::content::fighter_tk_slot_count;
         ++i) {
        put32(
            tkc,
            i * 4u,
            jojo::content::fighter_tkc_load_base +
                static_cast<std::uint32_t>(
                    0x6Cu + i * 4u));
        put32(
            tkd,
            i * 4u,
            static_cast<std::uint32_t>(
                0x6Cu + i * 4u));
    }
    put32(tkc, 4u, 0xFFFFFFFFu);

    const auto parsed =
        jojo::content::parse_fighter_tk_roots(tkc, tkd);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.slots.size() == 27u);
    CHECK(parsed.value.slots[0].tkc_offset == 0x6Cu);
    CHECK(parsed.value.slots[0].tkd_value == 0x6Cu);
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
