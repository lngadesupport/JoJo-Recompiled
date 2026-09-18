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

    clock.reset();
    CHECK(clock.remainder() == 0u);
    return failures ? 1 : 0;
}
