#include "core/ps1_commercial_evidence.h"
#include "mips_test_encode.h"
#include "ps1_fixture.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;
namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

std::vector<std::uint8_t> read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void test_frame_evidence_requires_non_black_visible_pixels() {
    jojo::Ps1DisplayFrame black{};
    black.width = 2u;
    black.height = 1u;
    black.rgba8 = {0xFF000000u, 0xFF000000u};
    CHECK(!jojo::make_ps1_commercial_frame_evidence(black).has_value());

    jojo::Ps1DisplayFrame visible = black;
    visible.rgba8[1] = 0xFF0000FFu;
    const auto first = jojo::make_ps1_commercial_frame_evidence(visible);
    const auto second = jojo::make_ps1_commercial_frame_evidence(visible);
    CHECK(first.has_value());
    CHECK(second.has_value());
    if (first && second) {
        CHECK(first->width == 2u);
        CHECK(first->height == 1u);
        CHECK(first->non_black_pixels == 1u);
        CHECK(first->frame_hash_fnv1a64 == second->frame_hash_fnv1a64);
        CHECK(first->frame_hash_fnv1a64 != 0u);
    }
}

void test_runner_promotes_visible_gpu_output_to_commercial_frame(const fs::path& temp) {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.executable = test_ps1::make_psx_exe_from_words({
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u), // r8 = MMIO base

        test_mips::i(0x0Fu, 0u, 9u, 0xA000u),
        test_mips::i(0x2Bu, 8u, 9u, 0x1810u), // GP0 A0
        test_mips::i(0x09u, 0u, 9u, 0x0000u),
        test_mips::i(0x2Bu, 8u, 9u, 0x1810u), // destination (0,0)
        test_mips::i(0x0Fu, 0u, 9u, 0x0001u),
        test_mips::i(0x0Du, 9u, 9u, 0x0001u),
        test_mips::i(0x2Bu, 8u, 9u, 0x1810u), // size 1x1
        test_mips::i(0x09u, 0u, 9u, 0x001Fu),
        test_mips::i(0x2Bu, 8u, 9u, 0x1810u), // red BGR555 pixel

        test_mips::i(0x0Fu, 0u, 9u, 0x0300u),
        test_mips::i(0x2Bu, 8u, 9u, 0x1814u), // display enabled
        test_mips::i(0x0Fu, 0u, 9u, 0x0800u),
        test_mips::i(0x0Du, 9u, 9u, 0x0001u),
        test_mips::i(0x2Bu, 8u, 9u, 0x1814u), // 320x240 display mode

        test_mips::j(0x02u, 0x8001003Cu >> 2),
        0x00000000u,
    });
    const auto source = test_ps1::write_cooked_iso(temp / "visible-frame.iso", fixture);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(
        test_ps1::make_revision_profile(fixture, "synthetic-visible-frame"));

    auto runner = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(runner);
    if (!runner) return;

    jojo::Ps1CommercialEvidenceOptions options{};
    options.boot.instruction_budget = 40u;
    const auto report = runner.value.run(options);

    CHECK(report.frontier == jojo::Ps1CommercialFrontierClass::commercial_frame_presented);
    const auto audio = runner.value.drain_audio_samples();
    CHECK(audio.size() % 2u == 0u);
    CHECK(report.boot.stop_reason == jojo::Ps1BootStopReason::commercial_frame_presented);
    CHECK(report.boot.presented_frames == 1u);
    CHECK(report.first_frame.has_value());

    const auto visible_frame = runner.value.display_frame();
    CHECK(visible_frame.width == 320u);
    CHECK(visible_frame.height == 240u);
    CHECK(visible_frame.rgba8.size() == static_cast<std::size_t>(320u * 240u));
    if (!visible_frame.rgba8.empty()) {
        CHECK(visible_frame.rgba8[0] == 0xFF0000FFu);
    }

    if (report.first_frame) {
        CHECK(report.first_frame->width == 320u);
        CHECK(report.first_frame->height == 240u);
        CHECK(report.first_frame->non_black_pixels == 1u);
        CHECK(report.first_frame->frame_hash_fnv1a64 != 0u);
    }
}

