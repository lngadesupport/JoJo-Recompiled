#include "content/xa_audio.h"

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

void make_sector(
    std::vector<std::uint8_t>& bytes,
    std::size_t index,
    std::uint8_t channel,
    std::uint8_t submode,
    std::uint8_t coding,
    std::uint8_t fill) {
    auto* sector =
        bytes.data() +
        index * jojo::content::xa_raw_sector_size;
    sector[15] = 2u;
    sector[16] = 1u;
    sector[17] = channel;
    sector[18] = submode;
    sector[19] = coding;
    sector[20] = 1u;
    sector[21] = channel;
    sector[22] = submode;
    sector[23] = coding;
    for (std::size_t i = 0u;
         i < jojo::content::xa_audio_payload_size;
         ++i) {
        sector[
            jojo::content::xa_audio_payload_offset + i] =
            fill;
    }
}

} // namespace

int main() {
    std::vector<std::uint8_t> sectors(
        jojo::content::xa_raw_sector_size * 4u,
        0u);
    make_sector(sectors, 0u, 0u, 0x64u, 0x01u, 0x11u);
    make_sector(sectors, 1u, 1u, 0x64u, 0x01u, 0x22u);
    make_sector(sectors, 2u, 0u, 0xE4u, 0x01u, 0x33u);
    make_sector(sectors, 3u, 0u, 0x00u, 0x00u, 0x44u);

    const auto parsed =
        jojo::content::parse_xa_audio_sectors(sectors);
    CHECK(static_cast<bool>(parsed));
    CHECK(parsed.value.total_sectors == 4u);
    CHECK(parsed.value.audio_sectors == 3u);
    CHECK(parsed.value.skipped_non_audio_sectors == 1u);
    CHECK(parsed.value.streams.size() == 2u);
    CHECK(parsed.value.streams[0].channel == 0u);
    CHECK(parsed.value.streams[0].sample_rate_hz == 37800u);
    CHECK(parsed.value.streams[0].channel_count == 2u);
    CHECK(parsed.value.streams[0].packets.size() == 2u);
    CHECK(parsed.value.streams[0].packets[1].end_of_file);
    CHECK(
        parsed.value.streams[0].adpcm_payload.size() ==
        jojo::content::xa_audio_payload_size * 2u);
    CHECK(parsed.value.streams[0].adpcm_payload.front() == 0x11u);
    CHECK(
        parsed.value.streams[0].adpcm_payload[
            jojo::content::xa_audio_payload_size] ==
        0x33u);
    CHECK(parsed.value.streams[1].channel == 1u);

    std::cout << "XA audio sector tests passed\n";
    return 0;
}
