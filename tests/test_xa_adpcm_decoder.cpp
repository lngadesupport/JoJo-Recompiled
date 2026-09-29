#include "content/xa_adpcm_decoder.h"

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

} // namespace

int main() {
    // One full XA Form-2 audio payload: 18 groups x 128 bytes.
    std::vector<std::uint8_t> payload(2304u, 0u);

    // 4-bit stereo, filter 0, shift-code 0 => signed nibble << 12.
    // Header bytes for block 0: left then right.
    payload[4u] = 0x00u;
    payload[5u] = 0x00u;

    // First packed sample word: low nibble = left 1, high nibble = right 2.
    payload[16u] = 0x21u;

    const auto decoded =
        jojo::content::decode_xa_adpcm(
            0x01u,
            payload);
    CHECK(static_cast<bool>(decoded));
    CHECK(decoded.value.sample_rate_hz == 37800u);
    CHECK(decoded.value.channel_count == 2u);
    CHECK(decoded.value.samples.size() == 2016u * 2u);
    CHECK(decoded.value.samples[0] == 4096);
    CHECK(decoded.value.samples[1] == 8192);

    // Mono uses both nibbles sequentially on the same channel.
    const auto mono =
        jojo::content::decode_xa_adpcm(
            0x00u,
            payload);
    CHECK(static_cast<bool>(mono));
    CHECK(mono.value.channel_count == 1u);
    CHECK(mono.value.samples.size() == 4032u);
    CHECK(mono.value.samples[0] == 4096);
    CHECK(mono.value.samples[28] == 8192);

    // Retail migration currently targets normal 4-bit XA, not XA 8-bit.
    CHECK(!jojo::content::decode_xa_adpcm(
        0x10u,
        payload));

    std::cout << "XA ADPCM decoder tests passed\n";
    return 0;
}
