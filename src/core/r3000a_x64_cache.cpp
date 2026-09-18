#include "core/r3000a_x64_cache.h"

#include <cstdint>
#include <utility>

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint64_t instruction_fingerprint(
    std::uint32_t pc,
    std::uint32_t raw_opcode) noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_u32(hash, kR3000aX64BackendAbiVersion);
    hash_u32(hash, pc);
    hash_u32(hash, 1u);
    hash_u32(hash, pc);
    hash_u32(hash, raw_opcode);
    hash_byte(hash, 0u);
    hash_byte(
        hash,
        static_cast<std::uint8_t>(R3000aIrTerminatorKind::fallthrough));
    return hash;
}

} // namespace

std::uint64_t r3000a_x64_block_fingerprint(
    const R3000aIrBlock& block) noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_u32(hash, kR3000aX64BackendAbiVersion);
    hash_u32(hash, block.entry_pc);
    hash_u32(hash, static_cast<std::uint32_t>(block.instructions.size()));
    for (const auto& instruction : block.instructions) {
        hash_u32(hash, instruction.pc);
        hash_u32(hash, instruction.decoded.raw);
        hash_byte(hash, instruction.delay_slot ? 1u : 0u);
    }
    hash_byte(hash, static_cast<std::uint8_t>(block.terminator));
    return hash;
}

Result<const R3000aX64Code*> R3000aX64BlockCache::get_or_compile(
    const R3000aIrBlock& block) {
    const auto fingerprint = r3000a_x64_block_fingerprint(block);
    const auto found = entries_.find(block.entry_pc);
    if (found != entries_.end() &&
        found->second.fingerprint == fingerprint &&
        found->second.abi_version == kR3000aX64BackendAbiVersion) {
        ++reuses_;
        return Result<const R3000aX64Code*>::success(&found->second.code);
    }

    auto emitted = emit_r3000a_x64_alu_block(block);
    if (!emitted) {
        return Result<const R3000aX64Code*>::failure(
            emitted.error,
            emitted.detail);
    }

    if (found != entries_.end()) {
        entries_.erase(found);
        ++invalidations_;
    }

    Entry entry{};
    entry.fingerprint = fingerprint;
    entry.abi_version = kR3000aX64BackendAbiVersion;
    entry.code = std::move(emitted.value);
    auto [it, inserted] =
        entries_.emplace(block.entry_pc, std::move(entry));
    (void)inserted;
    ++compilations_;
    return Result<const R3000aX64Code*>::success(&it->second.code);
}

Result<const R3000aX64Code*>
R3000aX64BlockCache::get_or_compile_instruction(
    std::uint32_t pc,
    std::uint32_t raw_opcode) {
    const auto fingerprint = instruction_fingerprint(pc, raw_opcode);
    const auto found = entries_.find(pc);
    if (found != entries_.end() &&
        found->second.fingerprint == fingerprint &&
        found->second.abi_version == kR3000aX64BackendAbiVersion) {
        ++reuses_;
        return Result<const R3000aX64Code*>::success(&found->second.code);
    }

    const auto decoded = decode_mips(raw_opcode);
    if (!r3000a_op_is_x64_lowerable(decoded.op)) {
        return Result<const R3000aX64Code*>::failure(
            ErrorCode::backend_unavailable,
            "R3000A instruction is not lowered by the x64 backend");
    }

    R3000aIrBlock block{};
    block.entry_pc = pc;
    block.instructions.push_back(R3000aIrInstruction{
        pc,
        decoded,
        false,
    });
    block.terminator = R3000aIrTerminatorKind::fallthrough;
    block.fallthrough_target = pc + 4u;

    auto emitted = emit_r3000a_x64_alu_block(block);
    if (!emitted) {
        return Result<const R3000aX64Code*>::failure(
            emitted.error,
            emitted.detail);
    }

    if (found != entries_.end()) {
        entries_.erase(found);
        ++invalidations_;
    }

    Entry entry{};
    entry.fingerprint = fingerprint;
    entry.abi_version = kR3000aX64BackendAbiVersion;
    entry.code = std::move(emitted.value);
    auto [it, inserted] =
        entries_.emplace(pc, std::move(entry));
    (void)inserted;
    ++compilations_;
    return Result<const R3000aX64Code*>::success(&it->second.code);
}

void R3000aX64BlockCache::invalidate(std::uint32_t entry_pc) noexcept {
    const auto erased = entries_.erase(entry_pc);
    if (erased != 0u) ++invalidations_;
}

void R3000aX64BlockCache::clear() noexcept {
    if (!entries_.empty()) {
        invalidations_ += entries_.size();
        entries_.clear();
    }
}

R3000aX64CacheStats R3000aX64BlockCache::stats() const noexcept {
    return {
        entries_.size(),
        compilations_,
        reuses_,
        invalidations_,
    };
}

} // namespace jojo
