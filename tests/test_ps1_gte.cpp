#include "core/ps1_gte.h"

#include <cstdint>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

constexpr std::uint32_t pack_s16(std::int16_t lo, std::int16_t hi) noexcept {
    return static_cast<std::uint16_t>(lo) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(hi)) << 16u);
}

constexpr std::uint32_t command(
    std::uint8_t op,
    bool sf = false,
    bool lm = false,
    std::uint8_t mx = 0u,
    std::uint8_t v = 0u,
    std::uint8_t cv = 0u) noexcept {
    return (0x12u << 26u) | (0x10u << 21u) |
           (static_cast<std::uint32_t>(sf) << 19u) |
           (static_cast<std::uint32_t>(mx & 3u) << 17u) |
           (static_cast<std::uint32_t>(v & 3u) << 15u) |
           (static_cast<std::uint32_t>(cv & 3u) << 13u) |
           (static_cast<std::uint32_t>(lm) << 10u) |
           (op & 0x3Fu);
}

void set_identity_rotation(jojo::R3000aGte& gte) {
    gte.control[0] = pack_s16(0x1000, 0);
    gte.control[1] = pack_s16(0, 0);
    gte.control[2] = pack_s16(0x1000, 0);
    gte.control[3] = pack_s16(0, 0);
    gte.control[4] = 0x00001000u;
}

void test_register_side_effects() {
    jojo::R3000aGte gte{};

    jojo::write_ps1_gte_data(gte, 28u, 0x00007C1Fu);
    CHECK(jojo::read_ps1_gte_data(gte, 9u) == 0x00000F80u);
    CHECK(jojo::read_ps1_gte_data(gte, 10u) == 0x00000000u);
    CHECK(jojo::read_ps1_gte_data(gte, 11u) == 0x00000F80u);
    CHECK(jojo::read_ps1_gte_data(gte, 29u) == 0x00007C1Fu);

    jojo::write_ps1_gte_data(gte, 30u, 0x00F00000u);
    CHECK(jojo::read_ps1_gte_data(gte, 31u) == 8u);
    jojo::write_ps1_gte_data(gte, 30u, 0xFFF00000u);
    CHECK(jojo::read_ps1_gte_data(gte, 31u) == 12u);

    jojo::write_ps1_gte_data(gte, 15u, pack_s16(7, 9));
    CHECK(gte.data[12] == 0u);
    CHECK(gte.data[13] == 0u);
    CHECK(gte.data[14] == pack_s16(7, 9));
    CHECK(jojo::read_ps1_gte_data(gte, 15u) == pack_s16(7, 9));
}

void test_nclip_and_average_z() {
    jojo::R3000aGte gte{};
    gte.data[12] = pack_s16(0, 0);
    gte.data[13] = pack_s16(100, 0);
    gte.data[14] = pack_s16(0, 100);

    CHECK(jojo::execute_ps1_gte_command(gte, command(0x06u)) ==
          jojo::Ps1GteCommandStatus::ok);
    CHECK(static_cast<std::int32_t>(gte.data[24]) == 10000);

    gte.data[17] = 100u;
    gte.data[18] = 200u;
    gte.data[19] = 300u;
    gte.control[29] = 0x00001000u;
    CHECK(jojo::execute_ps1_gte_command(gte, command(0x2Du)) ==
          jojo::Ps1GteCommandStatus::ok);
    CHECK(gte.data[24] == 600u * 0x1000u);
    CHECK(gte.data[7] == 600u);

    gte.data[16] = 50u;
    gte.control[30] = 0x00000800u;
    CHECK(jojo::execute_ps1_gte_command(gte, command(0x2Eu)) ==
          jojo::Ps1GteCommandStatus::ok);
    CHECK(gte.data[7] == 325u);
}

void test_sqr_op_and_mvmva() {
    jojo::R3000aGte gte{};
    jojo::write_ps1_gte_data(gte, 9u, 2u);
    jojo::write_ps1_gte_data(gte, 10u, static_cast<std::uint32_t>(static_cast<std::int32_t>(-3)));
    jojo::write_ps1_gte_data(gte, 11u, 4u);

    CHECK(jojo::execute_ps1_gte_command(gte, command(0x28u)) ==
          jojo::Ps1GteCommandStatus::ok);
    CHECK(gte.data[25] == 4u);
    CHECK(gte.data[26] == 9u);
    CHECK(gte.data[27] == 16u);

    jojo::write_ps1_gte_data(gte, 9u, 4u);
    jojo::write_ps1_gte_data(gte, 10u, 5u);
    jojo::write_ps1_gte_data(gte, 11u, 6u);
    gte.control[0] = pack_s16(1, 0);
    gte.control[2] = pack_s16(2, 0);
    gte.control[4] = 3u;
    CHECK(jojo::execute_ps1_gte_command(gte, command(0x0Cu)) ==
          jojo::Ps1GteCommandStatus::ok);
    CHECK(static_cast<std::int32_t>(gte.data[25]) == -3);
    CHECK(static_cast<std::int32_t>(gte.data[26]) == 6);
    CHECK(static_cast<std::int32_t>(gte.data[27]) == -3);

    gte = {};
    set_identity_rotation(gte);
    gte.data[0] = pack_s16(1, 2);
    gte.data[1] = 3u;
    CHECK(jojo::execute_ps1_gte_command(gte, command(0x12u, true, false, 0u, 0u, 3u)) ==
          jojo::Ps1GteCommandStatus::ok);
    CHECK(gte.data[25] == 1u);
    CHECK(gte.data[26] == 2u);
    CHECK(gte.data[27] == 3u);
    CHECK(jojo::read_ps1_gte_data(gte, 9u) == 1u);
    CHECK(jojo::read_ps1_gte_data(gte, 10u) == 2u);
    CHECK(jojo::read_ps1_gte_data(gte, 11u) == 3u);
}

void test_rtps_identity_projection() {
    jojo::R3000aGte gte{};
    set_identity_rotation(gte);
    gte.data[0] = pack_s16(100, 50);
    gte.data[1] = 1000u;
    gte.control[26] = 1000u; // H
    gte.control[24] = 0u;    // OFX
    gte.control[25] = 0u;    // OFY
    gte.control[27] = 0u;    // DQA
    gte.control[28] = 0u;    // DQB

    CHECK(jojo::execute_ps1_gte_command(gte, command(0x01u, true)) ==
          jojo::Ps1GteCommandStatus::ok);
    CHECK(gte.data[19] == 1000u);
    CHECK(static_cast<std::int16_t>(gte.data[14] & 0xFFFFu) == 100);
    CHECK(static_cast<std::int16_t>((gte.data[14] >> 16u) & 0xFFFFu) == 50);
    CHECK(gte.data[8] == 0u);
}

void test_unknown_command_remains_explicit() {
    jojo::R3000aGte gte{};
    CHECK(jojo::execute_ps1_gte_command(gte, command(0x02u)) ==
          jojo::Ps1GteCommandStatus::unsupported);
}
}

int main() {
    test_register_side_effects();
    test_nclip_and_average_z();
    test_sqr_op_and_mvmva();
    test_rtps_identity_projection();
    test_unknown_command_remains_explicit();
    return failures ? 1 : 0;
}
