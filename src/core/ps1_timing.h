#pragma once

#include <cstdint>

namespace jojo {

enum class Ps1VideoTimingMode : std::uint8_t {
    ntsc_non_interlaced,
    ntsc_interlaced,
    pal_non_interlaced,
    pal_interlaced,
};

struct Ps1VideoTimingSpec {
    std::uint64_t refresh_numerator{};
    std::uint64_t refresh_denominator{};
};

[[nodiscard]] constexpr Ps1VideoTimingSpec ps1_video_timing_spec(
    Ps1VideoTimingMode mode) noexcept {
    switch (mode) {
        case Ps1VideoTimingMode::ntsc_non_interlaced:
            return {29913u, 500u};
        case Ps1VideoTimingMode::ntsc_interlaced:
            return {2997u, 50u};
        case Ps1VideoTimingMode::pal_non_interlaced:
            return {49761u, 1000u};
        case Ps1VideoTimingMode::pal_interlaced:
            return {50u, 1u};
    }
    return {29913u, 500u};
}

class Ps1VideoReferenceClock {
public:
    static constexpr std::uint64_t nominal_cpu_hz = 33868800u;

    explicit Ps1VideoReferenceClock(
        Ps1VideoTimingMode mode =
            Ps1VideoTimingMode::ntsc_non_interlaced) noexcept
        : mode_(mode) {}

    [[nodiscard]] std::uint64_t next_frame_ticks() noexcept;
    void reset() noexcept;
    void set_mode(Ps1VideoTimingMode mode) noexcept;
    [[nodiscard]] Ps1VideoTimingMode mode() const noexcept;
    [[nodiscard]] std::uint64_t remainder() const noexcept;

private:
    Ps1VideoTimingMode mode_{Ps1VideoTimingMode::ntsc_non_interlaced};
    std::uint64_t remainder_{};
};

using Ps1NtscReferenceClock = Ps1VideoReferenceClock;

[[nodiscard]] constexpr double ps1_frame_seconds(
    Ps1VideoTimingMode mode) noexcept {
    const auto spec = ps1_video_timing_spec(mode);
    return static_cast<double>(spec.refresh_denominator) /
           static_cast<double>(spec.refresh_numerator);
}

[[nodiscard]] constexpr double ps1_ntsc_frame_seconds() noexcept {
    return ps1_frame_seconds(Ps1VideoTimingMode::ntsc_non_interlaced);
}

} // namespace jojo
