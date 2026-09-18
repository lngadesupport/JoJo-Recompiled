#include "core/r3000a_cfg.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <utility>

namespace jojo {
namespace {

bool contains_pc(
    std::uint32_t base_pc,
    std::span<const std::uint32_t> words,
    std::uint32_t pc) noexcept {
    if ((pc & 3u) != 0u || pc < base_pc) return false;
    const auto delta = static_cast<std::uint64_t>(pc - base_pc);
    return (delta / 4u) < words.size();
}

std::size_t word_index(
    std::uint32_t base_pc,
    std::uint32_t pc) noexcept {
    return static_cast<std::size_t>((pc - base_pc) / 4u);
}

void enqueue_if_in_program(
    std::uint32_t base_pc,
    std::span<const std::uint32_t> words,
    const std::optional<std::uint32_t>& target,
    std::set<std::uint32_t>& discovered,
    std::deque<std::uint32_t>& pending) {
    if (!target || !contains_pc(base_pc, words, *target)) return;
    if (discovered.insert(*target).second) pending.push_back(*target);
}

} // namespace

Result<R3000aCfg> build_r3000a_cfg(
    std::uint32_t program_base_pc,
    std::span<const std::uint32_t> words,
    std::uint32_t entry_pc,
    std::size_t max_blocks,
    std::size_t max_instructions_per_block) noexcept {
    if ((program_base_pc & 3u) != 0u || words.empty()) {
        return Result<R3000aCfg>::failure(
            ErrorCode::invalid_argument,
            "R3000A CFG requires aligned non-empty program words");
    }
    if (!contains_pc(program_base_pc, words, entry_pc)) {
        return Result<R3000aCfg>::failure(
            ErrorCode::invalid_argument,
            "R3000A CFG entry PC is outside the supplied program");
    }
    if (max_blocks == 0u || max_instructions_per_block == 0u) {
        return Result<R3000aCfg>::failure(
            ErrorCode::invalid_argument,
            "R3000A CFG limits must be non-zero");
    }

    R3000aCfg cfg{};
    cfg.program_base_pc = program_base_pc;
    cfg.entry_pc = entry_pc;

    std::set<std::uint32_t> discovered{entry_pc};
    std::set<std::uint32_t> built;
    std::deque<std::uint32_t> pending{entry_pc};

    while (!pending.empty()) {
        const auto block_pc = pending.front();
        pending.pop_front();
        if (built.contains(block_pc)) continue;

        if (cfg.blocks.size() >= max_blocks) {
            return Result<R3000aCfg>::failure(
                ErrorCode::backend_unavailable,
                "R3000A CFG exceeded the configured block budget");
        }

        const auto start = word_index(program_base_pc, block_pc);
        const auto remaining = words.size() - start;
        const auto span_count = std::min(remaining, max_instructions_per_block);
        auto lifted = lift_r3000a_basic_block(
            block_pc,
            words.subspan(start, span_count),
            max_instructions_per_block);
        if (!lifted) {
            return Result<R3000aCfg>::failure(lifted.error, lifted.detail);
        }

        // A successor discovered by an earlier block is a block boundary.
        // Never cut away a control-transfer delay slot: a delay-slot address may
        // also be a legal entry point from another path.
        auto boundary = discovered.upper_bound(block_pc);
        if (boundary != discovered.end()) {
            const auto boundary_pc = *boundary;
            const auto it = std::find_if(
                lifted.value.instructions.begin(),
                lifted.value.instructions.end(),
                [&](const R3000aIrInstruction& instruction) {
                    return instruction.pc == boundary_pc;
                });
            if (it != lifted.value.instructions.end() && !it->delay_slot) {
                const auto limited_count =
                    static_cast<std::size_t>((boundary_pc - block_pc) / 4u);
                if (limited_count != 0u) {
                    lifted = lift_r3000a_basic_block(
                        block_pc,
                        words.subspan(start, limited_count),
                        limited_count);
                    if (!lifted) {
                        return Result<R3000aCfg>::failure(
                            lifted.error,
                            lifted.detail);
                    }
                }
            }
        }

        const auto block = lifted.value;
        built.insert(block_pc);
        cfg.blocks.push_back(block);

        switch (block.terminator) {
            case R3000aIrTerminatorKind::conditional_branch:
                enqueue_if_in_program(
                    program_base_pc,
                    words,
                    block.taken_target,
                    discovered,
                    pending);
                enqueue_if_in_program(
                    program_base_pc,
                    words,
                    block.fallthrough_target,
                    discovered,
                    pending);
                break;
            case R3000aIrTerminatorKind::direct_jump:
                enqueue_if_in_program(
                    program_base_pc,
                    words,
                    block.taken_target,
                    discovered,
                    pending);
                break;
            case R3000aIrTerminatorKind::fallthrough:
                enqueue_if_in_program(
                    program_base_pc,
                    words,
                    block.fallthrough_target,
                    discovered,
                    pending);
                break;
            case R3000aIrTerminatorKind::indirect_jump:
                break;
        }
    }

    std::sort(
        cfg.blocks.begin(),
        cfg.blocks.end(),
        [](const R3000aIrBlock& lhs, const R3000aIrBlock& rhs) {
            return lhs.entry_pc < rhs.entry_pc;
        });

    return Result<R3000aCfg>::success(std::move(cfg));
}

} // namespace jojo
