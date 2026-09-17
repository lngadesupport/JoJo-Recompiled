#pragma once

#include <cstdint>
#include <optional>

namespace jojo {

enum class R3000aExceptionCode : std::uint8_t {
    interrupt = 0,
    adel = 4,
    ades = 5,
    ibe = 6,
    dbe = 7,
    syscall = 8,
    breakpoint = 9,
    reserved_instruction = 10,
    coprocessor_unusable = 11,
    overflow = 12,
};

enum class R3000aBoundaryCode : std::uint8_t {
    none,
    unsupported_address_space,
    architectural_operation_unimplemented,
    cop2_unimplemented,
    unpredictable_delay_slot_control_transfer,
};

enum class R3000aStage : std::uint8_t {
    interrupt,
    fetch,
    decode,
    execute,
    memory,
    cop0,
    cop2,
};

enum class R3000aStepStatus : std::uint8_t {
    retired,
    exception,
    boundary,
};

struct R3000aDiagnostic {
    R3000aBoundaryCode boundary{R3000aBoundaryCode::none};
    R3000aStage stage{R3000aStage::execute};
    std::uint32_t pc{};
    std::optional<std::uint32_t> opcode{};
    std::optional<std::uint32_t> address{};
    std::optional<std::uint32_t> write_value{};
    std::optional<std::uint8_t> access_width{};
    std::optional<std::uint8_t> coprocessor{};
    std::optional<std::uint8_t> register_index{};
    std::optional<R3000aExceptionCode> exception_code{};
};

struct R3000aStepResult {
    R3000aStepStatus status{R3000aStepStatus::retired};
    R3000aDiagnostic diagnostic{};
};

} // namespace jojo
