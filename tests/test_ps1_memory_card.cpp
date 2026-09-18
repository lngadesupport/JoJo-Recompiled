#include "core/ps1_memory_card.h"
#include "core/ps1_sio0.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static std::uint8_t checksum_frame(const std::array<std::uint8_t, 128>& frame) {
    std::uint8_t value = 0u;
    for (std::size_t i = 0; i < 127u; ++i) value ^= frame[i];
    return value;
}

static std::uint8_t exchange(jojo::Ps1Sio0& sio, std::uint8_t value) {
    CHECK(sio.write8(0x1F801040u, value).status == jojo::R3000aBusStatus::ok);
    const auto result = sio.read8(0x1F801040u);
    CHECK(result.status == jojo::R3000aBusStatus::ok);
    return static_cast<std::uint8_t>(result.value);
}

static void select_port0(jojo::Ps1Sio0& sio) {
    CHECK(sio.write16(0x1F80104Au, 0x0043u).status == jojo::R3000aBusStatus::ok);
    CHECK(sio.write16(0x1F80104Au, 0x0003u).status == jojo::R3000aBusStatus::ok);
}

static void test_blank_card_format_and_persistence() {
    jojo::Ps1MemoryCard card;
    CHECK(card.size() == 128u * 1024u);
    CHECK(card.byte(0u) == 'M');
    CHECK(card.byte(1u) == 'C');
    CHECK(card.byte(127u) == 0x0Eu);

    const auto directory = card.read_sector(1u);
    CHECK(directory.has_value());
    if (directory) {
        CHECK((*directory)[0] == 0xA0u);
        CHECK((*directory)[8] == 0xFFu);
        CHECK((*directory)[9] == 0xFFu);
        CHECK((*directory)[127] == checksum_frame(*directory));
    }

    const auto broken = card.read_sector(16u);
    CHECK(broken.has_value());
    if (broken) {
        CHECK((*broken)[0] == 0xFFu);
        CHECK((*broken)[1] == 0xFFu);
        CHECK((*broken)[2] == 0xFFu);
        CHECK((*broken)[3] == 0xFFu);
        CHECK((*broken)[127] == checksum_frame(*broken));
    }

    std::array<std::uint8_t, 128> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i ^ 0x5Au);
    CHECK(card.write_sector(100u, payload));
    CHECK(card.dirty());
    CHECK(card.flag_byte() == 0u);

    const auto root = std::filesystem::temp_directory_path() / "jojo-ps1-memory-card-tests";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto path = root / "card1.mcr";
    card.set_backing_path(path);
    CHECK(card.flush());
    CHECK(!card.dirty());
    CHECK(std::filesystem::file_size(path) == jojo::Ps1MemoryCard::raw_size);

    auto loaded = jojo::Ps1MemoryCard::load(path);
    CHECK(loaded);
    if (loaded) {
        const auto round_trip = loaded.value.read_sector(100u);
        CHECK(round_trip.has_value());
        if (round_trip) CHECK(*round_trip == payload);
        CHECK(loaded.value.flag_byte() == 0x08u);
    }
    std::filesystem::remove_all(root, ec);
}

static void test_sio0_memory_card_read_and_write_protocol() {
    jojo::Ps1Sio0 sio;
    select_port0(sio);

    auto& card = sio.memory_card(0u);
    std::array<std::uint8_t, 128> sector{};
    for (std::size_t i = 0; i < sector.size(); ++i) sector[i] = static_cast<std::uint8_t>(i);
    CHECK(card.write_sector(2u, sector));

    CHECK(exchange(sio, 0x81u) == 0xFFu);
    CHECK(exchange(sio, 0x52u) == 0x00u);
    CHECK(exchange(sio, 0x00u) == 0x5Au);
    CHECK(exchange(sio, 0x00u) == 0x5Du);
    CHECK(exchange(sio, 0x00u) == 0x00u);
    CHECK(exchange(sio, 0x02u) == 0x00u);
    CHECK(exchange(sio, 0x00u) == 0x5Cu);
    CHECK(exchange(sio, 0x00u) == 0x5Du);
    CHECK(exchange(sio, 0x00u) == 0x00u);
    CHECK(exchange(sio, 0x00u) == 0x02u);

    std::uint8_t checksum = 0x02u;
    for (std::size_t i = 0; i < sector.size(); ++i) {
        const auto value = exchange(sio, 0x00u);
        CHECK(value == sector[i]);
        checksum ^= value;
    }
    CHECK(exchange(sio, 0x00u) == checksum);
    CHECK(exchange(sio, 0x00u) == 0x47u);
    CHECK(sio.memory_card_read_sector_count(0u) == 1u);
    CHECK(sio.memory_card_write_sector_count(0u) == 0u);

    select_port0(sio);
    std::array<std::uint8_t, 128> replacement{};
    replacement.fill(0xA5u);
    std::uint8_t write_checksum = 0x03u;

    CHECK(exchange(sio, 0x81u) == 0xFFu);
    CHECK(exchange(sio, 0x57u) == 0x00u);
    CHECK(exchange(sio, 0x00u) == 0x5Au);
    CHECK(exchange(sio, 0x00u) == 0x5Du);
    CHECK(exchange(sio, 0x00u) == 0x00u);
    CHECK(exchange(sio, 0x03u) == 0x00u);
    for (const auto value : replacement) {
        (void)exchange(sio, value);
        write_checksum ^= value;
    }
    (void)exchange(sio, write_checksum);
    CHECK(exchange(sio, 0x00u) == 0x5Cu);
    CHECK(exchange(sio, 0x00u) == 0x5Du);
    CHECK(exchange(sio, 0x00u) == 0x47u);
    CHECK(sio.memory_card_read_sector_count(0u) == 1u);
    CHECK(sio.memory_card_write_sector_count(0u) == 1u);

    const auto written = card.read_sector(3u);
    CHECK(written.has_value());
    if (written) CHECK(*written == replacement);
}

int main() {
    test_blank_card_format_and_persistence();
    test_sio0_memory_card_read_and_write_protocol();
    return failures ? 1 : 0;
}
