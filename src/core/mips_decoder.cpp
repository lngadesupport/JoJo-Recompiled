#include "core/mips_decoder.h"

namespace jojo {
namespace {

MipsOp decode_special(std::uint32_t raw) noexcept {
    switch (raw & 0x3fu) {
        case 0x00: return MipsOp::sll;
        case 0x02: return MipsOp::srl;
        case 0x03: return MipsOp::sra;
        case 0x04: return MipsOp::sllv;
        case 0x06: return MipsOp::srlv;
        case 0x07: return MipsOp::srav;
        case 0x08: return MipsOp::jr;
        case 0x09: return MipsOp::jalr;
        case 0x0c: return MipsOp::syscall;
        case 0x0d: return MipsOp::break_;
        case 0x10: return MipsOp::mfhi;
        case 0x11: return MipsOp::mthi;
        case 0x12: return MipsOp::mflo;
        case 0x13: return MipsOp::mtlo;
        case 0x18: return MipsOp::mult;
        case 0x19: return MipsOp::multu;
        case 0x1a: return MipsOp::div;
        case 0x1b: return MipsOp::divu;
        case 0x20: return MipsOp::add;
        case 0x21: return MipsOp::addu;
        case 0x22: return MipsOp::sub;
        case 0x23: return MipsOp::subu;
        case 0x24: return MipsOp::bit_and;
        case 0x25: return MipsOp::bit_or;
        case 0x26: return MipsOp::bit_xor;
        case 0x27: return MipsOp::bit_nor;
        case 0x2a: return MipsOp::slt;
        case 0x2b: return MipsOp::sltu;
        default: return MipsOp::reserved;
    }
}

MipsOp decode_regimm(std::uint8_t rt) noexcept {
    switch (rt) {
        case 0x00: return MipsOp::bltz;
        case 0x01: return MipsOp::bgez;
        case 0x10: return MipsOp::bltzal;
        case 0x11: return MipsOp::bgezal;
        default: return MipsOp::reserved;
    }
}

MipsOp decode_cop0(std::uint32_t raw, std::uint8_t rs) noexcept {
    if (rs == 0x00u) return MipsOp::mfc0;
    if (rs == 0x04u) return MipsOp::mtc0;
    if (rs == 0x10u && (raw & 0x01ffffffu) == 0x00000010u) return MipsOp::rfe;
    return MipsOp::reserved;
}

MipsOp decode_cop2(std::uint8_t rs) noexcept {
    switch (rs) {
        case 0x00: return MipsOp::mfc2;
        case 0x02: return MipsOp::cfc2;
        case 0x04: return MipsOp::mtc2;
        case 0x06: return MipsOp::ctc2;
        default:
            if ((rs & 0x10u) != 0u) return MipsOp::cop2_command;
            return MipsOp::reserved;
    }
}

} // namespace

MipsInstruction decode_mips(std::uint32_t raw) noexcept {
    MipsInstruction out{
        raw,
        MipsOp::reserved,
        static_cast<std::uint8_t>((raw >> 21) & 31u),
        static_cast<std::uint8_t>((raw >> 16) & 31u),
        static_cast<std::uint8_t>((raw >> 11) & 31u),
        static_cast<std::uint8_t>((raw >> 6) & 31u),
        static_cast<std::uint16_t>(raw & 0xffffu),
        raw & 0x03ffffffu,
    };

    const auto opcode = static_cast<std::uint8_t>((raw >> 26) & 0x3fu);
    switch (opcode) {
        case 0x00: out.op = decode_special(raw); break;
        case 0x01: out.op = decode_regimm(out.rt); break;
        case 0x02: out.op = MipsOp::j; break;
        case 0x03: out.op = MipsOp::jal; break;
        case 0x04: out.op = MipsOp::beq; break;
        case 0x05: out.op = MipsOp::bne; break;
        case 0x06: out.op = MipsOp::blez; break;
        case 0x07: out.op = MipsOp::bgtz; break;
        case 0x08: out.op = MipsOp::addi; break;
        case 0x09: out.op = MipsOp::addiu; break;
        case 0x0a: out.op = MipsOp::slti; break;
        case 0x0b: out.op = MipsOp::sltiu; break;
        case 0x0c: out.op = MipsOp::andi; break;
        case 0x0d: out.op = MipsOp::ori; break;
        case 0x0e: out.op = MipsOp::xori; break;
        case 0x0f: out.op = MipsOp::lui; break;
        case 0x10: out.op = decode_cop0(raw, out.rs); break;
        case 0x12: out.op = decode_cop2(out.rs); break;
        case 0x20: out.op = MipsOp::lb; break;
        case 0x21: out.op = MipsOp::lh; break;
        case 0x22: out.op = MipsOp::lwl; break;
        case 0x23: out.op = MipsOp::lw; break;
        case 0x24: out.op = MipsOp::lbu; break;
        case 0x25: out.op = MipsOp::lhu; break;
        case 0x26: out.op = MipsOp::lwr; break;
        case 0x28: out.op = MipsOp::sb; break;
        case 0x29: out.op = MipsOp::sh; break;
        case 0x2a: out.op = MipsOp::swl; break;
        case 0x2b: out.op = MipsOp::sw; break;
        case 0x2e: out.op = MipsOp::swr; break;
        default: break;
    }

    return out;
}

bool is_control_transfer(MipsOp op) noexcept {
    switch (op) {
        case MipsOp::j:
        case MipsOp::jal:
        case MipsOp::jr:
        case MipsOp::jalr:
        case MipsOp::beq:
        case MipsOp::bne:
        case MipsOp::blez:
        case MipsOp::bgtz:
        case MipsOp::bltz:
        case MipsOp::bgez:
        case MipsOp::bltzal:
        case MipsOp::bgezal:
            return true;
        default:
            return false;
    }
}

} // namespace jojo
