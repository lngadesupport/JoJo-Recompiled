#pragma once

#include "core/r3000a_state.h"

#include <array>
#include <cstdint>
#include <optional>

namespace jojo {

inline constexpr std::uint32_t kPs1HleC0TableAddress = 0x00000674u;
inline constexpr std::uint32_t kPs1HleExceptionHandlerAddress = 0x00000C80u;

enum class Ps1HleBiosDispatchStatus : std::uint8_t {
    handled,
    unimplemented,
};

struct Ps1BiosHeapState {
    std::uint32_t base{};
    std::uint32_t size{};
};

class Ps1HleBios {
public:
    [[nodiscard]] Ps1HleBiosDispatchStatus dispatch(
        R3000aState& cpu,
        std::uint32_t table_physical,
        std::uint32_t selector) noexcept;
    [[nodiscard]] Ps1HleBiosDispatchStatus dispatch_syscall(
        R3000aState& cpu,
        std::uint32_t selector) noexcept;

    [[nodiscard]] std::uint64_t diagnostic_state_hash() const noexcept;

    [[nodiscard]] const std::optional<Ps1BiosHeapState>& heap_state() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& interrupt_hook_address() const noexcept;
    [[nodiscard]] const std::optional<bool>& pad_card_auto_ack_enabled() const noexcept;
    [[nodiscard]] std::optional<bool> root_counter_auto_ack_enabled(
        std::uint32_t counter) const noexcept;
    [[nodiscard]] bool iso9660_removed() const noexcept;

private:
    std::optional<Ps1BiosHeapState> heap_state_{};
    std::optional<std::uint32_t> interrupt_hook_address_{};
    std::optional<bool> pad_card_auto_ack_enabled_{};
    std::array<std::optional<bool>, 4> root_counter_auto_ack_enabled_{};
    bool iso9660_removed_{};
};

} // namespace jojo
