#include "content/pcm_wav.h"

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

std::uint32_t le32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

std::uint16_t le16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u));
}

} // namespace

int main() {
    const std::vector<std::int16_t> samples{
        1, -2,
        32767, -32768,
    };

    const auto wav =
        jojo::content::encode_pcm16_wav(
            37800u,
            2u,
            samples);
    CHECK(static_cast<bool>(wav));
    CHECK(wav.value.size() == 52u);
    CHECK(
        std::string(
            reinterpret_cast<const char*>(wav.value.data()),
            4u) == "RIFF");
    CHECK(le32(wav.value, 4u) == 44u);
    CHECK(
        std::string(
            reinterpret_cast<const char*>(wav.value.data() + 8u),
            4u) == "WAVE");
    CHECK(le16(wav.value, 20u) == 1u);
    CHECK(le16(wav.value, 22u) == 2u);
    CHECK(le32(wav.value, 24u) == 37800u);
    CHECK(le32(wav.value, 28u) == 151200u);
    CHECK(le16(wav.value, 32u) == 4u);
    CHECK(le16(wav.value, 34u) == 16u);
    CHECK(le32(wav.value, 40u) == 8u);
    CHECK(le16(wav.value, 44u) == 1u);
    CHECK(le16(wav.value, 46u) == 0xFFFEu);
    CHECK(le16(wav.value, 48u) == 0x7FFFu);
    CHECK(le16(wav.value, 50u) == 0x8000u);

    CHECK(!jojo::content::encode_pcm16_wav(
        37800u, 0u, samples));
    CHECK(!jojo::content::encode_pcm16_wav(
        37800u, 3u, samples));

    std::cout << "PCM WAV tests passed\n";
    return 0;
}
