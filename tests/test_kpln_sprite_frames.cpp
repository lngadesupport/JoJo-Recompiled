#include "content/kpln_sprite_frames.h"

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
    std::uint16_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
}

void put32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    put16(bytes, offset, static_cast<std::uint16_t>(value));
    put16(bytes, offset + 2u, static_cast<std::uint16_t>(value >> 16u));
}

} // namespace

int main() {
    // 0x0800 direct frame: one 2x1 part. The matrix begins at word 12
    // (byte 24), after one live record plus a zero-marker sentinel record.
    std::vector<std::uint8_t> direct(28u, 0u);
    put16(direct, 0u, 12u);
    put16(direct, 2u, 0x0102u);
    put16(direct, 4u, 4u);
    put16(direct, 6u, 8u);
    put16(direct, 8u, 1u);
    put16(direct, 10u, 0u);
    put16(direct, 24u, 0x1234u);
    put16(direct, 26u, 0xFFFFu);

    const auto parsed_direct =
        jojo::content::parse_kpln_direct_frames_0800(direct);
    CHECK(static_cast<bool>(parsed_direct));
    CHECK(parsed_direct.value.size() == 1u);
    CHECK(parsed_direct.value[0].source_record_index == 0u);
    CHECK(parsed_direct.value[0].parts.size() == 1u);
    CHECK(parsed_direct.value[0].parts[0].columns == 2u);
    CHECK(parsed_direct.value[0].parts[0].rows == 1u);
    CHECK(parsed_direct.value[0].parts[0].cells.size() == 2u);
    CHECK(parsed_direct.value[0].parts[0].cells[0].tile_word == 0x1234u);
    CHECK(parsed_direct.value[0].parts[0].cells[1].empty);

    // 0x0802 cached frame: one visible cell in a 2x1 matrix. Data begins
    // at dword 6 (byte 24): 4-byte mask then one descriptor dword.
    std::vector<std::uint8_t> cached(32u, 0u);
    put16(cached, 0u, 6u);
    put16(cached, 2u, 0x0102u);
    put16(cached, 4u, 5u);
    put16(cached, 6u, 9u);
    put16(cached, 8u, 1u);
    put16(cached, 10u, 0u);
    cached[24u] = 0x80u;

    const std::uint32_t descriptor =
        3u | (5u << 24u) | (2u << 30u);
    put32(cached, 28u, descriptor);

    // 0x0801 compressed tile stream at offset 3. One control byte marks all
    // eight tokens as compressed runs; each token emits 16 identical bytes.
    std::vector<std::uint8_t> tile_pool(32u, 0u);
    std::size_t cursor = 3u;
    tile_pool[cursor++] = 0xFFu;
    for (std::uint8_t run = 0u; run < 8u; ++run) {
        tile_pool[cursor++] = 0x0Fu;
        tile_pool[cursor++] =
            static_cast<std::uint8_t>(0x11u * (run + 1u));
    }

    const auto parsed_cached =
        jojo::content::parse_kpln_cached_frames_0802(
            cached, tile_pool);
    CHECK(static_cast<bool>(parsed_cached));
    CHECK(parsed_cached.value.frames.size() == 1u);
    CHECK(parsed_cached.value.frames[0].parts.size() == 1u);
    const auto& part = parsed_cached.value.frames[0].parts[0];
    CHECK(part.cells.size() == 2u);
    CHECK(part.cells[0].visible);
    CHECK(part.cells[0].has_descriptor);
    CHECK(part.cells[0].descriptor.stream_offset == 3u);
    CHECK(part.cells[0].descriptor.clut_selector == 5u);
    CHECK(part.cells[0].descriptor.transform == 2u);
    CHECK(part.cells[0].descriptor.tile_indices.size() == 256u);
    CHECK(part.cells[0].descriptor.tile_indices[0] == 1u);
    CHECK(part.cells[0].descriptor.tile_indices[1] == 1u);
    CHECK(part.cells[0].descriptor.tile_indices[32] == 2u);
    CHECK(!part.cells[1].visible);
    CHECK(parsed_cached.value.unique_tile_offsets.size() == 1u);
    CHECK(parsed_cached.value.unique_tile_offsets[0] == 3u);

    const auto raw_tile =
        jojo::content::decompress_kpln_tile_0801(
            std::span<const std::uint8_t>(tile_pool).subspan(3u));
    CHECK(static_cast<bool>(raw_tile));
    CHECK(raw_tile.value.bytes.size() == 128u);
    CHECK(raw_tile.value.bytes_consumed == 17u);

    // 0x0204 is arranged as 16-pixel-wide x 256-row strips, one strip per
    // 0x800 bytes. Two strips become a 32x256 native indexed surface.
    std::vector<std::uint8_t> strips(0x1000u, 0u);
    strips[0] = 0x21u;
    strips[0x800u] = 0x43u;
    const auto surface =
        jojo::content::parse_kpln_indexed_surface_0204(strips);
    CHECK(static_cast<bool>(surface));
    CHECK(surface.value.width == 32u);
    CHECK(surface.value.height == 256u);
    CHECK(surface.value.indices[0] == 1u);
    CHECK(surface.value.indices[1] == 2u);
    CHECK(surface.value.indices[16] == 3u);
    CHECK(surface.value.indices[17] == 4u);

    std::cout << "KPLN sprite frame tests passed\n";
    return 0;
}
