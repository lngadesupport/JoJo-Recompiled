#include "app_win32/audio_host.h"

#include <array>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    CHECK(jojo::xaudio2_gain_from_percent(-10) == 0.0f);
    CHECK(jojo::xaudio2_gain_from_percent(0) == 0.0f);
    CHECK(jojo::xaudio2_gain_from_percent(50) == 0.5f);
    CHECK(jojo::xaudio2_gain_from_percent(100) == 1.0f);
    CHECK(jojo::xaudio2_gain_from_percent(150) == 1.0f);

    const std::array<std::int16_t, 4> samples{100, -100, 200, -200};
    const auto plan = jojo::make_xaudio2_pcm_plan(samples);
    CHECK(plan);
    if (plan) {
        CHECK(plan.value.sample_rate == 44100u);
        CHECK(plan.value.channels == 2u);
        CHECK(plan.value.bits_per_sample == 16u);
        CHECK(plan.value.block_align == 4u);
        CHECK(plan.value.average_bytes_per_second == 176400u);
        CHECK(plan.value.frame_count == 2u);
        CHECK(plan.value.byte_size == 8u);
        CHECK(plan.value.samples == samples.data());
    }

    const std::array<std::int16_t, 3> odd{1, 2, 3};
    CHECK(!jojo::make_xaudio2_pcm_plan(odd));

    const std::array<std::int16_t, 0> empty{};
    CHECK(!jojo::make_xaudio2_pcm_plan(empty));

    return failures ? 1 : 0;
}
