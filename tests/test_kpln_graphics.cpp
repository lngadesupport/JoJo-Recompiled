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

    std::cout << "KPLN 0x0202 indexed page tests passed\n";
    return 0;
}
