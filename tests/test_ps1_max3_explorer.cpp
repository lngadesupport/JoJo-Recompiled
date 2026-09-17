#include "core/ps1_max3_explorer.h"
#include "core/ps1_exe.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

static jojo::Ps1Executable make_executable(const std::vector<std::uint32_t>& words) {
    auto parsed = jojo::parse_ps1_executable(test_ps1::make_psx_exe_from_words(words));
    CHECK(parsed);
    return parsed ? std::move(parsed.value) : jojo::Ps1Executable{};
}

static jojo::Ps1Max3Options fast_options() {
    jojo::Ps1Max3Options options{};
    options.max_nodes = 64u;
    options.max_branch_depth = 3u;
    options.max_total_retired = 10000u;
    options.segment_options.instruction_budget = std::numeric_limits<std::uint64_t>::max();
    options.segment_options.trace_capacity = 16u;
    options.segment_options.diagnostic_mmio_probe = true;
    options.segment_options.mmio_event_capacity = 32u;
    options.segment_options.bios_event_capacity = 32u;
    options.segment_options.stagnation_instruction_limit = 16u;
    return options;
}

static std::vector<std::uint32_t> one_frontier_then_loop() {
    return {
        test_mips::i(0x09u, 0u, 2u, 5u),
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::r(2u, 0u, 16u, 0u, 0x21u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    };
}

static void test_one_frontier_branches_four_ways() {
    const auto executable = make_executable(one_frontier_then_loop());
    const auto explored = jojo::explore_ps1_max3(executable, fast_options());
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 5u);
    CHECK(report.nodes[0].depth == 0u);
    CHECK(report.nodes[0].stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
    CHECK(report.nodes[0].frontier_table == 0x000000A0u);
    CHECK(report.nodes[0].frontier_selector == 0x00000033u);

    const std::vector<jojo::Ps1BiosFallback> expected{
        jojo::Ps1BiosFallback::return_zero,
        jojo::Ps1BiosFallback::return_one,
        jojo::Ps1BiosFallback::return_minus_one,
        jojo::Ps1BiosFallback::preserve_v0,
    };
    std::vector<jojo::Ps1BiosFallback> seen;
    for (std::size_t i = 1; i < report.nodes.size(); ++i) {
        CHECK(report.nodes[i].depth == 1u);
        CHECK(report.nodes[i].fallback.has_value());
        if (report.nodes[i].fallback) seen.push_back(*report.nodes[i].fallback);
    }
    CHECK(seen == expected);
    CHECK(report.dependencies.size() == 1u);
    CHECK(report.dependencies[0].kind == jojo::Ps1Max3DependencyKind::bios_frontier);
    CHECK(report.dependencies[0].table == 0x000000A0u);
    CHECK(report.dependencies[0].selector == 0x00000033u);
}

static std::vector<std::uint32_t> two_frontiers_with_convergence() {
    return {
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 2u, 7u),
        test_mips::i(0x09u, 0u, 9u, 0x0034u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::j(0x02u, 0x80010020u >> 2),
        0x00000000u,
    };
}

static void test_converged_frontier_state_is_expanded_once() {
    const auto executable = make_executable(two_frontiers_with_convergence());
    auto options = fast_options();
    options.max_branch_depth = 2u;
    const auto explored = jojo::explore_ps1_max3(executable, options);
    CHECK(explored);
    if (!explored) return;

    const auto& report = explored.value;
    CHECK(report.nodes.size() == 9u);
    CHECK(std::count_if(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
        return node.depth == 1u && node.deduplicated;
    }) == 3);
    CHECK(std::count_if(report.nodes.begin(), report.nodes.end(), [](const auto& node) {
        return node.depth == 2u;
    }) == 4);
    CHECK(report.dependencies.size() == 2u);
    CHECK(report.dependencies[1].selector == 0x00000034u);
}

static void test_bounds_and_progress_ranking_are_deterministic() {
    const auto executable = make_executable(one_frontier_then_loop());

    auto node_limited = fast_options();
    node_limited.max_nodes = 3u;
    const auto three = jojo::explore_ps1_max3(executable, node_limited);
    CHECK(three);
    if (three) CHECK(three.value.nodes.size() == 3u);

    auto depth_limited = fast_options();
    depth_limited.max_branch_depth = 0u;
    const auto root_only = jojo::explore_ps1_max3(executable, depth_limited);
    CHECK(root_only);
    if (root_only) CHECK(root_only.value.nodes.size() == 1u);

    auto retired_limited = fast_options();
    retired_limited.max_total_retired = 3u;
    const auto capped = jojo::explore_ps1_max3(executable, retired_limited);
    CHECK(capped);
    if (capped) {
        CHECK(capped.value.total_retired == 3u);
        CHECK(capped.value.nodes.size() == 1u);
    }

    const auto ranked_executable = make_executable({
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x05u, 2u, 0u, 3u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 9u, 0x0034u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::j(0x02u, 0x80010024u >> 2),
        0x00000000u,
    });
    auto ranked_options = fast_options();
    ranked_options.max_branch_depth = 2u;
    const auto ranked = jojo::explore_ps1_max3(ranked_executable, ranked_options);
    CHECK(ranked);
    if (ranked) {
        CHECK(!ranked.value.best_path.empty());
        CHECK(ranked.value.best_path.front().fallback == jojo::Ps1BiosFallback::return_zero ||
              ranked.value.best_path.front().fallback == jojo::Ps1BiosFallback::return_minus_one ||
              ranked.value.best_path.front().fallback == jojo::Ps1BiosFallback::preserve_v0);
        CHECK(ranked.value.nodes[ranked.value.best_node].path_dependency_count >= 2u);
    }
}

int main() {
    test_one_frontier_branches_four_ways();
    test_converged_frontier_state_is_expanded_once();
    test_bounds_and_progress_ranking_are_deterministic();
    return failures ? 1 : 0;
}
