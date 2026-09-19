#pragma once

#include "core/ps1_boot_report.h"
#include "core/ps1_display_frame.h"
#include "core/ps1_exe.h"
#include "core/ps1_hle_bios.h"
#include "core/ps1_memory_bus.h"
#include "core/r3000a_state.h"
#include "core/r3000a_x64_cache.h"
#include "core/result.h"

#include <cstdint>
#include <optional>

namespace jojo {

enum class Ps1InterruptChainPhase : std::uint8_t {
    first,
    second,
};

struct Ps1InterruptChainState {
    bool active{};
    R3000aState resume_state{};
    std::uint32_t priority{};
    std::uint32_t node{};
    std::uint32_t next_node{};
    std::uint32_t second_function{};
    Ps1InterruptChainPhase phase{Ps1InterruptChainPhase::first};
    std::uint32_t nodes_visited{};
};

struct Ps1BootRuntimeState {
    Ps1MemoryBus bus{};
    R3000aState cpu{};
    Ps1HleBios bios{};
    std::uint64_t native_text_begin{};
    std::uint64_t native_text_end{};
    bool native_x64_enabled{};
    bool diagnostic_bios_frontier_pending{};
    Ps1InterruptChainState interrupt_chain{};
};

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
    void set_native_x64_enabled(bool enabled) noexcept;
    [[nodiscard]] bool native_x64_enabled() const noexcept;
    void signal_vblank() noexcept;
    [[nodiscard]] bool apply_diagnostic_bios_fallback(Ps1BiosFallback fallback) noexcept;
    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;
    [[nodiscard]] Ps1DisplayFrame display_frame() const;
    [[nodiscard]] Ps1BootRuntimeState save_state() const;
    [[nodiscard]] Result<void> load_state(const Ps1BootRuntimeState& state);

    [[nodiscard]] const R3000aState& cpu_state() const noexcept;
    [[nodiscard]] const std::optional<Ps1BiosHeapState>& bios_heap_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& bios_interrupt_hook_address() const noexcept;
    [[nodiscard]] const std::optional<bool>& bios_pad_card_auto_ack_enabled() const noexcept;
    [[nodiscard]] std::uint64_t bios_pad_call_count(
        std::uint32_t selector) const noexcept;
    [[nodiscard]] std::uint64_t bios_pad_internal_set_call_count() const noexcept;
    [[nodiscard]] std::uint64_t bios_pad_internal_clear_call_count() const noexcept;
    [[nodiscard]] std::optional<bool> bios_root_counter_auto_ack_enabled(
        std::uint32_t counter) const noexcept;
    [[nodiscard]] bool bios_iso9660_removed() const noexcept;
    [[nodiscard]] Ps1MemoryBus& bus() noexcept;
    [[nodiscard]] const Ps1MemoryBus& bus() const noexcept;

private:
    [[nodiscard]] bool begin_interrupt_priority_chain(
        const R3000aState& resume_state) noexcept;
    [[nodiscard]] bool continue_interrupt_priority_chain() noexcept;
    [[nodiscard]] bool enter_interrupt_chain_node() noexcept;
    void restore_interrupt_resume_state(
        const R3000aState& resume_state) noexcept;
    [[nodiscard]] bool mirror_jojo_pad_buffers() noexcept;

    Ps1MemoryBus bus_{};
    R3000aState cpu_{};
    Ps1HleBios bios_{};
    R3000aX64BlockCache native_x64_cache_{};
    std::uint64_t native_text_begin_{};
    std::uint64_t native_text_end_{};
    bool native_x64_enabled_{};
    bool diagnostic_bios_frontier_pending_{};
    Ps1InterruptChainState interrupt_chain_{};
};

} // namespace jojo
