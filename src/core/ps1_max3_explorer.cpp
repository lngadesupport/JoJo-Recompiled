#include "core/ps1_max3_explorer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace jojo {
namespace {

constexpr std::array<Ps1BiosFallback, 4> kFallbackOrder{
    Ps1BiosFallback::return_zero,
    Ps1BiosFallback::return_one,
    Ps1BiosFallback::return_minus_one,
    Ps1BiosFallback::preserve_v0,
};

struct DependencyKey {
    Ps1Max3DependencyKind kind{};
    std::uint32_t first{};
    std::uint32_t second{};
    std::uint8_t width{};
    bool write{};

    friend bool operator<(const DependencyKey& lhs, const DependencyKey& rhs) noexcept {
        return std::tie(lhs.kind, lhs.first, lhs.second, lhs.width, lhs.write) <
               std::tie(rhs.kind, rhs.first, rhs.second, rhs.width, rhs.write);
    }
};

struct FrontierKey {
    std::uint32_t table{};
    std::uint32_t selector{};
    std::uint64_t state_hash{};

    friend bool operator<(const FrontierKey& lhs, const FrontierKey& rhs) noexcept {
        return std::tie(lhs.table, lhs.selector, lhs.state_hash) <
               std::tie(rhs.table, rhs.selector, rhs.state_hash);
    }
};

struct PathMetrics {
    std::uint64_t presented_frames{};
    std::uint64_t vram_write_count{};
    std::uint64_t gpu_gp0_command_count{};
    std::uint64_t gpu_gp1_command_count{};
    std::uint64_t cdrom_command_count{};
    std::uint64_t dma_transfer_count{};
};

bool is_bios_table(std::uint32_t physical) noexcept {
    return physical == 0x000000A0u ||
           physical == 0x000000B0u ||
           physical == 0x000000C0u;
}

DependencyKey dependency_key(const Ps1Max3Dependency& dependency) noexcept {
    if (dependency.kind == Ps1Max3DependencyKind::bios_frontier) {
        return DependencyKey{dependency.kind, dependency.table, dependency.selector, 0u, false};
    }
    return DependencyKey{
        dependency.kind,
        dependency.address,
        0u,
        dependency.width,
        dependency.write,
    };
}

void add_dependency(Ps1Max3Report& report,
                    std::set<DependencyKey>& global_dependencies,
                    std::set<DependencyKey>& path_dependencies,
                    const Ps1Max3Dependency& dependency) {
    const auto key = dependency_key(dependency);
    path_dependencies.insert(key);
    if (global_dependencies.insert(key).second) {
        report.dependencies.push_back(dependency);
    }
}

PathMetrics accumulate_metrics(const PathMetrics& prior,
                               const Ps1BootReport& segment) noexcept {
    PathMetrics result = prior;
    result.presented_frames += segment.presented_frames;
    result.vram_write_count += segment.vram_write_count;
    result.gpu_gp0_command_count += segment.gpu_gp0_command_count;
    result.gpu_gp1_command_count += segment.gpu_gp1_command_count;
    result.cdrom_command_count += static_cast<std::uint64_t>(segment.recent_cdrom_commands.size());
    result.dma_transfer_count += segment.dma_transfer_count;
    return result;
}

bool outranks(const Ps1Max3NodeSummary& candidate,
              const Ps1Max3NodeSummary& current) noexcept {
    if (candidate.path_presented_frames != current.path_presented_frames)
        return candidate.path_presented_frames > current.path_presented_frames;
    if (candidate.path_vram_write_count != current.path_vram_write_count)
        return candidate.path_vram_write_count > current.path_vram_write_count;
    if (candidate.path_gpu_gp0_command_count != current.path_gpu_gp0_command_count)
        return candidate.path_gpu_gp0_command_count > current.path_gpu_gp0_command_count;
    if (candidate.path_gpu_gp1_command_count != current.path_gpu_gp1_command_count)
        return candidate.path_gpu_gp1_command_count > current.path_gpu_gp1_command_count;
    if (candidate.path_cdrom_command_count != current.path_cdrom_command_count)
        return candidate.path_cdrom_command_count > current.path_cdrom_command_count;
    if (candidate.path_dma_transfer_count != current.path_dma_transfer_count)
        return candidate.path_dma_transfer_count > current.path_dma_transfer_count;
    if (candidate.path_dependency_count != current.path_dependency_count)
        return candidate.path_dependency_count > current.path_dependency_count;
    return candidate.cumulative_retired > current.cumulative_retired;
}

class Explorer {
public:
    explicit Explorer(const Ps1Max3Options& options) : options_(options) {
        report_.options = options;
    }

