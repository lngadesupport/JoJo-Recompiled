#include "core/ps1_hardware_services.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1HardwareServices hw;

    CHECK(hw.read32(0x1F8010F0u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.read32(0x1F8010F0u).value == 0x07654321u);
    CHECK(hw.read32(0x1F8010F4u).value == 0u);

    for (std::uint32_t channel = 0; channel < 7; ++channel) {
        const auto base = 0x1F801080u + channel * 0x10u;
        const auto madr = 0x1000u + channel * 0x100u;
        const auto bcr = 0x00000004u + channel;
        CHECK(hw.write32(base + 0x0u, madr).status == jojo::R3000aBusStatus::ok);
        CHECK(hw.write32(base + 0x4u, bcr).status == jojo::R3000aBusStatus::ok);
        CHECK(hw.write32(base + 0x8u, 0u).status == jojo::R3000aBusStatus::ok);
        CHECK(hw.read32(base + 0x0u).value == madr);
        CHECK(hw.read32(base + 0x4u).value == bcr);
        CHECK(hw.read32(base + 0x8u).value == 0u);
    }

    // Disabled channel must not silently start a transfer.
    CHECK(hw.write32(0x1F8010F0u, 0u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B0u, 0x00002000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B4u, 0x00000004u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B8u, 0x11000000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(!hw.pending_dma_transfer().has_value());

    // Channel 3: CD-ROM -> RAM, manual bounded transfer.
    const std::uint32_t ch3_enable = 1u << (3u * 4u + 3u);
    CHECK(hw.write32(0x1F8010F0u, ch3_enable).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B0u, 0x00002000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B4u, 0x00000004u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B8u, 0x11000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.pending_dma_transfer().has_value());
    if (hw.pending_dma_transfer()) {
        CHECK(hw.pending_dma_transfer()->channel == 3u);
        CHECK(hw.pending_dma_transfer()->madr == 0x00002000u);
        CHECK(hw.pending_dma_transfer()->words == 4u);
        CHECK(!hw.pending_dma_transfer()->from_ram);
    }

    // Enable channel-3 IRQ + master IRQ and complete only after the device handoff succeeds.
    CHECK(hw.write32(0x1F8010F4u, (1u << 23u) | (1u << 19u)).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.complete_dma_transfer(3u));
    CHECK(!hw.pending_dma_transfer().has_value());
    CHECK(hw.completed_dma_transfer_count() == 1u);
    CHECK((hw.read32(0x1F8010F4u).value & (1u << 27u)) != 0u);
    CHECK((hw.read32(0x1F8010F4u).value & 0x80000000u) != 0u);
    CHECK((hw.interrupt_status() & 0x0008u) != 0u);

    // Acknowledge channel-3 flag.
    CHECK(hw.write32(0x1F8010F4u, (1u << 23u) | (1u << 19u) | (1u << 27u)).status == jojo::R3000aBusStatus::ok);
    CHECK((hw.read32(0x1F8010F4u).value & (1u << 27u)) == 0u);

    // Channel 2: RAM -> GPU request is accepted; unsupported devices are not guessed.
    const std::uint32_t ch2_enable = 1u << (2u * 4u + 3u);
    CHECK(hw.write32(0x1F8010F0u, ch2_enable).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A0u, 0x00003000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A4u, 0x00000002u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A8u, 0x11000001u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.pending_dma_transfer().has_value());
    if (hw.pending_dma_transfer()) {
        CHECK(hw.pending_dma_transfer()->channel == 2u);
        CHECK(hw.pending_dma_transfer()->from_ram);
        CHECK(hw.pending_dma_transfer()->words == 2u);
    }
    hw.cancel_pending_dma_transfer();

    // Request mode 1 uses BCR block-size * block-count and does
    // not require the manual trigger bit. JoJo uses this for SPU DMA.
    const std::uint32_t ch4_enable = 1u << (4u * 4u + 3u);
    CHECK(hw.write32(0x1F8010F0u, ch4_enable).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C0u, 0x00004000u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C4u, 0x00030010u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010C8u, 0x01000201u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.pending_dma_transfer().has_value());
    if (hw.pending_dma_transfer()) {
        CHECK(hw.pending_dma_transfer()->channel == 4u);
        CHECK(hw.pending_dma_transfer()->madr == 0x00004000u);
        CHECK(hw.pending_dma_transfer()->words == 48u);
        CHECK(hw.pending_dma_transfer()->from_ram);
    }
    hw.cancel_pending_dma_transfer();

    // CD request mode is also accepted with device->RAM direction.
    CHECK(hw.write32(0x1F8010F0u, ch3_enable).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B0u, 0x00005000u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B4u, 0x00020020u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010B8u, 0x01000200u).status ==
          jojo::R3000aBusStatus::ok);
    CHECK(hw.pending_dma_transfer().has_value());
    if (hw.pending_dma_transfer()) {
        CHECK(hw.pending_dma_transfer()->channel == 3u);
        CHECK(hw.pending_dma_transfer()->words == 64u);
        CHECK(!hw.pending_dma_transfer()->from_ram);
    }
    hw.cancel_pending_dma_transfer();

    const std::uint32_t ch0_enable = 1u << 3u;
    CHECK(hw.write32(0x1F8010F0u, ch0_enable).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F801088u, 0x11000000u).status == jojo::R3000aBusStatus::unsupported);
    CHECK(!hw.pending_dma_transfer().has_value());

    return failures ? 1 : 0;
}
