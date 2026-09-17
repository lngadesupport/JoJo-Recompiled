#pragma once

#include <cstdint>

namespace jojo {

enum class MipsOp : std::uint16_t {
    reserved,
    sll,
    srl,
    sra,
    sllv,
    srlv,
    srav,
    add,
    addu,
    sub,
    subu,
    bit_and,
    bit_or,
    bit_xor,
    bit_nor,
    slt,
    sltu,
    addi,
    addiu,
    andi,
    ori,
    xori,
    lui,
    slti,
    sltiu,
    mfhi,
    mthi,
    mflo,
    mtlo,
    mult,
    multu,
    div,
    divu,
    j,
    jal,
    jr,
    jalr,
    beq,
    bne,
    blez,
    bgtz,
    bltz,
    bgez,
    bltzal,
    bgezal,
    lb,
    lbu,
    lh,
    lhu,
    lw,
    sb,
    sh,
    sw,
    lwl,
    lwr,
    swl,
    swr,
    syscall,
    break_,
    mfc0,
    mtc0,
    rfe,
    mfc2,
    cfc2,
    mtc2,
    ctc2,
    cop2_command,
};

struct MipsInstruction {
    std::uint32_t raw{};
    MipsOp op{MipsOp::reserved};
    std::uint8_t rs{};
    std::uint8_t rt{};
    std::uint8_t rd{};
    std::uint8_t sa{};
    std::uint16_t immediate{};
    std::uint32_t target{};
};

[[nodiscard]] MipsInstruction decode_mips(std::uint32_t raw) noexcept;
[[nodiscard]] bool is_control_transfer(MipsOp op) noexcept;

} // namespace jojo
