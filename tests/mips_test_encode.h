#pragma once

#include <cstdint>

namespace test_mips {

constexpr std::uint32_t r(
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint8_t rd,
    std::uint8_t sa,
    std::uint8_t funct) noexcept {
    return (std::uint32_t(rs) << 21) |
           (std::uint32_t(rt) << 16) |
           (std::uint32_t(rd) << 11) |
           (std::uint32_t(sa) << 6) |
           std::uint32_t(funct);
}

constexpr std::uint32_t i(
    std::uint8_t op,
    std::uint8_t rs,
    std::uint8_t rt,
    std::uint16_t imm) noexcept {
    return (std::uint32_t(op) << 26) |
           (std::uint32_t(rs) << 21) |
           (std::uint32_t(rt) << 16) |
           std::uint32_t(imm);
}

constexpr std::uint32_t j(std::uint8_t op, std::uint32_t target) noexcept {
    return (std::uint32_t(op) << 26) | (target & 0x03ffffffu);
}

} // namespace test_mips
