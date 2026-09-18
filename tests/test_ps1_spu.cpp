#include "core/ps1_spu.h"
#include "core/ps1_hardware_services.h"

#include <array>
#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)


static void test_spu_adpcm_decode_contract() {
    std::array<std::uint8_t, 16> block{};
    block[0] = 0x00u; // filter 0, shift 0
    block[1] = 0x07u; // loop end + repeat + loop start
    block[2] = 0x97u; // +7 then -7
    jojo::Ps1SpuAdpcmHistory history{};
    const auto decoded = jojo::Ps1Spu::decode_adpcm_block(block, history);

    CHECK(decoded.flags == 0x07u);
    CHECK(decoded.samples[0] == 28672);
    CHECK(decoded.samples[1] == -28672);

    std::array<std::uint8_t, 16> shifted{};
    shifted[0] = 0x04u; // filter 0, shift 4
    shifted[2] = 0x07u;
    history = {};
    const auto shifted_decoded = jojo::Ps1Spu::decode_adpcm_block(shifted, history);
    CHECK(shifted_decoded.samples[0] == 1792);

    std::array<std::uint8_t, 16> filtered{};
    filtered[0] = 0x10u; // filter 1, shift 0
    history.previous = 1000;
    history.older = 0;
    const auto filtered_decoded = jojo::Ps1Spu::decode_adpcm_block(filtered, history);
    CHECK(filtered_decoded.samples[0] == 938);
}


static void test_spu_host_neutral_audio_clock_and_voice_mix() {
    jojo::Ps1Spu spu;

    CHECK(spu.write16(0x1F801DA6u, 0x0200u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801DA8u, 0x0700u).status == jojo::R3000aBusStatus::ok);
    for (int i = 0; i < 7; ++i) {
        CHECK(spu.write16(0x1F801DA8u, 0x7777u).status == jojo::R3000aBusStatus::ok);
    }

    CHECK(spu.write16(0x1F801C00u, 0x3FFFu).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C02u, 0x3FFFu).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C04u, 0x1000u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C06u, 0x0200u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801D80u, 0x3FFFu).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801D82u, 0x3FFFu).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801DAAu, 0xC000u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.control() == 0xC000u);
    CHECK((spu.status() & 0x003Fu) == 0u);
    CHECK(spu.write16(0x1F801DACu, 0x0004u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.transfer_control() == 0x0004u);
    CHECK(spu.write16(0x1F801D88u, 0x0001u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C0Cu, 0x7FFFu).status == jojo::R3000aBusStatus::ok);

    spu.step(767u);
    CHECK(spu.generated_sample_frames() == 0u);
    CHECK(spu.nonzero_sample_count() == 0u);
    CHECK(spu.drain_audio_samples().empty());

    spu.step(1u);
    CHECK(spu.generated_sample_frames() == 1u);
    CHECK(spu.nonzero_sample_count() == 2u);
    const auto frame = spu.drain_audio_samples();
    CHECK(frame.size() == 2u);
    if (frame.size() == 2u) {
        CHECK(frame[0] > 20000);
        CHECK(frame[1] > 20000);
    }

    spu.step(768u * 28u);
    CHECK(spu.generated_sample_frames() == 29u);
    CHECK((spu.endx_flags() & 1u) != 0u);
}


static void test_spu_adsr_advances_and_releases() {
    jojo::Ps1Spu spu;
    CHECK(spu.write16(0x1F801C08u, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C0Au, 0x0000u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C04u, 0x1000u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801DAAu, 0xC000u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801D88u, 0x0001u).status == jojo::R3000aBusStatus::ok);

    CHECK(spu.voice(0u).adsr_volume == 0u);
    spu.step(768u);
    CHECK(spu.voice(0u).adsr_volume > 0u);

    CHECK(spu.write16(0x1F801C0Cu, 0x7FFFu).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801D8Cu, 0x0001u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.voice(0u).releasing);
    spu.step(768u * 4u);
    CHECK(spu.voice(0u).adsr_volume == 0u);
    CHECK(!spu.voice(0u).releasing);
}

int main() {
    test_spu_adpcm_decode_contract();
    test_spu_host_neutral_audio_clock_and_voice_mix();
    test_spu_adsr_advances_and_releases();
    jojo::Ps1Spu spu;

    CHECK(spu.write16(0x1F801C00u, 0x1234u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C02u, 0xFEDCu).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C04u, 0x1000u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801C06u, 0x0010u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.read16(0x1F801C00u).value == 0x1234u);
    CHECK(spu.read16(0x1F801C02u).value == 0xFEDCu);
    CHECK(spu.read16(0x1F801C04u).value == 0x1000u);
    CHECK(spu.read16(0x1F801C06u).value == 0x0010u);

    CHECK(spu.write16(0x1F801D88u, 0x0001u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.voice(0u).keyed_on);
    CHECK(!spu.voice(0u).releasing);
    CHECK(spu.voice(0u).current_address == 0x80u);
    CHECK((spu.endx_flags() & 1u) == 0u);

    CHECK(spu.write16(0x1F801D8Cu, 0x0001u).status == jojo::R3000aBusStatus::ok);
    CHECK(!spu.voice(0u).keyed_on);
    CHECK(spu.voice(0u).releasing);

    // ENDX is hardware-owned status, but retail code may write it while
    // resetting SPU state. Writes must be accepted without forging voice
    // completion bits.
    const auto endx_before = spu.endx_flags();
    CHECK(spu.write16(0x1F801D9Cu, 0x0000u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(spu.write16(0x1F801D9Eu, 0xFFFFu).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(spu.endx_flags() == endx_before);

    CHECK(spu.write16(0x1F801DA6u, 0x0020u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.transfer_current_address() == 0x100u);
    CHECK(spu.write16(0x1F801DA8u, 0xA1B2u).status == jojo::R3000aBusStatus::ok);
    CHECK(spu.sound_ram_byte(0x100u) == 0xB2u);
    CHECK(spu.sound_ram_byte(0x101u) == 0xA1u);
    CHECK(spu.transfer_current_address() == 0x102u);

    jojo::Ps1HardwareServices hw;
    CHECK(hw.write32(0x1F801D80u, 0x22221111u).status == jojo::R3000aBusStatus::ok);
    const auto main_volume = hw.read32(0x1F801D80u);
    CHECK(main_volume.status == jojo::R3000aBusStatus::ok);
    CHECK(main_volume.value == 0x22221111u);

    CHECK(spu.read16(0x1F801E60u).status == jojo::R3000aBusStatus::unsupported);

    return failures ? 1 : 0;
}
