#pragma once

#include "core/r3000a_ir.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace jojo {

struct R3000aCfg {
    std::uint32_t program_base_pc{};
    std::uint32_t entry_pc{};
    std::vector<R3000aIrBlock> blocks;
};

[[nodiscard]] Result<R3000aCfg> build_r3000a_cfg(
    std::uint32_t program_base_pc,
    std::span<const std::uint32_t> words,
    std::uint32_t entry_pc,
    std::size_t max_blocks = 4096u,
    std::size_t max_instructions_per_block = 64u) noexcept;

} // namespace jojo
