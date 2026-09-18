#pragma once

#include <cstdint>

namespace jojo {

class Ps1NtscReferenceClock {
public:
    static constexpr std::uint64_t nominal_cpu_hz = 33868800u;
    static constexpr std::uint64_t refresh_numerator = 60000u;
    static constexpr std::uint64_t refresh_denominator = 1001u;

    [[nodiscard]] std::uint64_t next_frame_ticks() noexcept;
    void reset() noexcept;
    [[nodiscard]] std::uint64_t remainder() const noexcept;

private:
    std::uint64_t remainder_{};
};

[[nodiscard]] constexpr double ps1_ntsc_frame_seconds() noexcept {
    return static_cast<double>(Ps1NtscReferenceClock::refresh_denominator) /
           static_cast<double>(Ps1NtscReferenceClock::refresh_numerator);
}

} // namespace jojo
