#pragma once

#include "core/mips_decoder.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace jojo {

enum class R3000aIrTerminatorKind : std::uint8_t {
    fallthrough,
    conditional_branch,
    direct_jump,
    indirect_jump,
};

struct R3000aIrInstruction {
    std::uint32_t pc{};
    MipsInstruction decoded{};
    bool delay_slot{};
};

struct R3000aIrBlock {
    std::uint32_t entry_pc{};
    std::vector<R3000aIrInstruction> instructions;
    R3000aIrTerminatorKind terminator{R3000aIrTerminatorKind::fallthrough};
    std::optional<std::uint32_t> taken_target;
    std::optional<std::uint32_t> fallthrough_target;
    bool has_delay_slot{};
};

[[nodiscard]] Result<R3000aIrBlock> lift_r3000a_basic_block(
    std::uint32_t entry_pc,
    std::span<const std::uint32_t> words,
    std::size_t max_instructions = 64u) noexcept;

} // namespace jojo
