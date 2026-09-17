#include "core/ps1_exe.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void expect_rejected(std::vector<std::uint8_t> bytes) {
    const auto parsed = jojo::parse_ps1_executable(bytes);
    CHECK(!parsed);
    if (!parsed) CHECK(parsed.error == jojo::ErrorCode::unsupported_format);
}

int main() {
    const auto fixture = test_ps1::make_psx_exe();
    const auto parsed = jojo::parse_ps1_executable(fixture);
    CHECK(parsed);
    if (parsed) {
        CHECK(parsed.value.metadata.entry_pc == 0x80010000u);
        CHECK(parsed.value.metadata.initial_gp == 0x80018000u);
        CHECK(parsed.value.metadata.text_load_address == 0x80010000u);
        CHECK(parsed.value.metadata.text_size == 16u);
        CHECK(parsed.value.metadata.stack_base == 0x801FFF00u);
        CHECK(parsed.value.metadata.stack_size == 0x100u);
        CHECK(parsed.value.metadata.fnv1a64_hex.size() == 16u);
        CHECK(std::all_of(parsed.value.metadata.fnv1a64_hex.begin(),
                          parsed.value.metadata.fnv1a64_hex.end(),
                          [](unsigned char ch) {
                              return std::isdigit(ch) || (ch >= 'a' && ch <= 'f');
                          }));
        CHECK(parsed.value.file_bytes == fixture);
    }

    auto wrong_magic = fixture;
    wrong_magic[0] = 'X';
    expect_rejected(std::move(wrong_magic));

    expect_rejected(std::vector<std::uint8_t>(0x7ffu, 0u));

    auto oversized_text = fixture;
    test_ps1::write_le32(oversized_text, 0x01C, 20u);
    expect_rejected(std::move(oversized_text));

    auto unaligned_entry = fixture;
    test_ps1::write_le32(unaligned_entry, 0x010, 0x80010002u);
    expect_rejected(std::move(unaligned_entry));

    auto unaligned_load = fixture;
    test_ps1::write_le32(unaligned_load, 0x018, 0x80010002u);
    expect_rejected(std::move(unaligned_load));

    auto unaligned_text = fixture;
    test_ps1::write_le32(unaligned_text, 0x01C, 15u);
    expect_rejected(std::move(unaligned_text));

    return failures ? 1 : 0;
}
