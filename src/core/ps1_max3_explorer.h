#pragma once

#include "core/ps1_boot_runtime.h"
#include "core/ps1_exe.h"
#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace jojo {

enum class Ps1Max3DependencyKind : std::uint8_t {
    bios_frontier,
    speculative_mmio,
};

enum class Ps1Max3TerminationReason : std::uint8_t {
    completed,
    node_limit,
    total_retired_limit,
};

struct Ps1Max3Options {
    std::size_t max_nodes{5461u};
    std::size_t max_branch_depth{6u};
    std::uint64_t max_total_retired{1000000000ull};
    Ps1BootOptions segment_options{};
};

struct Ps1Max3Decision {
    std::uint32_t table{};
    std::uint32_t selector{};
    Ps1BiosFallback fallback{Ps1BiosFallback::return_zero};
};

struct Ps1Max3NodeSummary {
    std::size_t index{};
    std::optional<std::size_t> parent{};
    std::size_t depth{};
    std::optional<Ps1BiosFallback> fallback{};
    Ps1BootStopReason stop_reason{Ps1BootStopReason::none};
    std::uint64_t segment_retired{};
    std::uint64_t cumulative_retired{};
    std::uint64_t state_hash{};
    bool deduplicated{};
    std::uint32_t frontier_table{};
    std::uint32_t frontier_selector{};
    std::uint64_t path_presented_frames{};
    std::uint64_t path_vram_write_count{};
    std::uint64_t path_gpu_gp0_command_count{};
    std::uint64_t path_gpu_gp1_command_count{};
    std::uint64_t path_cdrom_command_count{};
    std::uint64_t path_dma_transfer_count{};
    std::size_t path_dependency_count{};
};

struct Ps1Max3Dependency {
    Ps1Max3DependencyKind kind{Ps1Max3DependencyKind::bios_frontier};
    std::uint32_t table{};
    std::uint32_t selector{};
    std::uint32_t address{};
    std::uint8_t width{};
    bool write{};
};

struct Ps1Max3Report {
    Ps1Max3Options options{};
    Ps1Max3TerminationReason termination_reason{Ps1Max3TerminationReason::completed};
    std::uint64_t total_retired{};
    std::vector<Ps1Max3NodeSummary> nodes;
    std::vector<Ps1Max3Dependency> dependencies;
    std::size_t best_node{};
    std::vector<Ps1Max3Decision> best_path;
    Ps1BootReport best_report{};
};

[[nodiscard]] Ps1Max3Options ps1_max3_local_evidence_options() noexcept;
[[nodiscard]] Result<Ps1Max3Report> explore_ps1_max3(
    const Ps1Executable& executable,
    const Ps1Max3Options& options);

} // namespace jojo
