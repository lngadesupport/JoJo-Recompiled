#pragma once

#include <array>
#include <cstdint>

namespace jojo {

struct R3000aDelayedLoad {
    bool valid{};
    std::uint8_t reg{};
    std::uint32_t value{};
};

struct R3000aDelaySlot {
    bool active{};
    std::uint32_t branch_pc{};
    bool taken{};
    std::uint32_t target{};
};

struct R3000aCop0 {
    std::uint32_t target_address{};
    std::uint32_t bad_vaddr{};
    std::uint32_t status{};
    std::uint32_t cause{};
    std::uint32_t epc{};
};

struct R3000aState {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t pc{};
    std::uint32_t next_pc{};
    R3000aDelayedLoad pending_load{};
    R3000aDelaySlot delay_slot{};
    R3000aCop0 cop0{};
    std::uint8_t external_interrupt_pending{};
};

} // namespace jojo
