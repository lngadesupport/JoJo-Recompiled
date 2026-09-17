#pragma once

#include "core/ps1_boot_report.h"
#include "core/ps1_exe.h"
#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"
#include "core/r3000a_state.h"
#include "core/result.h"

#include <cstdint>
#include <optional>

namespace jojo {

enum class Ps1BiosFallback : std::uint8_t {
    return_zero,
    return_one,
    return_minus_one,
    preserve_v0,
};

class Ps1BootRuntime {
public:
    Ps1BootRuntime() = default;

    [[nodiscard]] static Result<Ps1BootRuntime> create(
        const Ps1Executable& executable);

    [[nodiscard]] Ps1BootReport run(const Ps1BootOptions& options) noexcept;
    [[nodiscard]] bool apply_diagnostic_bios_fallback(Ps1BiosFallback fallback) noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    [[nodiscard]] const R3000aState& cpu_state() const noexcept;
    [[nodiscard]] const std::optional<Ps1BiosHeapState>& bios_heap_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& bios_interrupt_hook_address() const noexcept;
    [[nodiscard]] const std::optional<bool>& bios_pad_card_auto_ack_enabled() const noexcept;
    [[nodiscard]] std::optional<bool> bios_root_counter_auto_ack_enabled(
        std::uint32_t counter) const noexcept;
    [[nodiscard]] bool bios_iso9660_removed() const noexcept;
    [[nodiscard]] Ps1MemoryBus& bus() noexcept;
    [[nodiscard]] const Ps1MemoryBus& bus() const noexcept;

private:
    Ps1MemoryBus bus_{};
    R3000aState cpu_{};
    Ps1HleBios bios_{};
    bool diagnostic_bios_frontier_pending_{};
};

} // namespace jojo
