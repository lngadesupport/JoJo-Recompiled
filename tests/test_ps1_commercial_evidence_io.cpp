#include "core/ps1_commercial_evidence_io.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)
}

int main() {
    jojo::Ps1CommercialEvidenceReport report{};
    report.source.source_format = "bin";
    report.source.source_size = 666806112u;
    report.source.source_hash_fnv1a64 = "b8b5dbf79cdb9fcf";
    report.source.revision_id = "jojo-usa-observed-b8b5dbf79cdb9fcf";
    report.frontier = jojo::Ps1CommercialFrontierClass::bios_call;
    report.total_instructions_retired = 123456u;
    report.execution_segments = 3u;
    report.pad_poll_count = {11u, 7u};
    report.memory_card_read_sector_count = {3u, 1u};
    report.memory_card_write_sector_count = {2u, 0u};
    report.spu_sample_frames = 44100u;
    report.boot.stop_reason = jojo::Ps1BootStopReason::bios_call_unimplemented;
    report.boot.instructions_retired = 456u;
    report.boot.last_pc = 0x000000A0u;
    report.boot.last_opcode = 0x0120F809u;
    report.boot.diagnostic_probe_mode = true;
    report.boot.speculative_mmio_count = 3u;
    report.boot.bios_call_count = 5u;
    report.boot.interrupts_accepted = 2u;
    report.boot.dma_transfer_count = 4u;
    report.boot.gpu_gp0_command_count = 7u;
    report.boot.gpu_gp1_command_count = 8u;
    report.boot.unsupported_gpu_gp0_command = 0xFEu;
    report.boot.unsupported_gpu_gp1_command = 0x09u;
    report.boot.vram_write_count = 9u;
    report.boot.presented_frames = 1u;
    report.first_frame = jojo::Ps1CommercialFrameEvidence{
        320u,
        240u,
        0x0123456789ABCDEFull,
        1234u,
    };
    report.boot.cpu_diagnostic = jojo::R3000aDiagnostic{
        jojo::R3000aBoundaryCode::cop2_unimplemented,
        jojo::R3000aStage::cop2,
        0x80023450u,
        0x4A000001u,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        2u,
        std::nullopt,
        std::nullopt,
    };
    report.boot.recent_trace.push_back({0x80010000u, 0x24080001u});
    report.boot.recent_bios_calls.push_back({0x8001000Cu, 0x000000A0u, 0x33u, 1u, 2u, 3u, 4u, 0x80010014u});
    report.boot.recent_mmio.push_back({0x80010100u, 0x1F801810u, 32u, true, 0xE1000400u, false});
    report.boot.recent_cdrom_commands.push_back({0x06u, 0u, 0x02u});
    report.boot.unsupported_access = jojo::Ps1UnsupportedAccess{0xBF801810u, 0x1F801810u, 32u, true, 0xE1000400u};
    report.diagnostic_decisions.push_back({0xA0u, 0x33u, jojo::Ps1BiosFallback::return_zero});

    const auto text = jojo::format_ps1_commercial_evidence_report(report);
    CHECK(text.find("format=jojo-commercial-frontier-v1") != std::string::npos);
    CHECK(text.find("source_format=bin") != std::string::npos);
    CHECK(text.find("source_size=666806112") != std::string::npos);
    CHECK(text.find("source_hash_fnv1a64=b8b5dbf79cdb9fcf") != std::string::npos);
    CHECK(text.find("revision_id=jojo-usa-observed-b8b5dbf79cdb9fcf") != std::string::npos);
    CHECK(text.find("frontier=bios_call") != std::string::npos);
    CHECK(text.find("stop_reason=bios_call_unimplemented") != std::string::npos);
    CHECK(text.find("total_instructions_retired=123456") != std::string::npos);
    CHECK(text.find("execution_segments=3") != std::string::npos);
    CHECK(text.find("pad0_poll_count=11") != std::string::npos);
    CHECK(text.find("pad1_poll_count=7") != std::string::npos);
    CHECK(text.find("memory_card0_read_sector_count=3") != std::string::npos);
    CHECK(text.find("memory_card0_write_sector_count=2") != std::string::npos);
    CHECK(text.find("memory_card1_read_sector_count=1") != std::string::npos);
    CHECK(text.find("memory_card1_write_sector_count=0") != std::string::npos);
    CHECK(text.find("spu_sample_frames=44100") != std::string::npos);
    CHECK(text.find("last_pc=0x000000a0") != std::string::npos);
    CHECK(text.find("bios_event_0_selector=0x00000033") != std::string::npos);
    CHECK(text.find("mmio_event_0_address=0x1f801810") != std::string::npos);
    CHECK(text.find("cdrom_event_0_command=6") != std::string::npos);
    CHECK(text.find("dma_transfer_count=4") != std::string::npos);
    CHECK(text.find("gpu_gp0_command_count=7") != std::string::npos);
    CHECK(text.find("unsupported_gpu_gp0_command=0xfe") != std::string::npos);
    CHECK(text.find("unsupported_gpu_gp1_command=0x09") != std::string::npos);
    CHECK(text.find("frame_width=320") != std::string::npos);
    CHECK(text.find("frame_height=240") != std::string::npos);
    CHECK(text.find("frame_hash_fnv1a64=0123456789abcdef") != std::string::npos);
    CHECK(text.find("frame_non_black_pixels=1234") != std::string::npos);
    CHECK(text.find("cpu_boundary=" +
                    std::to_string(static_cast<unsigned>(jojo::R3000aBoundaryCode::cop2_unimplemented))) !=
          std::string::npos);
    CHECK(text.find("cpu_stage=" +
                    std::to_string(static_cast<unsigned>(jojo::R3000aStage::cop2))) !=
          std::string::npos);
    CHECK(text.find("cpu_pc=0x80023450") != std::string::npos);
    CHECK(text.find("cpu_opcode=0x4a000001") != std::string::npos);
    CHECK(text.find("cpu_coprocessor=2") != std::string::npos);
    CHECK(text.find("diagnostic_decision_0_fallback=return_zero") != std::string::npos);
    CHECK(text.find("PS-X EXE") == std::string::npos);
    CHECK(text.find("source_path=") == std::string::npos);
    CHECK(text.size() < 65536u);

    const auto path = fs::temp_directory_path() / "jojo-commercial-frontier-test.txt";
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(path.string() + ".tmp", ec);
    const auto saved = jojo::save_ps1_commercial_evidence_report_atomic(path, report);
    CHECK(saved);
    if (saved) {
        std::ifstream in(path, std::ios::binary);
        const std::string disk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(disk == text);
    }
    CHECK(!fs::exists(path.string() + ".tmp"));
    fs::remove(path, ec);

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "commercial evidence IO passed\n";
    return 0;
}
