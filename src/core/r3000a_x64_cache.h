#pragma once

#include "core/r3000a_x64.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace jojo {

inline constexpr std::uint32_t kR3000aX64BackendAbiVersion = 1u;

[[nodiscard]] std::uint64_t r3000a_x64_block_fingerprint(
    const R3000aIrBlock& block) noexcept;

struct R3000aX64CacheStats {
    std::size_t entries{};
    std::uint64_t compilations{};
    std::uint64_t reuses{};
    std::uint64_t invalidations{};
};

class R3000aX64BlockCache {
public:
    [[nodiscard]] Result<const R3000aX64Code*> get_or_compile(
        const R3000aIrBlock& block);
    [[nodiscard]] Result<const R3000aX64Code*> get_or_compile_instruction(
        std::uint32_t pc,
        std::uint32_t raw_opcode);

    void invalidate(std::uint32_t entry_pc) noexcept;
    void clear() noexcept;

    [[nodiscard]] R3000aX64CacheStats stats() const noexcept;

private:
    struct Entry {
        std::uint64_t fingerprint{};
        std::uint32_t abi_version{};
        R3000aX64Code code{};
    };

    std::map<std::uint32_t, Entry> entries_;
    std::uint64_t compilations_{};
    std::uint64_t reuses_{};
    std::uint64_t invalidations_{};
};

} // namespace jojo
