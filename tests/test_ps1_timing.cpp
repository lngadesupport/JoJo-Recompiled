#include "core/ps1_timing.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    using jojo::Ps1VideoTimingMode;

    jojo::Ps1VideoReferenceClock clock;
    CHECK(jojo::Ps1VideoReferenceClock::nominal_cpu_hz == 33868800u);
    CHECK(clock.mode() == Ps1VideoTimingMode::ntsc_non_interlaced);

    const auto ntsc_progressive = jojo::ps1_video_timing_spec(
        Ps1VideoTimingMode::ntsc_non_interlaced);
    CHECK(ntsc_progressive.refresh_numerator == 29913u);
    CHECK(ntsc_progressive.refresh_denominator == 500u);
    CHECK(clock.next_frame_ticks() == 566121u);
    CHECK(clock.next_frame_ticks() == 566122u);

    const auto saved_ntsc = clock.save_state();
    const auto expected_after_restore = clock.next_frame_ticks();
    CHECK(clock.load_state(saved_ntsc));
    CHECK(clock.next_frame_ticks() == expected_after_restore);
    CHECK(!clock.load_state({
        Ps1VideoTimingMode::ntsc_non_interlaced,
        ntsc_progressive.refresh_numerator}));

    clock.set_mode(Ps1VideoTimingMode::ntsc_interlaced);
    CHECK(clock.remainder() == 0u);
    CHECK(clock.next_frame_ticks() == 565045u);

    clock.set_mode(Ps1VideoTimingMode::pal_non_interlaced);
    CHECK(clock.next_frame_ticks() == 680629u);
    CHECK(clock.next_frame_ticks() == 680629u);
    CHECK(clock.next_frame_ticks() == 680630u);

    clock.set_mode(Ps1VideoTimingMode::pal_interlaced);
    CHECK(clock.next_frame_ticks() == 677376u);
    CHECK(clock.next_frame_ticks() == 677376u);

    CHECK(jojo::ps1_frame_seconds(
              Ps1VideoTimingMode::ntsc_non_interlaced) > 0.01671);
    CHECK(jojo::ps1_frame_seconds(
              Ps1VideoTimingMode::ntsc_non_interlaced) < 0.01672);
    CHECK(jojo::ps1_frame_seconds(
              Ps1VideoTimingMode::ntsc_interlaced) > 0.01668);
    CHECK(jojo::ps1_frame_seconds(
              Ps1VideoTimingMode::ntsc_interlaced) < 0.01669);
    CHECK(jojo::ps1_frame_seconds(
              Ps1VideoTimingMode::pal_interlaced) == 0.02);


    jojo::Ps1FrameSliceBudget slices{65536u};
    const auto frame_budget = slices.begin_frame(
        Ps1VideoTimingMode::ntsc_non_interlaced);
    CHECK(frame_budget == 566121u);
    std::uint64_t sliced_total = 0u;
    std::uint32_t slice_count = 0u;
    while (!slices.frame_complete()) {
        const auto slice = slices.next_slice_ticks();
        CHECK(slice > 0u);
        CHECK(slice <= 65536u);
        sliced_total += slice;
        ++slice_count;
        CHECK(slices.consume(slice));
    }
    CHECK(sliced_total == frame_budget);
    CHECK(slice_count == 9u);
    CHECK(slices.remaining_ticks() == 0u);
    CHECK(slices.next_slice_ticks() == 0u);
    CHECK(!slices.consume(1u));

    const auto pal_budget = slices.begin_frame(
        Ps1VideoTimingMode::pal_interlaced);
    CHECK(pal_budget == 677376u);
    CHECK(slices.next_slice_ticks() == 65536u);
    CHECK(slices.consume(65536u));
    CHECK(slices.remaining_ticks() == pal_budget - 65536u);

    clock.reset();
    CHECK(clock.remainder() == 0u);
    return failures ? 1 : 0;
}
