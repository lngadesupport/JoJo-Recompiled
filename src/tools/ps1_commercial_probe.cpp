#include "core/ps1_commercial_evidence.h"
#include "core/ps1_commercial_evidence_io.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace {

std::uint64_t parse_u64(const char* text, std::uint64_t fallback) {
    try {
        const auto value = std::stoull(text);
        return value == 0u ? fallback : value;
    } catch (...) {
        return fallback;
    }
}

std::uint32_t parse_u32(const char* text, std::uint32_t fallback) {
    const auto value = parse_u64(text, fallback);
    if (value > std::numeric_limits<std::uint32_t>::max()) return fallback;
    return static_cast<std::uint32_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr
            << "usage: jojo_ps1_commercial_probe <source.bin|source.cue|source.iso> "
               "<report.txt> [segments=128] [instructions_per_segment=500000] "
               "[native_x64=0|1]\n";
        return 2;
    }

    const std::filesystem::path source = argv[1];
    const std::filesystem::path report_path = argv[2];
    const auto max_segments = argc >= 4 ? parse_u32(argv[3], 128u) : 128u;
    const auto segment_budget =
        argc >= 5 ? parse_u64(argv[4], 500000u) : 500000u;
    const bool native_x64 =
        argc >= 6 ? parse_u32(argv[5], 0u) != 0u : false;
#if defined(_WIN32) && defined(_M_X64)
    constexpr bool native_x64_backend_available = true;
#else
    constexpr bool native_x64_backend_available = false;
#endif

    auto runner = jojo::Ps1CommercialEvidenceRunner::open(source);
    if (!runner) {
        std::cerr << "open_error=" << static_cast<unsigned>(runner.error) << "\n";
        std::cerr << "open_detail=" << runner.detail << "\n";
        return 3;
    }

    runner.value.set_native_x64_enabled(native_x64);

    jojo::Ps1CommercialEvidenceOptions options{};
    options.boot.instruction_budget = segment_budget;
    options.boot.trace_capacity = 4096u;
    options.boot.mmio_event_capacity = 1024u;
    options.boot.bios_event_capacity = 1024u;
    options.boot.stagnation_instruction_limit = 0u;
    options.max_execution_segments = max_segments;

    const auto report = runner.value.run(options);
    const auto saved =
        jojo::save_ps1_commercial_evidence_report_atomic(report_path, report);
    if (!saved) {
        std::cerr << "save_error=" << static_cast<unsigned>(saved.error) << "\n";
        std::cerr << "save_detail=" << saved.detail << "\n";
        return 4;
    }

    std::cout << "revision_id=" << report.source.revision_id << "\n";
    std::cout << "source_size=" << report.source.source_size << "\n";
    std::cout << "source_hash_fnv1a64=" << report.source.source_hash_fnv1a64 << "\n";
    std::cout << "native_x64_requested="
              << (native_x64 ? 1 : 0)
              << "\n";
    std::cout << "native_x64_backend_available="
              << (native_x64_backend_available ? 1 : 0)
              << "\n";
    std::cout << "native_x64_enabled="
              << (runner.value.native_x64_enabled() ? 1 : 0)
              << "\n";
    std::cout << "native_x64_instructions_retired="
              << report.total_native_x64_instructions_retired
              << "\n";
    std::cout << "reference_instructions_retired="
              << report.total_reference_instructions_retired
              << "\n";
    std::cout << "native_x64_cache_compilations="
              << report.total_native_x64_cache_compilations
              << "\n";
    std::cout << "native_x64_cache_reuses="
              << report.total_native_x64_cache_reuses
              << "\n";
    std::cout << "frontier="
              << jojo::ps1_commercial_frontier_class_name(report.frontier)
              << "\n";
    std::cout << "stop_reason="
              << static_cast<unsigned>(report.boot.stop_reason)
              << "\n";
    std::cout << "total_execution_steps="
              << report.total_execution_steps << "\n";
    std::cout << "total_instructions_retired="
              << report.total_instructions_retired << "\n";
    std::cout << "execution_segments=" << report.execution_segments << "\n";
    std::cout << "presented_frames=" << report.boot.presented_frames << "\n";
    if (report.first_frame) {
        std::cout << "frame_width=" << report.first_frame->width << "\n";
        std::cout << "frame_height=" << report.first_frame->height << "\n";
        std::cout << "frame_non_black_pixels="
                  << report.first_frame->non_black_pixels << "\n";
    }
    std::cout << "report_path=" << report_path.string() << "\n";
    return 0;
}
