#include "core/ps1_timing.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1NtscReferenceClock clock;

    CHECK(jojo::Ps1NtscReferenceClock::nominal_cpu_hz == 33868800u);
    CHECK(jojo::Ps1NtscReferenceClock::refresh_numerator == 60000u);
    CHECK(jojo::Ps1NtscReferenceClock::refresh_denominator == 1001u);

    std::uint64_t total = 0u;
    std::uint64_t count_565044 = 0u;
    std::uint64_t count_565045 = 0u;
    for (std::uint64_t frame = 0u; frame < 125u; ++frame) {
        const auto ticks = clock.next_frame_ticks();
        CHECK(ticks == 565044u || ticks == 565045u);
        if (ticks == 565044u) ++count_565044;
        if (ticks == 565045u) ++count_565045;
        total += ticks;
    }

    CHECK(count_565044 == 65u);
    CHECK(count_565045 == 60u);
    CHECK(total == 70630560u);
    CHECK(clock.remainder() == 0u);

    clock.reset();
    CHECK(clock.remainder() == 0u);
    CHECK(clock.next_frame_ticks() == 565044u);

    const auto seconds = jojo::ps1_ntsc_frame_seconds();
    CHECK(seconds > 0.01668 && seconds < 0.01669);

    return failures ? 1 : 0;
}
