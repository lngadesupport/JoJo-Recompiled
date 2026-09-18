#include "core/ps1_gpu_ingress.h"
#include "core/ps1_hardware_services.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::Ps1GpuIngress gpu;

    const auto reset_status = gpu.status();
    CHECK(reset_status != 0u);
    CHECK(gpu.write_gp1(0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.gp1_command_count() == 1u);

    CHECK(gpu.write_gp1(0x03000001u).status == jojo::R3000aBusStatus::ok);
    CHECK((gpu.status() & (1u << 23u)) != 0u);

    CHECK(gpu.write_gp0(0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(gpu.gp0_word_count() == 1u);

    const auto unsupported = gpu.write_gp0(0x04000000u);
    CHECK(unsupported.status == jojo::R3000aBusStatus::unsupported);
    CHECK(gpu.last_unsupported_gp0_command().has_value());
    if (gpu.last_unsupported_gp0_command()) {
        CHECK(*gpu.last_unsupported_gp0_command() == 0x04u);
    }

    jojo::Ps1HardwareServices hw;
    CHECK(hw.write32(0x1F801814u, 0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F801810u, 0x00000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.gpu_gp0_word_count() == 1u);

    // GPUREAD is routed through the same hardware-services MMIO address.
    CHECK(hw.write32(0x1F801810u, 0xA0000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F801810u, (5u << 16u) | 4u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F801810u, (1u << 16u) | 2u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F801810u, 0x56781234u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F801810u, 0xC0000000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F801810u, (5u << 16u) | 4u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F801810u, (1u << 16u) | 2u).status == jojo::R3000aBusStatus::ok);
    const auto gpuread = hw.read32(0x1F801810u);
    CHECK(gpuread.status == jojo::R3000aBusStatus::ok);
    CHECK(gpuread.value == 0x56781234u);
    CHECK(hw.gpu_gp1_command_count() == 1u);

    // DMA2 RAM -> GP0 must use the same ingress path.
    std::vector<std::uint8_t> ram(2u * 1024u * 1024u, 0u);
    ram[0x1000u] = 0x00u;
    ram[0x1001u] = 0x00u;
    ram[0x1002u] = 0x00u;
    ram[0x1003u] = 0x00u;
    ram[0x1004u] = 0x00u;
    ram[0x1005u] = 0x00u;
    ram[0x1006u] = 0x00u;
    ram[0x1007u] = 0x00u;

    const std::uint32_t ch2_enable = 1u << (2u * 4u + 3u);
    CHECK(hw.write32(0x1F8010F0u, ch2_enable).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A0u, 0x00001000u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A4u, 2u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.write32(0x1F8010A8u, 0x11000001u).status == jojo::R3000aBusStatus::ok);
    CHECK(hw.execute_pending_dma(ram));
    CHECK(hw.completed_dma_transfer_count() == 1u);
    CHECK(hw.gpu_gp0_word_count() == 3u);

    return failures ? 1 : 0;
}
