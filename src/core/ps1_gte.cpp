#include "core/ps1_gte.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

namespace jojo {
namespace {

constexpr std::uint32_t kFlagError = 1u << 31u;
constexpr std::uint32_t kFlagErrorInputs = 0x7F87E000u;
constexpr std::uint32_t kFlagDivideOverflow = 1u << 17u;
constexpr std::uint32_t kFlagSzOtzSaturated = 1u << 18u;
constexpr std::uint32_t kFlagSxSaturated = 1u << 14u;
constexpr std::uint32_t kFlagSySaturated = 1u << 13u;
constexpr std::uint32_t kFlagIr0Saturated = 1u << 12u;

std::int16_t signed16(std::uint32_t value) noexcept {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

std::int32_t signed32(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

std::uint32_t sign_extend16(std::uint32_t value) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(signed16(value)));
}

std::int64_t arithmetic_shift_right(std::int64_t value, unsigned shift) noexcept {
    if (shift == 0u) return value;
    if (value >= 0) return value >> shift;
    const auto magnitude = static_cast<std::uint64_t>(-(value + 1)) + 1u;
    const auto rounded = (magnitude + ((std::uint64_t{1} << shift) - 1u)) >> shift;
    return -static_cast<std::int64_t>(rounded);
}

void update_error_flag(R3000aGte& gte) noexcept {
    if ((gte.control[31] & kFlagErrorInputs) != 0u) gte.control[31] |= kFlagError;
    else gte.control[31] &= ~kFlagError;
}

void clear_flags(R3000aGte& gte) noexcept {
    gte.control[31] = 0u;
}

void check_mac123_overflow(R3000aGte& gte, unsigned index, std::int64_t value) noexcept {
    constexpr std::int64_t kMin = -(std::int64_t{1} << 43u);
    constexpr std::int64_t kMax = (std::int64_t{1} << 43u) - 1;
    if (value > kMax) gte.control[31] |= 1u << (31u - index);
    else if (value < kMin) gte.control[31] |= 1u << (28u - index);
}

void check_mac0_overflow(R3000aGte& gte, std::int64_t value) noexcept {
    if (value > std::numeric_limits<std::int32_t>::max()) gte.control[31] |= 1u << 16u;
    else if (value < std::numeric_limits<std::int32_t>::min()) gte.control[31] |= 1u << 15u;
}

void store_mac0(R3000aGte& gte, std::int64_t value) noexcept {
    check_mac0_overflow(gte, value);
    gte.data[24] = static_cast<std::uint32_t>(value);
}

void store_ir(R3000aGte& gte, unsigned index, std::int64_t value, bool lm) noexcept {
    const std::int64_t minimum = lm ? 0 : -32768;
    constexpr std::int64_t maximum = 32767;
    if (value < minimum) {
        value = minimum;
        gte.control[31] |= 1u << (25u - index);
    } else if (value > maximum) {
        value = maximum;
        gte.control[31] |= 1u << (25u - index);
    }
    gte.data[8u + index] =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
}

void store_mac_ir(
    R3000aGte& gte,
    unsigned index,
    std::int64_t raw,
    unsigned shift,
    bool lm) noexcept {
    check_mac123_overflow(gte, index, raw);
    const auto value = arithmetic_shift_right(raw, shift);
    gte.data[24u + index] = static_cast<std::uint32_t>(value);
    store_ir(gte, index, value, lm);
}

void store_ir0(R3000aGte& gte, std::int64_t value) noexcept {
    if (value < 0) {
        value = 0;
        gte.control[31] |= kFlagIr0Saturated;
    } else if (value > 0x1000) {
        value = 0x1000;
        gte.control[31] |= kFlagIr0Saturated;
    }
    gte.data[8] = static_cast<std::uint32_t>(value);
}

void store_otz(R3000aGte& gte, std::int64_t value) noexcept {
    if (value < 0) {
        value = 0;
        gte.control[31] |= kFlagSzOtzSaturated;
    } else if (value > 0xFFFF) {
        value = 0xFFFF;
        gte.control[31] |= kFlagSzOtzSaturated;
    }
    gte.data[7] = static_cast<std::uint32_t>(value);
}

void push_sz(R3000aGte& gte, std::int64_t value) noexcept {
    if (value < 0) {
        value = 0;
        gte.control[31] |= kFlagSzOtzSaturated;
    } else if (value > 0xFFFF) {
        value = 0xFFFF;
        gte.control[31] |= kFlagSzOtzSaturated;
    }
    gte.data[16] = gte.data[17];
    gte.data[17] = gte.data[18];
    gte.data[18] = gte.data[19];
    gte.data[19] = static_cast<std::uint32_t>(value);
}

void push_sxy(R3000aGte& gte, std::int64_t x, std::int64_t y) noexcept {
    if (x < -1024) {
        x = -1024;
        gte.control[31] |= kFlagSxSaturated;
    } else if (x > 1023) {
        x = 1023;
        gte.control[31] |= kFlagSxSaturated;
    }
    if (y < -1024) {
        y = -1024;
        gte.control[31] |= kFlagSySaturated;
    } else if (y > 1023) {
        y = 1023;
        gte.control[31] |= kFlagSySaturated;
    }

    gte.data[12] = gte.data[13];
    gte.data[13] = gte.data[14];
    gte.data[14] =
        static_cast<std::uint16_t>(static_cast<std::int16_t>(x)) |
        (static_cast<std::uint32_t>(
             static_cast<std::uint16_t>(static_cast<std::int16_t>(y))) << 16u);
}

void matrix_from_control(
    const R3000aGte& gte,
    unsigned base,
    std::int16_t (&matrix)[3][3]) noexcept {
    matrix[0][0] = signed16(gte.control[base + 0u]);
    matrix[0][1] = signed16(gte.control[base + 0u] >> 16u);
    matrix[0][2] = signed16(gte.control[base + 1u]);
    matrix[1][0] = signed16(gte.control[base + 1u] >> 16u);
    matrix[1][1] = signed16(gte.control[base + 2u]);
    matrix[1][2] = signed16(gte.control[base + 2u] >> 16u);
    matrix[2][0] = signed16(gte.control[base + 3u]);
    matrix[2][1] = signed16(gte.control[base + 3u] >> 16u);
    matrix[2][2] = signed16(gte.control[base + 4u]);
}

void vector_from_data(
    const R3000aGte& gte,
    unsigned vector_index,
    std::int16_t (&vector)[3]) noexcept {
    if (vector_index < 3u) {
        const unsigned base = vector_index * 2u;
        vector[0] = signed16(gte.data[base]);
        vector[1] = signed16(gte.data[base] >> 16u);
        vector[2] = signed16(gte.data[base + 1u]);
    } else {
        vector[0] = signed16(gte.data[9]);
        vector[1] = signed16(gte.data[10]);
        vector[2] = signed16(gte.data[11]);
    }
}

Ps1GteCommandStatus execute_mvmva(R3000aGte& gte, std::uint32_t raw) noexcept {
    const unsigned shift = ((raw >> 19u) & 1u) != 0u ? 12u : 0u;
    const bool lm = ((raw >> 10u) & 1u) != 0u;
    const unsigned matrix_select = (raw >> 17u) & 3u;
    const unsigned vector_select = (raw >> 15u) & 3u;
    const unsigned translation_select = (raw >> 13u) & 3u;
    if (matrix_select == 3u || translation_select == 2u) {
        return Ps1GteCommandStatus::unsupported;
    }

    const unsigned matrix_base = matrix_select == 0u ? 0u :
                                 matrix_select == 1u ? 8u : 16u;
    std::int16_t matrix[3][3]{};
    matrix_from_control(gte, matrix_base, matrix);

    std::int16_t vector[3]{};
    vector_from_data(gte, vector_select, vector);

    std::int32_t translation[3]{};
    if (translation_select != 3u) {
        const unsigned base = translation_select == 0u ? 5u : 13u;
        translation[0] = signed32(gte.control[base + 0u]);
        translation[1] = signed32(gte.control[base + 1u]);
        translation[2] = signed32(gte.control[base + 2u]);
    }

    clear_flags(gte);
    for (unsigned row = 0u; row < 3u; ++row) {
        const std::int64_t result =
            static_cast<std::int64_t>(translation[row]) * 0x1000 +
            static_cast<std::int64_t>(matrix[row][0]) * vector[0] +
            static_cast<std::int64_t>(matrix[row][1]) * vector[1] +
            static_cast<std::int64_t>(matrix[row][2]) * vector[2];
        store_mac_ir(gte, row + 1u, result, shift, lm);
    }
    update_error_flag(gte);
    return Ps1GteCommandStatus::ok;
}

void execute_sqr(R3000aGte& gte, std::uint32_t raw) noexcept {
    const unsigned shift = ((raw >> 19u) & 1u) != 0u ? 12u : 0u;
    clear_flags(gte);
    for (unsigned i = 1u; i <= 3u; ++i) {
        const auto value = static_cast<std::int64_t>(signed16(gte.data[8u + i]));
        store_mac_ir(gte, i, value * value, shift, false);
    }
    update_error_flag(gte);
}

void execute_op(R3000aGte& gte, std::uint32_t raw) noexcept {
    const unsigned shift = ((raw >> 19u) & 1u) != 0u ? 12u : 0u;
    const bool lm = ((raw >> 10u) & 1u) != 0u;
    const auto ir1 = static_cast<std::int64_t>(signed16(gte.data[9]));
    const auto ir2 = static_cast<std::int64_t>(signed16(gte.data[10]));
    const auto ir3 = static_cast<std::int64_t>(signed16(gte.data[11]));
    const auto d1 = static_cast<std::int64_t>(signed16(gte.control[0]));
    const auto d2 = static_cast<std::int64_t>(signed16(gte.control[2]));
    const auto d3 = static_cast<std::int64_t>(signed16(gte.control[4]));

    clear_flags(gte);
    store_mac_ir(gte, 1u, ir3 * d2 - ir2 * d3, shift, lm);
    store_mac_ir(gte, 2u, ir1 * d3 - ir3 * d1, shift, lm);
    store_mac_ir(gte, 3u, ir2 * d1 - ir1 * d2, shift, lm);
    update_error_flag(gte);
}

void execute_nclip(R3000aGte& gte) noexcept {
    const auto sx0 = static_cast<std::int64_t>(signed16(gte.data[12]));
    const auto sy0 = static_cast<std::int64_t>(signed16(gte.data[12] >> 16u));
    const auto sx1 = static_cast<std::int64_t>(signed16(gte.data[13]));
    const auto sy1 = static_cast<std::int64_t>(signed16(gte.data[13] >> 16u));
    const auto sx2 = static_cast<std::int64_t>(signed16(gte.data[14]));
    const auto sy2 = static_cast<std::int64_t>(signed16(gte.data[14] >> 16u));

    clear_flags(gte);
    store_mac0(gte,
        sx0 * sy1 + sx1 * sy2 + sx2 * sy0 -
        sx0 * sy2 - sx1 * sy0 - sx2 * sy1);
    update_error_flag(gte);
}

void execute_avsz(R3000aGte& gte, bool four) noexcept {
    clear_flags(gte);
    std::uint64_t sum =
        static_cast<std::uint16_t>(gte.data[17]) +
        static_cast<std::uint16_t>(gte.data[18]) +
        static_cast<std::uint16_t>(gte.data[19]);
    const unsigned zsf_index = four ? 30u : 29u;
    if (four) sum += static_cast<std::uint16_t>(gte.data[16]);

    const auto result =
        static_cast<std::int64_t>(signed16(gte.control[zsf_index])) *
        static_cast<std::int64_t>(sum);
    store_mac0(gte, result);
    store_otz(gte, arithmetic_shift_right(result, 12u));
    update_error_flag(gte);
}

std::uint32_t perspective_factor(R3000aGte& gte, std::uint32_t h, std::uint32_t sz) noexcept {
    if (sz == 0u || static_cast<std::uint64_t>(h) >= static_cast<std::uint64_t>(sz) * 2u) {
        gte.control[31] |= kFlagDivideOverflow;
        return 0x1FFFFu;
    }
    const auto numerator = static_cast<std::uint64_t>(h) * 0x10000u + (sz / 2u);
    const auto value = numerator / sz;
    if (value > 0x1FFFFu) {
        gte.control[31] |= kFlagDivideOverflow;
        return 0x1FFFFu;
    }
    return static_cast<std::uint32_t>(value);
}

void transform_vertex(
    R3000aGte& gte,
    unsigned vector_index,
    unsigned shift,
    bool lm,
    bool depth_cue) noexcept {
    std::int16_t matrix[3][3]{};
    matrix_from_control(gte, 0u, matrix);
    std::int16_t vector[3]{};
    vector_from_data(gte, vector_index, vector);

    std::int64_t raw[3]{};
    for (unsigned row = 0u; row < 3u; ++row) {
        raw[row] =
            static_cast<std::int64_t>(signed32(gte.control[5u + row])) * 0x1000 +
            static_cast<std::int64_t>(matrix[row][0]) * vector[0] +
            static_cast<std::int64_t>(matrix[row][1]) * vector[1] +
            static_cast<std::int64_t>(matrix[row][2]) * vector[2];
        store_mac_ir(gte, row + 1u, raw[row], shift, lm);
    }

    push_sz(gte, arithmetic_shift_right(raw[2], 12u));
    const auto h = static_cast<std::uint32_t>(
        static_cast<std::uint16_t>(gte.control[26]));
    const auto sz3 = static_cast<std::uint32_t>(
        static_cast<std::uint16_t>(gte.data[19]));
    const auto factor = perspective_factor(gte, h, sz3);

    const auto screen_x_mac =
        static_cast<std::int64_t>(factor) * signed16(gte.data[9]) +
        signed32(gte.control[24]);
    const auto screen_y_mac =
        static_cast<std::int64_t>(factor) * signed16(gte.data[10]) +
        signed32(gte.control[25]);
    check_mac0_overflow(gte, screen_x_mac);
    check_mac0_overflow(gte, screen_y_mac);
    push_sxy(
        gte,
        arithmetic_shift_right(screen_x_mac, 16u),
        arithmetic_shift_right(screen_y_mac, 16u));

    if (depth_cue) {
        const auto dqa = static_cast<std::int64_t>(signed16(gte.control[27]));
        const auto dqb = static_cast<std::int64_t>(signed32(gte.control[28]));
        const auto depth_mac = static_cast<std::int64_t>(factor) * dqa + dqb;
        store_mac0(gte, depth_mac);
        store_ir0(gte, arithmetic_shift_right(depth_mac, 12u));
    }
}

void execute_rtps(R3000aGte& gte, std::uint32_t raw, bool triple) noexcept {
    const unsigned shift = ((raw >> 19u) & 1u) != 0u ? 12u : 0u;
    const bool lm = ((raw >> 10u) & 1u) != 0u;
    clear_flags(gte);
    if (triple) {
        transform_vertex(gte, 0u, shift, lm, false);
        transform_vertex(gte, 1u, shift, lm, false);
        transform_vertex(gte, 2u, shift, lm, true);
    } else {
        transform_vertex(gte, 0u, shift, lm, true);
    }
    update_error_flag(gte);
}

std::uint32_t packed_irgb(const R3000aGte& gte) noexcept {
    const auto component = [&](unsigned index) {
        const auto value = static_cast<std::int32_t>(signed16(gte.data[index]));
        return static_cast<std::uint32_t>(std::clamp(value / 0x80, 0, 0x1F));
    };
    return component(9u) |
           (component(10u) << 5u) |
           (component(11u) << 10u);
}

} // namespace

std::uint32_t read_ps1_gte_data(const R3000aGte& gte, std::uint8_t index) noexcept {
    index &= 31u;
    if (index == 15u) return gte.data[14];
    if (index == 28u || index == 29u) return packed_irgb(gte);
    return gte.data[index];
}

void write_ps1_gte_data(
    R3000aGte& gte,
    std::uint8_t index,
    std::uint32_t value) noexcept {
    index &= 31u;
    switch (index) {
        case 1u:
        case 3u:
        case 5u:
        case 8u:
        case 9u:
        case 10u:
        case 11u:
            gte.data[index] = sign_extend16(value);
            break;
        case 7u:
        case 16u:
        case 17u:
        case 18u:
        case 19u:
            gte.data[index] = value & 0xFFFFu;
            break;
        case 15u:
            gte.data[12] = gte.data[13];
            gte.data[13] = gte.data[14];
            gte.data[14] = value;
            break;
        case 28u:
            gte.data[28] = value & 0x7FFFu;
            gte.data[9] = ((value >> 0u) & 0x1Fu) * 0x80u;
            gte.data[10] = ((value >> 5u) & 0x1Fu) * 0x80u;
            gte.data[11] = ((value >> 10u) & 0x1Fu) * 0x80u;
            break;
        case 29u:
        case 31u:
            break;
        case 30u:
            gte.data[30] = value;
            gte.data[31] = (value & 0x80000000u) != 0u
                ? static_cast<std::uint32_t>(std::countl_zero(~value))
                : static_cast<std::uint32_t>(std::countl_zero(value));
            break;
        default:
            gte.data[index] = value;
            break;
    }
}

std::uint32_t read_ps1_gte_control(const R3000aGte& gte, std::uint8_t index) noexcept {
    return gte.control[index & 31u];
}

void write_ps1_gte_control(
    R3000aGte& gte,
    std::uint8_t index,
    std::uint32_t value) noexcept {
    index &= 31u;
    switch (index) {
        case 4u:
        case 12u:
        case 20u:
        case 26u:
        case 27u:
        case 29u:
        case 30u:
            gte.control[index] = sign_extend16(value);
            break;
        case 31u:
            gte.control[31] = value & 0x7FFFF000u;
            update_error_flag(gte);
            break;
        default:
            gte.control[index] = value;
            break;
    }
}

Ps1GteCommandStatus execute_ps1_gte_command(
    R3000aGte& gte,
    std::uint32_t raw) noexcept {
    switch (raw & 0x3Fu) {
        case 0x01u:
            execute_rtps(gte, raw, false);
            return Ps1GteCommandStatus::ok;
        case 0x06u:
            execute_nclip(gte);
            return Ps1GteCommandStatus::ok;
        case 0x0Cu:
            execute_op(gte, raw);
            return Ps1GteCommandStatus::ok;
        case 0x12u:
            return execute_mvmva(gte, raw);
        case 0x28u:
            execute_sqr(gte, raw);
            return Ps1GteCommandStatus::ok;
        case 0x2Du:
            execute_avsz(gte, false);
            return Ps1GteCommandStatus::ok;
        case 0x2Eu:
            execute_avsz(gte, true);
            return Ps1GteCommandStatus::ok;
        case 0x30u:
            execute_rtps(gte, raw, true);
            return Ps1GteCommandStatus::ok;
        default:
            return Ps1GteCommandStatus::unsupported;
    }
}

} // namespace jojo