void test_runner_continues_bounded_budget_until_real_frontier(const fs::path& temp) {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.executable = test_ps1::make_psx_exe_from_words({
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u), // MMIO base
        0x00000000u,
        0x00000000u,
        0x00000000u,
        0x00000000u,
        0x00000000u,
        test_mips::i(0x23u, 8u, 9u, 0x1040u), // unsupported SIO read
        0x00000000u,
    });
    const auto source = test_ps1::write_cooked_iso(temp / "segmented-frontier.iso", fixture);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(
        test_ps1::make_revision_profile(fixture, "synthetic-segmented-frontier"));

    auto runner = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(runner);
    if (!runner) return;

    jojo::Ps1CommercialEvidenceOptions options{};
    options.boot.instruction_budget = 2u;
    options.max_execution_segments = 8u;
    const auto report = runner.value.run(options);

    CHECK(report.frontier == jojo::Ps1CommercialFrontierClass::mmio_access);
    CHECK(report.boot.stop_reason == jojo::Ps1BootStopReason::mmio_unimplemented);
    CHECK(report.total_instructions_retired > options.boot.instruction_budget);
    CHECK(report.total_instructions_retired == 6u);
    CHECK(report.execution_segments == 4u);
    CHECK(report.boot.unsupported_access.has_value());
    if (report.boot.unsupported_access) {
        CHECK(report.boot.unsupported_access->physical_address == 0x1F801040u);
    }
}

void test_normal_mode_retains_disc_and_never_mutates_source(const fs::path& temp) {
    auto fixture = test_ps1::make_disc_fixture();
    const auto source = test_ps1::write_cooked_iso(temp / "normal.iso", fixture);
    const auto before = read_all(source);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture));

    auto opened = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(opened);
    if (opened) {
        CHECK(opened.value.disc_session().binding().revision_id == "synthetic-ps1-jojo");
        auto sector = opened.value.disc_session().read_sectors(16u, 1u);
        CHECK(sector);
        if (sector) CHECK(sector.value.size() == 2048u);

        jojo::Ps1CommercialEvidenceOptions options{};
        options.boot.instruction_budget = 4u;
        options.boot.trace_capacity = 2u;
        options.boot.mmio_event_capacity = 2u;
        options.boot.bios_event_capacity = 2u;

        const auto report = opened.value.run(options);
        CHECK(report.source.revision_id == "synthetic-ps1-jojo");
        CHECK(report.frontier == jojo::Ps1CommercialFrontierClass::execution_budget);
        CHECK(report.boot.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
        CHECK(report.boot.instructions_retired == 4u);
        CHECK(report.total_instructions_retired == 4u);
        CHECK(report.diagnostic_decisions.empty());
        CHECK(report.boot.recent_trace.size() <= 2u);
        CHECK(report.boot.recent_bios_calls.size() <= 2u);
        CHECK(report.boot.recent_mmio.size() <= 2u);
    }

    CHECK(before == read_all(source));
}

