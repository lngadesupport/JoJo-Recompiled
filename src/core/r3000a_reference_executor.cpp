#include "core/r3000a_reference_executor.h"

#include "core/mips_decoder.h"

#include <bit>
#include <cstdint>
#include <optional>

namespace jojo {
namespace {

constexpr std::uint32_t kCauseExcCodeMask = 0x0000007cu;
constexpr std::uint32_t kCauseContextMask = 0xF0000000u; // BD, BT, CE
constexpr std::uint32_t kCauseBd = 1u << 31;
constexpr std::uint32_t kCauseBt = 1u << 30;
constexpr std::uint32_t kCauseSwMask = 0x00000300u;
constexpr std::uint32_t kCauseExternalMask = 0x0000FC00u;
constexpr std::uint32_t kCauseInterruptMask = 0x0000FF00u;
constexpr std::uint32_t kStatusModeStackMask = 0x0000003fu;
constexpr std::uint32_t kStatusBev = 1u << 22;
constexpr std::uint32_t kStatusCu2 = 1u << 30;
constexpr std::uint32_t kStatusWritableMask = 0xF27FFF3Fu;

std::uint32_t sign_extend8(std::uint8_t value) noexcept {
    return (value & 0x80u) != 0u ? (0xffffff00u | static_cast<std::uint32_t>(value))
                                : static_cast<std::uint32_t>(value);
}

std::uint32_t sign_extend16(std::uint16_t value) noexcept {
    return (value & 0x8000u) != 0u ? (0xffff0000u | static_cast<std::uint32_t>(value))
                                  : static_cast<std::uint32_t>(value);
}

std::int32_t signed_view(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

std::uint32_t arithmetic_shift_right(std::uint32_t value, std::uint32_t amount) noexcept {
    amount &= 31u;
    if (amount == 0u) return value;
    const auto shifted = value >> amount;
    if ((value & 0x80000000u) == 0u) return shifted;
    return shifted | (0xffffffffu << (32u - amount));
}

bool add_overflow(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t result) noexcept {
    return ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) != 0u;
}

bool sub_overflow(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t result) noexcept {
    return (((lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) != 0u;
}

bool cop0_register_is_ri(std::uint8_t rd) noexcept {
    return rd == 0u || rd == 1u || rd == 2u || rd == 4u || rd == 10u;
}

void write_gpr(R3000aState& state, std::uint8_t reg, std::uint32_t value) noexcept {
    if (reg != 0u) state.gpr[reg] = value;
}

void retire_pending_load(R3000aState& state) noexcept {
    if (state.pending_load.valid) {
        write_gpr(state, state.pending_load.reg, state.pending_load.value);
        state.pending_load = {};
    }
}

void advance_ordinary_pc(R3000aState& state) noexcept {
    state.pc = state.next_pc;
    state.next_pc = state.next_pc + 4u;
}

R3000aStepResult boundary(
    R3000aState& state,
    R3000aBoundaryCode code,
    R3000aStage stage,
    std::uint32_t pc,
    std::optional<std::uint32_t> opcode = std::nullopt) noexcept {
    state.gpr[0] = 0u;
    R3000aStepResult result{};
    result.status = R3000aStepStatus::boundary;
    result.diagnostic.boundary = code;
    result.diagnostic.stage = stage;
    result.diagnostic.pc = pc;
    result.diagnostic.opcode = opcode;
    return result;
}

R3000aStepResult enter_exception(
    R3000aState& state,
    R3000aExceptionCode code,
    R3000aStage stage,
    std::uint32_t fault_pc,
    const R3000aDelaySlot& current_delay,
    std::optional<std::uint32_t> opcode = std::nullopt,
    std::optional<std::uint32_t> bad_vaddr = std::nullopt,
    std::optional<std::uint8_t> coprocessor = std::nullopt) noexcept {
    const auto status_before = state.cop0.status;
    state.cop0.cause &= ~(kCauseExcCodeMask | kCauseContextMask);
    state.cop0.cause |= static_cast<std::uint32_t>(code) << 2;
    if (coprocessor) state.cop0.cause |= (static_cast<std::uint32_t>(*coprocessor & 3u) << 28);

    if (current_delay.active) {
        state.cop0.cause |= kCauseBd;
        state.cop0.epc = current_delay.branch_pc;
        if (current_delay.taken) {
            state.cop0.cause |= kCauseBt;
            state.cop0.target_address = current_delay.target;
        }
    } else {
        state.cop0.epc = fault_pc;
    }

    if (code == R3000aExceptionCode::adel || code == R3000aExceptionCode::ades) {
        if (bad_vaddr) state.cop0.bad_vaddr = *bad_vaddr;
    }

    const auto low_stack = status_before & kStatusModeStackMask;
    state.cop0.status = (status_before & ~kStatusModeStackMask) |
                        ((low_stack << 2) & kStatusModeStackMask);

    const std::uint32_t vector = (status_before & kStatusBev) != 0u
        ? 0xBFC00180u
        : 0x80000080u;
    state.pc = vector;
    state.next_pc = vector + 4u;
    state.delay_slot = {};
    state.gpr[0] = 0u;

    R3000aStepResult result{};
    result.status = R3000aStepStatus::exception;
    result.diagnostic.stage = stage;
    result.diagnostic.pc = fault_pc;
    result.diagnostic.opcode = opcode;
    result.diagnostic.exception_code = code;
    result.diagnostic.coprocessor = coprocessor;
    if (bad_vaddr) result.diagnostic.address = *bad_vaddr;
    return result;
}

} // namespace

R3000aStepResult step_r3000a(R3000aState& state, R3000aBus& bus) noexcept {
    state.gpr[0] = 0u;
    const std::uint32_t instruction_pc = state.pc;
    const R3000aDelaySlot current_delay = state.delay_slot;

    state.cop0.cause = (state.cop0.cause & ~kCauseExternalMask) |
                       (static_cast<std::uint32_t>(state.external_interrupt_pending & 0xFCu) << 8);
    const bool interrupt_enabled = (state.cop0.status & 1u) != 0u;
    const bool masked_interrupt_pending =
        (state.cop0.cause & state.cop0.status & kCauseInterruptMask) != 0u;
    if (!current_delay.active && interrupt_enabled && masked_interrupt_pending) {
        retire_pending_load(state);
        return enter_exception(state, R3000aExceptionCode::interrupt, R3000aStage::interrupt,
                               instruction_pc, current_delay);
    }

    if ((instruction_pc & 3u) != 0u) {
        retire_pending_load(state);
        auto result = enter_exception(state, R3000aExceptionCode::adel, R3000aStage::fetch,
                                      instruction_pc, current_delay, std::nullopt, instruction_pc);
        result.diagnostic.access_width = 4u;
        return result;
    }

    const auto fetched = bus.read32(instruction_pc);
    if (fetched.status == R3000aBusStatus::unsupported) {
        retire_pending_load(state);
        auto result = boundary(state, R3000aBoundaryCode::unsupported_address_space,
                               R3000aStage::fetch, instruction_pc);
        result.diagnostic.address = instruction_pc;
        result.diagnostic.access_width = 4u;
        return result;
    }
    if (fetched.status == R3000aBusStatus::bus_error) {
        retire_pending_load(state);
        auto result = enter_exception(state, R3000aExceptionCode::ibe, R3000aStage::fetch,
                                      instruction_pc, current_delay);
        result.diagnostic.address = instruction_pc;
        result.diagnostic.access_width = 4u;
        return result;
    }

    const auto instruction = decode_mips(fetched.value);
    if (current_delay.active && is_control_transfer(instruction.op)) {
        return boundary(state, R3000aBoundaryCode::unpredictable_delay_slot_control_transfer,
                        R3000aStage::execute, instruction_pc, instruction.raw);
    }

    const std::uint32_t rs = state.gpr[instruction.rs];
    const std::uint32_t rt = state.gpr[instruction.rt];
    const R3000aDelayedLoad prior_pending = state.pending_load;
    retire_pending_load(state);

    bool supported = true;
    bool scheduled_control_transfer = false;
    bool direct_write_valid = false;
    std::uint8_t direct_write_reg = 0u;
    std::uint32_t direct_write_value = 0u;
    R3000aDelayedLoad new_pending{};

    const auto queue_gpr_write = [&](std::uint8_t reg, std::uint32_t value) noexcept {
        direct_write_valid = reg != 0u;
        direct_write_reg = reg;
        direct_write_value = value;
    };
    const auto queue_load = [&](std::uint8_t reg, std::uint32_t value) noexcept {
        if (reg != 0u) new_pending = {true, reg, value};
    };
    const auto branch_target = [&]() noexcept {
        return instruction_pc + 4u + (sign_extend16(instruction.immediate) << 2);
    };
    const auto schedule_control_transfer = [&](bool taken, std::uint32_t target) noexcept {
        state.delay_slot = {true, instruction_pc, taken, target};
        state.pc = instruction_pc + 4u;
        state.next_pc = taken ? target : instruction_pc + 8u;
        scheduled_control_transfer = true;
    };
    const auto data_boundary = [&](std::uint32_t address, std::uint8_t width,
                                   std::optional<std::uint32_t> write_value = std::nullopt) noexcept {
        auto result = boundary(state, R3000aBoundaryCode::unsupported_address_space,
                               R3000aStage::memory, instruction_pc, instruction.raw);
        result.diagnostic.address = address;
        result.diagnostic.access_width = width;
        result.diagnostic.write_value = write_value;
        return result;
    };
    const auto data_bus_error = [&](std::uint32_t address, std::uint8_t width,
                                    std::optional<std::uint32_t> write_value = std::nullopt) noexcept {
        auto result = enter_exception(state, R3000aExceptionCode::dbe, R3000aStage::memory,
                                      instruction_pc, current_delay, instruction.raw);
        result.diagnostic.address = address;
        result.diagnostic.access_width = width;
        result.diagnostic.write_value = write_value;
        return result;
    };
    const auto address_exception = [&](R3000aExceptionCode code, std::uint32_t address,
                                       std::uint8_t width) noexcept {
        auto result = enter_exception(state, code, R3000aStage::memory, instruction_pc,
                                      current_delay, instruction.raw, address);
        result.diagnostic.access_width = width;
        return result;
    };
    const auto cop0_ri = [&]() noexcept {
        return enter_exception(state, R3000aExceptionCode::reserved_instruction, R3000aStage::cop0,
                               instruction_pc, current_delay, instruction.raw);
    };
    const auto cop0_boundary = [&]() noexcept {
        return boundary(state, R3000aBoundaryCode::architectural_operation_unimplemented,
                        R3000aStage::cop0, instruction_pc, instruction.raw);
    };

    switch (instruction.op) {
        case MipsOp::sll: queue_gpr_write(instruction.rd, rt << instruction.sa); break;
        case MipsOp::srl: queue_gpr_write(instruction.rd, rt >> instruction.sa); break;
        case MipsOp::sra: queue_gpr_write(instruction.rd, arithmetic_shift_right(rt, instruction.sa)); break;
        case MipsOp::sllv: queue_gpr_write(instruction.rd, rt << (rs & 31u)); break;
        case MipsOp::srlv: queue_gpr_write(instruction.rd, rt >> (rs & 31u)); break;
        case MipsOp::srav: queue_gpr_write(instruction.rd, arithmetic_shift_right(rt, rs)); break;
        case MipsOp::add: {
            const auto value = rs + rt;
            if (add_overflow(rs, rt, value))
                return enter_exception(state, R3000aExceptionCode::overflow, R3000aStage::execute,
                                       instruction_pc, current_delay, instruction.raw);
            queue_gpr_write(instruction.rd, value);
            break;
        }
        case MipsOp::addu: queue_gpr_write(instruction.rd, rs + rt); break;
        case MipsOp::sub: {
            const auto value = rs - rt;
            if (sub_overflow(rs, rt, value))
                return enter_exception(state, R3000aExceptionCode::overflow, R3000aStage::execute,
                                       instruction_pc, current_delay, instruction.raw);
            queue_gpr_write(instruction.rd, value);
            break;
        }
        case MipsOp::subu: queue_gpr_write(instruction.rd, rs - rt); break;
        case MipsOp::bit_and: queue_gpr_write(instruction.rd, rs & rt); break;
        case MipsOp::bit_or: queue_gpr_write(instruction.rd, rs | rt); break;
        case MipsOp::bit_xor: queue_gpr_write(instruction.rd, rs ^ rt); break;
        case MipsOp::bit_nor: queue_gpr_write(instruction.rd, ~(rs | rt)); break;
        case MipsOp::slt: queue_gpr_write(instruction.rd, signed_view(rs) < signed_view(rt) ? 1u : 0u); break;
        case MipsOp::sltu: queue_gpr_write(instruction.rd, rs < rt ? 1u : 0u); break;
        case MipsOp::addi: {
            const auto immediate = sign_extend16(instruction.immediate);
            const auto value = rs + immediate;
            if (add_overflow(rs, immediate, value))
                return enter_exception(state, R3000aExceptionCode::overflow, R3000aStage::execute,
                                       instruction_pc, current_delay, instruction.raw);
            queue_gpr_write(instruction.rt, value);
            break;
        }
        case MipsOp::addiu: queue_gpr_write(instruction.rt, rs + sign_extend16(instruction.immediate)); break;
        case MipsOp::andi: queue_gpr_write(instruction.rt, rs & static_cast<std::uint32_t>(instruction.immediate)); break;
        case MipsOp::ori: queue_gpr_write(instruction.rt, rs | static_cast<std::uint32_t>(instruction.immediate)); break;
        case MipsOp::xori: queue_gpr_write(instruction.rt, rs ^ static_cast<std::uint32_t>(instruction.immediate)); break;
        case MipsOp::lui: queue_gpr_write(instruction.rt, static_cast<std::uint32_t>(instruction.immediate) << 16); break;
        case MipsOp::slti:
            queue_gpr_write(instruction.rt, signed_view(rs) < signed_view(sign_extend16(instruction.immediate)) ? 1u : 0u);
            break;
        case MipsOp::sltiu:
            queue_gpr_write(instruction.rt, rs < sign_extend16(instruction.immediate) ? 1u : 0u);
            break;
        case MipsOp::mfhi: queue_gpr_write(instruction.rd, state.hi); break;
        case MipsOp::mthi: state.hi = rs; break;
        case MipsOp::mflo: queue_gpr_write(instruction.rd, state.lo); break;
        case MipsOp::mtlo: state.lo = rs; break;
        case MipsOp::mult: {
            const auto product = static_cast<std::int64_t>(signed_view(rs)) * static_cast<std::int64_t>(signed_view(rt));
            const auto bits = static_cast<std::uint64_t>(product);
            state.lo = static_cast<std::uint32_t>(bits);
            state.hi = static_cast<std::uint32_t>(bits >> 32);
            break;
        }
        case MipsOp::multu: {
            const auto product = static_cast<std::uint64_t>(rs) * static_cast<std::uint64_t>(rt);
            state.lo = static_cast<std::uint32_t>(product);
            state.hi = static_cast<std::uint32_t>(product >> 32);
            break;
        }
        case MipsOp::div:
            if (rt == 0u) { state.hi = rs; state.lo = signed_view(rs) < 0 ? 1u : 0xFFFFFFFFu; }
            else if (rs == 0x80000000u && rt == 0xFFFFFFFFu) { state.hi = 0u; state.lo = 0x80000000u; }
            else {
                const auto lhs = static_cast<std::int64_t>(signed_view(rs));
                const auto rhs = static_cast<std::int64_t>(signed_view(rt));
                state.lo = static_cast<std::uint32_t>(lhs / rhs);
                state.hi = static_cast<std::uint32_t>(lhs % rhs);
            }
            break;
        case MipsOp::divu:
            if (rt == 0u) { state.hi = rs; state.lo = 0xFFFFFFFFu; }
            else { state.lo = rs / rt; state.hi = rs % rt; }
            break;
        case MipsOp::j:
            schedule_control_transfer(true, ((instruction_pc + 4u) & 0xF0000000u) | (instruction.target << 2));
            break;
        case MipsOp::jal:
            queue_gpr_write(31u, instruction_pc + 8u);
            schedule_control_transfer(true, ((instruction_pc + 4u) & 0xF0000000u) | (instruction.target << 2));
            break;
        case MipsOp::jr: schedule_control_transfer(true, rs); break;
        case MipsOp::jalr:
            queue_gpr_write(instruction.rd, instruction_pc + 8u);
            schedule_control_transfer(true, rs);
            break;
        case MipsOp::beq: schedule_control_transfer(rs == rt, branch_target()); break;
        case MipsOp::bne: schedule_control_transfer(rs != rt, branch_target()); break;
        case MipsOp::blez: schedule_control_transfer(signed_view(rs) <= 0, branch_target()); break;
        case MipsOp::bgtz: schedule_control_transfer(signed_view(rs) > 0, branch_target()); break;
        case MipsOp::bltz: schedule_control_transfer(signed_view(rs) < 0, branch_target()); break;
        case MipsOp::bgez: schedule_control_transfer(signed_view(rs) >= 0, branch_target()); break;
        case MipsOp::bltzal:
            queue_gpr_write(31u, instruction_pc + 8u);
            schedule_control_transfer(signed_view(rs) < 0, branch_target());
            break;
        case MipsOp::bgezal:
            queue_gpr_write(31u, instruction_pc + 8u);
            schedule_control_transfer(signed_view(rs) >= 0, branch_target());
            break;
        case MipsOp::lb:
        case MipsOp::lbu: {
            const auto address = rs + sign_extend16(instruction.immediate);
            const auto in = bus.read8(address);
            if (in.status == R3000aBusStatus::unsupported) return data_boundary(address, 1u);
            if (in.status == R3000aBusStatus::bus_error) return data_bus_error(address, 1u);
            const auto value = instruction.op == MipsOp::lb
                ? sign_extend8(static_cast<std::uint8_t>(in.value))
                : (in.value & 0xffu);
            queue_load(instruction.rt, value);
            break;
        }
        case MipsOp::lh:
        case MipsOp::lhu: {
            const auto address = rs + sign_extend16(instruction.immediate);
            if ((address & 1u) != 0u) return address_exception(R3000aExceptionCode::adel, address, 2u);
            const auto in = bus.read16(address);
            if (in.status == R3000aBusStatus::unsupported) return data_boundary(address, 2u);
            if (in.status == R3000aBusStatus::bus_error) return data_bus_error(address, 2u);
            const auto value = instruction.op == MipsOp::lh
                ? sign_extend16(static_cast<std::uint16_t>(in.value))
                : (in.value & 0xffffu);
            queue_load(instruction.rt, value);
            break;
        }
        case MipsOp::lw: {
            const auto address = rs + sign_extend16(instruction.immediate);
            if ((address & 3u) != 0u) return address_exception(R3000aExceptionCode::adel, address, 4u);
            const auto in = bus.read32(address);
            if (in.status == R3000aBusStatus::unsupported) return data_boundary(address, 4u);
            if (in.status == R3000aBusStatus::bus_error) return data_bus_error(address, 4u);
            queue_load(instruction.rt, in.value);
            break;
        }
        case MipsOp::sb: {
            const auto address = rs + sign_extend16(instruction.immediate);
            const auto value = static_cast<std::uint8_t>(rt & 0xffu);
            const auto out = bus.write8(address, value);
            if (out.status == R3000aBusStatus::unsupported) return data_boundary(address, 1u, rt & 0xffu);
            if (out.status == R3000aBusStatus::bus_error) return data_bus_error(address, 1u, rt & 0xffu);
            break;
        }
        case MipsOp::sh: {
            const auto address = rs + sign_extend16(instruction.immediate);
            if ((address & 1u) != 0u) return address_exception(R3000aExceptionCode::ades, address, 2u);
            const auto value = static_cast<std::uint16_t>(rt & 0xffffu);
            const auto out = bus.write16(address, value);
            if (out.status == R3000aBusStatus::unsupported) return data_boundary(address, 2u, rt & 0xffffu);
            if (out.status == R3000aBusStatus::bus_error) return data_bus_error(address, 2u, rt & 0xffffu);
            break;
        }
        case MipsOp::sw: {
            const auto address = rs + sign_extend16(instruction.immediate);
            if ((address & 3u) != 0u) return address_exception(R3000aExceptionCode::ades, address, 4u);
            const auto out = bus.write32(address, rt);
            if (out.status == R3000aBusStatus::unsupported) return data_boundary(address, 4u, rt);
            if (out.status == R3000aBusStatus::bus_error) return data_bus_error(address, 4u, rt);
            break;
        }
        case MipsOp::lwl:
        case MipsOp::lwr: {
            const auto effective = rs + sign_extend16(instruction.immediate);
            const auto aligned = effective & ~3u;
            const auto in = bus.read32(aligned);
            if (in.status == R3000aBusStatus::unsupported) return data_boundary(effective, 4u);
            if (in.status == R3000aBusStatus::bus_error) return data_bus_error(effective, 4u);
            const std::uint32_t base = prior_pending.valid && prior_pending.reg == instruction.rt
                ? prior_pending.value : rt;
            const auto lane = effective & 3u;
            std::uint32_t value = base;
            if (instruction.op == MipsOp::lwl) {
                switch (lane) {
                    case 0u: value = (base & 0x00FFFFFFu) | (in.value << 24); break;
                    case 1u: value = (base & 0x0000FFFFu) | (in.value << 16); break;
                    case 2u: value = (base & 0x000000FFu) | (in.value << 8); break;
                    case 3u: value = in.value; break;
                }
            } else {
                switch (lane) {
                    case 0u: value = in.value; break;
                    case 1u: value = (base & 0xFF000000u) | (in.value >> 8); break;
                    case 2u: value = (base & 0xFFFF0000u) | (in.value >> 16); break;
                    case 3u: value = (base & 0xFFFFFF00u) | (in.value >> 24); break;
                }
            }
            queue_load(instruction.rt, value);
            break;
        }
        case MipsOp::swl:
        case MipsOp::swr: {
            const auto effective = rs + sign_extend16(instruction.immediate);
            const auto aligned = effective & ~3u;
            const auto in = bus.read32(aligned);
            if (in.status == R3000aBusStatus::unsupported) return data_boundary(effective, 4u, rt);
            if (in.status == R3000aBusStatus::bus_error) return data_bus_error(effective, 4u, rt);
            const auto lane = effective & 3u;
            std::uint32_t merged = in.value;
            if (instruction.op == MipsOp::swl) {
                switch (lane) {
                    case 0u: merged = (in.value & 0xFFFFFF00u) | (rt >> 24); break;
                    case 1u: merged = (in.value & 0xFFFF0000u) | (rt >> 16); break;
                    case 2u: merged = (in.value & 0xFF000000u) | (rt >> 8); break;
                    case 3u: merged = rt; break;
                }
            } else {
                switch (lane) {
                    case 0u: merged = rt; break;
                    case 1u: merged = (in.value & 0x000000FFu) | (rt << 8); break;
                    case 2u: merged = (in.value & 0x0000FFFFu) | (rt << 16); break;
                    case 3u: merged = (in.value & 0x00FFFFFFu) | (rt << 24); break;
                }
            }
            const auto out = bus.write32(aligned, merged);
            if (out.status == R3000aBusStatus::unsupported) return data_boundary(effective, 4u, merged);
            if (out.status == R3000aBusStatus::bus_error) return data_bus_error(effective, 4u, merged);
            break;
        }
        case MipsOp::mfc0: {
            std::optional<std::uint32_t> value;
            switch (instruction.rd) {
                case 6u: value = state.cop0.target_address; break;
                case 8u: value = state.cop0.bad_vaddr; break;
                case 12u: value = state.cop0.status; break;
                case 13u: value = state.cop0.cause; break;
                case 14u: value = state.cop0.epc; break;
                default: break;
            }
            if (!value) {
                if (cop0_register_is_ri(instruction.rd)) return cop0_ri();
                return cop0_boundary();
            }
            queue_load(instruction.rt, *value);
            break;
        }
        case MipsOp::mtc0:
            switch (instruction.rd) {
                case 12u:
                    state.cop0.status = (state.cop0.status & ~kStatusWritableMask) |
                                        (rt & kStatusWritableMask);
                    break;
                case 13u:
                    state.cop0.cause = (state.cop0.cause & ~kCauseSwMask) | (rt & kCauseSwMask);
                    break;
                case 6u:
                case 8u:
                case 14u:
                    return cop0_boundary();
                default:
                    if (cop0_register_is_ri(instruction.rd)) return cop0_ri();
                    return cop0_boundary();
            }
            break;
        case MipsOp::rfe: {
            const auto low = state.cop0.status & kStatusModeStackMask;
            const auto restored = (low & 0x30u) | ((low >> 2) & 0x0Fu);
            state.cop0.status = (state.cop0.status & ~kStatusModeStackMask) | restored;
            break;
        }
        case MipsOp::mfc2:
        case MipsOp::cfc2:
        case MipsOp::mtc2:
        case MipsOp::ctc2:
        case MipsOp::cop2_command:
            if ((state.cop0.status & kStatusCu2) == 0u) {
                return enter_exception(state, R3000aExceptionCode::coprocessor_unusable,
                                       R3000aStage::cop2, instruction_pc, current_delay,
                                       instruction.raw, std::nullopt, 2u);
            } else {
                auto result = boundary(state, R3000aBoundaryCode::cop2_unimplemented,
                                       R3000aStage::cop2, instruction_pc, instruction.raw);
                result.diagnostic.coprocessor = 2u;
                return result;
            }
        case MipsOp::syscall:
            return enter_exception(state, R3000aExceptionCode::syscall, R3000aStage::execute,
                                   instruction_pc, current_delay, instruction.raw);
        case MipsOp::break_:
            return enter_exception(state, R3000aExceptionCode::breakpoint, R3000aStage::execute,
                                   instruction_pc, current_delay, instruction.raw);
        case MipsOp::reserved:
            return enter_exception(state, R3000aExceptionCode::reserved_instruction, R3000aStage::decode,
                                   instruction_pc, current_delay, instruction.raw);
        default:
            supported = false;
            break;
    }

    if (!supported)
        return boundary(state, R3000aBoundaryCode::architectural_operation_unimplemented,
                        R3000aStage::execute, instruction_pc, instruction.raw);

    if (direct_write_valid) write_gpr(state, direct_write_reg, direct_write_value);
    state.pending_load = new_pending;

    if (!scheduled_control_transfer) {
        advance_ordinary_pc(state);
        if (current_delay.active) state.delay_slot = {};
    }
    state.gpr[0] = 0u;
    return {};
}

R3000aState initialize_r3000a_for_psx_exe(const Ps1ExeMetadata& metadata) noexcept {
    R3000aState state{};
    state.pc = metadata.entry_pc;
    state.next_pc = metadata.entry_pc + 4u;
    state.gpr[28] = metadata.initial_gp;
    if (metadata.stack_base != 0u || metadata.stack_size != 0u) {
        state.gpr[29] = metadata.stack_base + metadata.stack_size;
    }
    state.gpr[0] = 0u;
    return state;
}

} // namespace jojo
