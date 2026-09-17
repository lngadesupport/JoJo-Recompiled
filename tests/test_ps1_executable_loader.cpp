#include "core/ps1_executable_loader.h"
#include "core/ps1_memory_bus.h"
#include "core/ps1_exe.h"
#include "ps1_fixture.h"

#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    auto parsed = jojo::parse_ps1_executable(test_ps1::make_psx_exe());
    CHECK(parsed);
    if (!parsed) return failures ? 1 : 0;

    jojo::Ps1MemoryBus bus;
    auto loaded = jojo::load_ps1_executable_into_bus(bus, parsed.value);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.pc == 0x80010000u);
        CHECK(loaded.value.next_pc == 0x80010004u);
        CHECK(loaded.value.gpr[28] == 0x80018000u);
        CHECK(loaded.value.gpr[29] == 0x80200000u);
        CHECK(bus.read32(0x80010000u).value == 0x00000000u);
        CHECK(bus.read32(0x80010004u).value == 0x24080001u);
        CHECK(bus.read32(0x00010008u).value == 0x24090002u);
    }

    auto truncated = parsed.value;
    truncated.file_bytes.resize(0x800u + truncated.metadata.text_size - 1u);
    CHECK(!jojo::load_ps1_executable_into_bus(bus, truncated));

    auto outside = parsed.value;
    outside.metadata.text_load_address = 0x801FFFFCu;
    outside.metadata.text_size = 8u;
    outside.file_bytes.resize(0x808u, 0u);
    CHECK(!jojo::load_ps1_executable_into_bus(bus, outside));

    auto wraps = parsed.value;
    wraps.metadata.text_load_address = 0xFFFFFFFCu;
    wraps.metadata.text_size = 8u;
    wraps.file_bytes.resize(0x808u, 0u);
    CHECK(!jojo::load_ps1_executable_into_bus(bus, wraps));

    return failures ? 1 : 0;
}
