#pragma once

#include "core/r3000a_ir.h"
#include "core/r3000a_state.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace jojo {

struct R3000aX64MemoryAccess {
    MipsOp op{MipsOp::reserved};
    std::uint8_t rs{};
    std::uint8_t rt{};
    std::uint8_t width{};
    std::uint16_t immediate{};
    bool write{};
};

struct R3000aX64Code {
    std::uint32_t entry_pc{};
    std::size_t instruction_count{};
    std::vector<std::uint8_t> bytes;
    std::optional<R3000aX64MemoryAccess> memory_access;
    std::shared_ptr<void> executable_owner;
    const void* executable_entry{};
};

enum class R3000aX64ExecutionStatus : std::uint8_t {
    executed,
    reference_required,
    host_unavailable,
    host_error,
};

struct R3000aX64ExecutionResult {
    R3000aX64ExecutionStatus status{
        R3000aX64ExecutionStatus::reference_required};
    std::size_t instructions_retired{};
};

[[nodiscard]] bool r3000a_op_is_x64_lowerable(MipsOp op) noexcept;
[[nodiscard]] bool r3000a_op_is_x64_direct_lowerable(MipsOp op) noexcept;

[[nodiscard]] Result<R3000aX64Code> emit_r3000a_x64_instruction(
    std::uint32_t pc,
    const MipsInstruction& instruction) noexcept;

[[nodiscard]] Result<R3000aX64Code> emit_r3000a_x64_alu_block(
    const R3000aIrBlock& block) noexcept;

[[nodiscard]] R3000aX64ExecutionResult execute_r3000a_x64_block(
    const R3000aX64Code& code,
    R3000aState& state,
    std::uint8_t* main_ram = nullptr) noexcept;

} // namespace jojo
