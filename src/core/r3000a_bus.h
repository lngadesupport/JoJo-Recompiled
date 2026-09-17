#pragma once

#include <cstdint>

namespace jojo {

enum class R3000aBusStatus : std::uint8_t {
    ok,
    bus_error,
    unsupported,
};

struct R3000aBusResult {
    R3000aBusStatus status{R3000aBusStatus::ok};
    std::uint32_t value{};
};

class R3000aBus {
public:
    virtual ~R3000aBus() = default;

    virtual R3000aBusResult read8(std::uint32_t address) noexcept = 0;
    virtual R3000aBusResult read16(std::uint32_t address) noexcept = 0;
    virtual R3000aBusResult read32(std::uint32_t address) noexcept = 0;
    virtual R3000aBusResult write8(std::uint32_t address, std::uint8_t value) noexcept = 0;
    virtual R3000aBusResult write16(std::uint32_t address, std::uint16_t value) noexcept = 0;
    virtual R3000aBusResult write32(std::uint32_t address, std::uint32_t value) noexcept = 0;
};

} // namespace jojo
