#pragma once

#include "core/r3000a_bus.h"
#include "core/r3000a_state.h"
#include "core/ps1_sio0.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace jojo {

inline constexpr std::uint32_t kPs1HleC0TableAddress = 0x00000674u;
inline constexpr std::uint32_t kPs1HleExceptionHandlerAddress = 0x00000C80u;
inline constexpr std::uint32_t kPs1HleB0TableAddress = 0x0000D000u;
inline constexpr std::uint32_t kPs1HleChangeClearPadHandlerAddress = 0x0000E000u;
inline constexpr std::uint32_t kPs1HleInitPad2HandlerAddress = 0x0000E010u;
inline constexpr std::uint32_t kPs1HleStartPad2HandlerAddress = 0x0000E020u;
inline constexpr std::uint32_t kPs1HleStopPad2HandlerAddress = 0x0000E030u;
inline constexpr std::uint32_t kPs1HlePadInit2HandlerAddress = 0x0000E040u;
inline constexpr std::uint32_t kPs1HlePadDrHandlerAddress = 0x0000E050u;
// JoJo applies the documented BIOS pad patch relative to B(5Bh), retaining
// callable pointers to these two internal routines.
inline constexpr std::uint32_t kPs1HleSetPadEnableHandlerAddress =
    kPs1HleChangeClearPadHandlerAddress + 0x0884u;
inline constexpr std::uint32_t kPs1HleClearPadEnableHandlerAddress =
    kPs1HleChangeClearPadHandlerAddress + 0x0894u;

enum class Ps1HleBiosDispatchStatus : std::uint8_t {
    handled,
    unimplemented,
};

struct Ps1BiosHeapState {
    std::uint32_t base{};
    std::uint32_t size{};
};

struct Ps1BiosEventState {
    bool allocated{};
    bool enabled{};
    bool ready{};
    std::uint32_t event_class{};
    std::uint32_t spec{};
    std::uint32_t mode{};
    std::uint32_t function{};
};

struct Ps1BiosThreadState {
    bool allocated{};
    R3000aState cpu{};
};

class Ps1HleBios {
public:
    Ps1HleBios() noexcept;
    [[nodiscard]] Ps1HleBiosDispatchStatus dispatch(
        R3000aState& cpu,
        std::uint32_t table_physical,
        std::uint32_t selector,
        R3000aBus* bus = nullptr) noexcept;
    [[nodiscard]] Ps1HleBiosDispatchStatus dispatch_syscall(
        R3000aState& cpu,
        std::uint32_t selector) noexcept;
    [[nodiscard]] Ps1HleBiosDispatchStatus dispatch_internal(
        R3000aState& cpu,
        std::uint32_t physical_address) noexcept;

    [[nodiscard]] Ps1HleBiosDispatchStatus begin_interrupt_hook(
        R3000aState& cpu,
        const R3000aState& resume_state,
        R3000aBus& bus) noexcept;

    void deliver_event(
        std::uint32_t event_class,
        std::uint32_t spec) noexcept;
    void service_pad_vblank(
        R3000aBus& bus,
        Ps1Sio0& sio0) noexcept;

    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    [[nodiscard]] const std::optional<Ps1BiosHeapState>& heap_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& interrupt_hook_address() const noexcept;
    [[nodiscard]] const std::optional<bool>& pad_card_auto_ack_enabled() const noexcept;
    [[nodiscard]] bool card_initialized() const noexcept;
    [[nodiscard]] bool card_started() const noexcept;
    [[nodiscard]] bool card_pad_enabled() const noexcept;
    [[nodiscard]] bool backup_unit_initialized() const noexcept;
    [[nodiscard]] const std::array<Ps1BiosEventState, 16>& events() const noexcept;
    [[nodiscard]] const std::array<Ps1BiosThreadState, 4>& threads() const noexcept;
    [[nodiscard]] std::uint32_t current_thread_handle() const noexcept;
    [[nodiscard]] std::optional<bool> root_counter_auto_ack_enabled(
        std::uint32_t counter) const noexcept;
    [[nodiscard]] bool iso9660_removed() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> interrupt_priority_head(
        std::uint32_t priority) const noexcept;
    [[nodiscard]] std::uint64_t pad_bios_call_count(
        std::uint32_t selector) const noexcept;
    [[nodiscard]] std::uint64_t pad_internal_set_call_count() const noexcept;
    [[nodiscard]] std::uint64_t pad_internal_clear_call_count() const noexcept;

private:
    std::optional<Ps1BiosHeapState> heap_state_{};
    std::optional<std::uint32_t> interrupt_hook_address_{};
    std::optional<R3000aState> interrupt_resume_state_{};
    std::optional<bool> pad_card_auto_ack_enabled_{};
    bool pad_initialized_{};
    bool pad_started_{};
    bool pad_enabled_{};
    std::array<std::uint32_t, 2> pad_buffer_addresses_{};
    std::array<std::uint32_t, 2> pad_buffer_sizes_{};
    std::optional<std::uint32_t> pad_button_destination_{};
    std::array<std::uint16_t, 2> pad_last_buttons_{0xFFFFu, 0xFFFFu};
    // Observation-only counters. Deliberately excluded from
    // diagnostic_state_hash() so instrumentation cannot alter replay state.
    std::array<std::uint64_t, 5> pad_bios_call_counts_{};
    std::uint64_t pad_internal_set_call_count_{};
    std::uint64_t pad_internal_clear_call_count_{};
    bool card_initialized_{};
    bool card_started_{};
    bool card_pad_enabled_{};
    bool backup_unit_initialized_{};
    std::array<Ps1BiosEventState, 16> events_{};
    std::array<Ps1BiosThreadState, 4> threads_{};
    std::size_t current_thread_index_{};
    std::array<std::optional<bool>, 4> root_counter_auto_ack_enabled_{};
    std::array<std::optional<std::uint32_t>, 4> interrupt_priority_heads_{};
    bool iso9660_removed_{};
};

} // namespace jojo
