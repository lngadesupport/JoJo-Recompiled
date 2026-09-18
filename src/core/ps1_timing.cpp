#include "core/ps1_timing.h"

namespace jojo {

std::uint64_t Ps1NtscReferenceClock::next_frame_ticks() noexcept {
    constexpr auto numerator =
        nominal_cpu_hz * refresh_denominator;
    constexpr auto base = numerator / refresh_numerator;
    constexpr auto fractional = numerator % refresh_numerator;

    remainder_ += fractional;
    auto ticks = base;
    if (remainder_ >= refresh_numerator) {
        remainder_ -= refresh_numerator;
        ++ticks;
    }
    return ticks;
}

void Ps1NtscReferenceClock::reset() noexcept {
    remainder_ = 0u;
}

std::uint64_t Ps1NtscReferenceClock::remainder() const noexcept {
    return remainder_;
}

} // namespace jojo