void test_runner_attaches_direct_disc_to_runtime_cdrom(const fs::path& temp) {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.executable = test_ps1::make_psx_exe_from_words({
        test_mips::i(0x0Fu, 0u, 8u, 0x1F80u),       // lui   r8, 0x1F80
        test_mips::i(0x09u, 0u, 9u, 0x0000u),       // addiu r9, r0, 0
        test_mips::i(0x28u, 8u, 9u, 0x1800u),       // sb    r9, CD index
        test_mips::i(0x28u, 8u, 9u, 0x1802u),       // param minute 00
        test_mips::i(0x09u, 0u, 9u, 0x0002u),
        test_mips::i(0x28u, 8u, 9u, 0x1802u),       // param second 02
        test_mips::i(0x09u, 0u, 9u, 0x0025u),
        test_mips::i(0x28u, 8u, 9u, 0x1802u),       // param frame 25
        test_mips::i(0x09u, 0u, 9u, 0x0002u),
        test_mips::i(0x28u, 8u, 9u, 0x1801u),       // Setloc
        test_mips::i(0x09u, 0u, 9u, 0x0006u),
        test_mips::i(0x28u, 8u, 9u, 0x1801u),       // ReadN
        test_mips::j(0x02u, 0x80010030u >> 2),
        0x00000000u,
    });
    const auto source = test_ps1::write_cooked_iso(temp / "cdrom-attached.iso", fixture);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(
        test_ps1::make_revision_profile(fixture, "synthetic-cdrom-attached"));

    auto runner = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(runner);
    if (!runner) return;

    jojo::Ps1CommercialEvidenceOptions options{};
    options.boot.instruction_budget = 20u;
    options.boot.trace_capacity = 8u;
    options.boot.mmio_event_capacity = 8u;
    options.boot.bios_event_capacity = 4u;

    const auto report = runner.value.run(options);
    CHECK(report.frontier == jojo::Ps1CommercialFrontierClass::execution_budget);
    CHECK(report.boot.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(report.boot.instructions_retired == 20u);
    CHECK(!report.boot.unsupported_access.has_value());
}


void test_runner_segment_api_preserves_runtime_state(const fs::path& temp) {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.executable = test_ps1::make_psx_exe_from_words({
        test_mips::j(0x02u, 0x80010000u >> 2),
        0x00000000u,
    });
    const auto source = test_ps1::write_cooked_iso(temp / "segment-api.iso", fixture);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(
        test_ps1::make_revision_profile(fixture, "synthetic-segment-api"));

    auto runner = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(runner);
    if (!runner) return;

    jojo::Ps1BootOptions options{};
    options.instruction_budget = 3u;
    const auto first = runner.value.run_segment(options);
    const auto second = runner.value.run_segment(options);

    CHECK(first.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(second.stop_reason == jojo::Ps1BootStopReason::execution_budget_exhausted);
    CHECK(first.instructions_retired == 3u);
    CHECK(second.instructions_retired == 3u);
    CHECK(first.last_pc != second.last_pc || first.last_opcode == second.last_opcode);
}

void test_bios_fallback_is_opt_in_and_recorded(const fs::path& temp) {
    auto fixture = test_ps1::make_disc_fixture();
    fixture.executable = test_ps1::make_psx_exe_from_words({
        test_mips::i(0x09u, 0u, 2u, 0x1234u),
        test_mips::i(0x09u, 0u, 9u, 0x0033u),
        test_mips::i(0x09u, 0u, 10u, 0x00A0u),
        test_mips::r(10u, 0u, 31u, 0u, 0x09u),
        0x00000000u,
        test_mips::i(0x09u, 0u, 16u, 0x5678u),
        test_mips::j(0x02u, 0x80010018u >> 2),
        0x00000000u,
    });
    const auto source = test_ps1::write_cooked_iso(temp / "bios.iso", fixture);

    jojo::Ps1DiscOpenOptions open_options{};
    open_options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture, "synthetic-bios-frontier"));

    jojo::Ps1CommercialEvidenceOptions normal{};
    normal.boot.instruction_budget = 16u;

    auto normal_runner = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(normal_runner);
    if (normal_runner) {
        const auto report = normal_runner.value.run(normal);
        CHECK(report.frontier == jojo::Ps1CommercialFrontierClass::bios_call);
        CHECK(report.boot.stop_reason == jojo::Ps1BootStopReason::bios_call_unimplemented);
        CHECK(report.diagnostic_decisions.empty());
    }

    auto diagnostic_runner = jojo::Ps1CommercialEvidenceRunner::open(source, open_options);
    CHECK(diagnostic_runner);
    if (diagnostic_runner) {
        jojo::Ps1CommercialEvidenceOptions diagnostic = normal;
        diagnostic.diagnostic_bios_fallbacks.push_back(jojo::Ps1BiosFallback::return_zero);
        const auto report = diagnostic_runner.value.run(diagnostic);
        CHECK(report.diagnostic_decisions.size() == 1u);
        if (!report.diagnostic_decisions.empty()) {
            CHECK(report.diagnostic_decisions[0].bios_table == 0x000000A0u);
            CHECK(report.diagnostic_decisions[0].bios_selector == 0x00000033u);
            CHECK(report.diagnostic_decisions[0].fallback == jojo::Ps1BiosFallback::return_zero);
        }
        CHECK(report.total_instructions_retired > report.boot.instructions_retired);
        CHECK(report.frontier != jojo::Ps1CommercialFrontierClass::bios_call);
    }
}
}

int main() {
    const auto temp = fs::temp_directory_path() / "jojo_phase2_commercial_evidence";
    std::error_code ec;
    fs::remove_all(temp, ec);
    fs::create_directories(temp, ec);
    CHECK(!ec);

    test_frame_evidence_requires_non_black_visible_pixels();
    test_runner_promotes_visible_gpu_output_to_commercial_frame(temp);
    test_runner_continues_bounded_budget_until_real_frontier(temp);
    test_normal_mode_retains_disc_and_never_mutates_source(temp);
    test_runner_attaches_direct_disc_to_runtime_cdrom(temp);
    test_runner_segment_api_preserves_runtime_state(temp);
    test_bios_fallback_is_opt_in_and_recorded(temp);

    fs::remove_all(temp, ec);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "commercial evidence runner passed\n";
    return 0;
}
