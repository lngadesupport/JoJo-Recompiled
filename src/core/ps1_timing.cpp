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

std::uint64_t Ps1FrameSliceBudget::begin_frame(
    Ps1VideoTimingMode mode) noexcept {
    clock_.set_mode(mode);
    remaining_ticks_ = clock_.next_frame_ticks();
    return remaining_ticks_;
}

std::uint64_t Ps1FrameSliceBudget::next_slice_ticks() const noexcept {
    return remaining_ticks_ < max_slice_ticks_
        ? remaining_ticks_
        : max_slice_ticks_;
}

bool Ps1FrameSliceBudget::consume(std::uint64_t ticks) noexcept {
    if (ticks == 0u || ticks > remaining_ticks_) return false;
    remaining_ticks_ -= ticks;
    return true;
}

bool Ps1FrameSliceBudget::frame_complete() const noexcept {
    return remaining_ticks_ == 0u;
}

std::uint64_t Ps1FrameSliceBudget::remaining_ticks() const noexcept {
    return remaining_ticks_;
}

Ps1VideoTimingMode Ps1FrameSliceBudget::mode() const noexcept {
    return clock_.mode();
}

void Ps1FrameSliceBudget::reset() noexcept {
    clock_.reset();
    remaining_ticks_ = 0u;
}

} // namespace jojo