    Result<Ps1Max3Report> run(const Ps1Executable& executable) {
        if (options_.max_nodes == 0u) {
            return Result<Ps1Max3Report>::failure(
                ErrorCode::invalid_argument,
                "PS1 MAX3 max_nodes must be greater than zero");
        }

        auto root = Ps1BootRuntime::create(executable);
        if (!root) {
            return Result<Ps1Max3Report>::failure(root.error, root.detail);
        }

        visit(std::move(root.value), 0u, std::nullopt, std::nullopt,
              0u, PathMetrics{}, {}, {});
        return Result<Ps1Max3Report>::success(std::move(report_));
    }

private:
    void visit(Ps1BootRuntime runtime,
               std::size_t depth,
               std::optional<std::size_t> parent,
               std::optional<Ps1BiosFallback> fallback,
               std::uint64_t cumulative_before,
               const PathMetrics& metrics_before,
               std::vector<Ps1Max3Decision> path,
               std::set<DependencyKey> path_dependencies) {
        if (report_.termination_reason != Ps1Max3TerminationReason::completed) return;
        if (report_.nodes.size() >= options_.max_nodes) {
            report_.termination_reason = Ps1Max3TerminationReason::node_limit;
            return;
        }
        if (!report_.nodes.empty() && report_.total_retired >= options_.max_total_retired) {
            report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
            return;
        }

        const std::uint64_t remaining =
            options_.max_total_retired >= report_.total_retired
                ? options_.max_total_retired - report_.total_retired
                : 0u;
        auto segment_options = options_.segment_options;
        segment_options.instruction_budget =
            std::min(segment_options.instruction_budget, remaining);

        Ps1BootReport segment = runtime.run(segment_options);
        report_.total_retired += segment.instructions_retired;
        const auto metrics = accumulate_metrics(metrics_before, segment);

        for (const auto& mmio : segment.recent_mmio) {
            if (!mmio.speculative) continue;
            add_dependency(report_, global_dependencies_, path_dependencies,
                           Ps1Max3Dependency{
                               Ps1Max3DependencyKind::speculative_mmio,
                               0u,
                               0u,
                               mmio.address,
                               mmio.width,
                               mmio.write,
                           });
        }

        std::uint32_t frontier_table = 0u;
        std::uint32_t frontier_selector = 0u;
        bool has_frontier = false;
        if (segment.stop_reason == Ps1BootStopReason::bios_call_unimplemented) {
            const auto physical = Ps1MemoryBus::guest_to_physical(runtime.cpu_state().pc);
            if (physical && is_bios_table(*physical)) {
                frontier_table = *physical;
                frontier_selector = runtime.cpu_state().gpr[9];
                has_frontier = true;
                add_dependency(report_, global_dependencies_, path_dependencies,
                               Ps1Max3Dependency{
                                   Ps1Max3DependencyKind::bios_frontier,
                                   frontier_table,
                                   frontier_selector,
                                   0u,
                                   0u,
                                   false,
                               });
            }
        }

        Ps1Max3NodeSummary summary{};
        summary.index = report_.nodes.size();
        summary.parent = parent;
        summary.depth = depth;
        summary.fallback = fallback;
        summary.stop_reason = segment.stop_reason;
        summary.segment_retired = segment.instructions_retired;
        summary.cumulative_retired = cumulative_before + segment.instructions_retired;
        summary.state_hash = runtime.diagnostic_state_hash();
        summary.frontier_table = frontier_table;
        summary.frontier_selector = frontier_selector;
        summary.path_presented_frames = metrics.presented_frames;
        summary.path_vram_write_count = metrics.vram_write_count;
        summary.path_gpu_gp0_command_count = metrics.gpu_gp0_command_count;
        summary.path_gpu_gp1_command_count = metrics.gpu_gp1_command_count;
        summary.path_cdrom_command_count = metrics.cdrom_command_count;
        summary.path_dma_transfer_count = metrics.dma_transfer_count;
        summary.path_dependency_count = path_dependencies.size();

        const auto node_index = summary.index;
        report_.nodes.push_back(summary);

        bool can_expand = has_frontier && depth < options_.max_branch_depth;
        if (can_expand) {
            const FrontierKey key{frontier_table, frontier_selector, summary.state_hash};
            if (!expanded_frontiers_.insert(key).second) {
                report_.nodes[node_index].deduplicated = true;
                can_expand = false;
            }
        }

        if (report_.nodes.size() == 1u ||
            outranks(report_.nodes[node_index], report_.nodes[report_.best_node])) {
            report_.best_node = node_index;
            report_.best_path = path;
            report_.best_report = segment;
        }

        if (report_.total_retired >= options_.max_total_retired) {
            report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
            return;
        }
        if (!can_expand) return;

        for (const auto policy : kFallbackOrder) {
            if (report_.nodes.size() >= options_.max_nodes) {
                report_.termination_reason = Ps1Max3TerminationReason::node_limit;
                return;
            }
            if (report_.total_retired >= options_.max_total_retired) {
                report_.termination_reason = Ps1Max3TerminationReason::total_retired_limit;
                return;
            }

            auto child = runtime;
            if (!child.apply_diagnostic_bios_fallback(policy)) continue;
            auto child_path = path;
            child_path.push_back(Ps1Max3Decision{frontier_table, frontier_selector, policy});
            visit(std::move(child), depth + 1u, node_index, policy,
                  summary.cumulative_retired, metrics,
                  std::move(child_path), path_dependencies);
            if (report_.termination_reason != Ps1Max3TerminationReason::completed) return;
        }
    }

    const Ps1Max3Options& options_;
    Ps1Max3Report report_{};
    std::set<DependencyKey> global_dependencies_;
    std::set<FrontierKey> expanded_frontiers_;
};

} // namespace

Ps1Max3Options ps1_max3_local_evidence_options() noexcept {
    Ps1Max3Options options{};
    options.max_nodes = 5461u;
    options.max_branch_depth = 6u;
    options.max_total_retired = 1000000000ull;
    options.segment_options = Ps1BootOptions{
        std::numeric_limits<std::uint64_t>::max(),
        131072u,
        true,
        65536u,
        65536u,
        2000000u,
    };
    return options;
}

Result<Ps1Max3Report> explore_ps1_max3(
    const Ps1Executable& executable,
    const Ps1Max3Options& options) {
    Explorer explorer(options);
    return explorer.run(executable);
}

} // namespace jojo
