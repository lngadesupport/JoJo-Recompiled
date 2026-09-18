#include "core/ps1_timing.h"

namespace jojo {

std::uint64_t Ps1VideoReferenceClock::next_frame_ticks() noexcept {
    const auto spec = ps1_video_timing_spec(mode_);
    const auto numerator =
        nominal_cpu_hz * spec.refresh_denominator;
    const auto base = numerator / spec.refresh_numerator;
    const auto fractional = numerator % spec.refresh_numerator;

    remainder_ += fractional;
    auto ticks = base;
    if (remainder_ >= spec.refresh_numerator) {
        remainder_ -= spec.refresh_numerator;
        ++ticks;
    }
    return ticks;
}

void Ps1VideoReferenceClock::reset() noexcept {
    remainder_ = 0u;
}

void Ps1VideoReferenceClock::set_mode(
    Ps1VideoTimingMode mode) noexcept {
    if (mode_ == mode) return;
    mode_ = mode;
    remainder_ = 0u;
}

Ps1VideoTimingMode Ps1VideoReferenceClock::mode() const noexcept {
    return mode_;
}

std::uint64_t Ps1VideoReferenceClock::remainder() const noexcept {
    return remainder_;
}

} // namespace jojo
