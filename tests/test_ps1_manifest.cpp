#include "core/conversion.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_manifest() {
    auto path = fs::temp_directory_path() / "jojo_ps1_manifest_v2.ini";
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(path.string() + ".tmp", ec);
    return path;
}

static jojo::ConversionManifest valid_manifest() {
    jojo::ConversionManifest m{};
    m.converter_version = "test-converter";
    m.platform = "playstation";
    m.game_id = "jojo-ps1";
    m.source_name = "owned.bin";
    m.source_format = "bin";
    m.source_size = 123456u;
    m.source_hash_fnv1a64 = "0123456789abcdef";
    m.revision_id = "synthetic-revision";
    m.system_cnf_path = "/SYSTEM.CNF";
    m.boot_executable = "/SLUS_TEST.00";
    m.psx_exe_hash_fnv1a64 = "fedcba9876543210";
    m.psx_exe_entry = 0x80010000u;
    m.psx_exe_load_address = 0x80010000u;
    m.psx_exe_initial_gp = 0u;
    m.psx_exe_text_size = 16u;
    m.psx_exe_stack_base = 0u;
    m.psx_exe_stack_size = 0u;
    m.media_status = "verified";
    m.executable_status = "verified";
    return m;
}

static void expect_invalid(jojo::ConversionManifest manifest) {
    const auto path = temp_manifest();
    const auto saved = jojo::save_conversion_manifest_atomic(path, manifest);
    CHECK(!saved);
    if (!saved) CHECK(saved.error == jojo::ErrorCode::invalid_installation);
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(path.string() + ".tmp", ec);
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

static void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

static void expect_load_invalid(const fs::path& path) {
    const auto loaded = jojo::load_conversion_manifest(path);
    CHECK(!loaded);
    if (!loaded) CHECK(loaded.error == jojo::ErrorCode::invalid_installation);
}

static void test_round_trip_all_v2_fields() {
    const auto path = temp_manifest();
    const auto expected = valid_manifest();
    CHECK(jojo::save_conversion_manifest_atomic(path, expected));
    const auto loaded = jojo::load_conversion_manifest(path);
    CHECK(loaded);
    if (loaded) {
        const auto& m = loaded.value;
        CHECK(m.manifest_version == "2");
        CHECK(m.converter_version == expected.converter_version);
        CHECK(m.platform == expected.platform);
        CHECK(m.game_id == expected.game_id);
        CHECK(m.source_name == expected.source_name);
        CHECK(m.source_format == expected.source_format);
        CHECK(m.source_size == expected.source_size);
        CHECK(m.source_hash_fnv1a64 == expected.source_hash_fnv1a64);
        CHECK(m.revision_id == expected.revision_id);
        CHECK(m.system_cnf_path == expected.system_cnf_path);
        CHECK(m.boot_executable == expected.boot_executable);
        CHECK(m.psx_exe_hash_fnv1a64 == expected.psx_exe_hash_fnv1a64);
        CHECK(m.psx_exe_entry == expected.psx_exe_entry);
        CHECK(m.psx_exe_load_address == expected.psx_exe_load_address);
        CHECK(m.psx_exe_initial_gp == expected.psx_exe_initial_gp);
        CHECK(m.psx_exe_text_size == expected.psx_exe_text_size);
        CHECK(m.psx_exe_stack_base == expected.psx_exe_stack_base);
        CHECK(m.psx_exe_stack_size == expected.psx_exe_stack_size);
        CHECK(m.media_status == expected.media_status);
        CHECK(m.executable_status == expected.executable_status);
        CHECK(m.mips_analysis_status == "pending");
        CHECK(m.reference_runtime_status == "pending");
        CHECK(m.native_codegen_status == "pending");
        CHECK(m.hardware_runtime_status == "pending");
        CHECK(m.boot_status == "pending");
        CHECK(m.rendering_status == "pending");
        CHECK(m.audio_status == "pending");
        CHECK(m.input_status == "pending");
        CHECK(m.gameplay_status == "pending");
    }
    std::error_code ec;
    fs::remove(path, ec);
}

static void test_truth_validation() {
    auto m = valid_manifest();
    m.manifest_version = "1";
    expect_invalid(std::move(m));

    m = valid_manifest(); m.platform = "dreamcast"; expect_invalid(std::move(m));
    m = valid_manifest(); m.media_status = "pending"; expect_invalid(std::move(m));
    m = valid_manifest(); m.system_cnf_path.clear(); expect_invalid(std::move(m));
    m = valid_manifest(); m.boot_executable.clear(); expect_invalid(std::move(m));
    m = valid_manifest(); m.psx_exe_hash_fnv1a64.clear(); expect_invalid(std::move(m));
    m = valid_manifest(); m.psx_exe_entry.reset(); expect_invalid(std::move(m));
    m = valid_manifest(); m.psx_exe_load_address.reset(); expect_invalid(std::move(m));
    m = valid_manifest(); m.psx_exe_initial_gp.reset(); expect_invalid(std::move(m));
    m = valid_manifest(); m.psx_exe_text_size.reset(); expect_invalid(std::move(m));
    m = valid_manifest(); m.psx_exe_stack_base.reset(); expect_invalid(std::move(m));
    m = valid_manifest(); m.psx_exe_stack_size.reset(); expect_invalid(std::move(m));

    const char* later[] = {
        "mips", "reference", "native", "hardware", "boot", "render", "audio", "input", "gameplay"
    };
    for (const auto* which : later) {
        m = valid_manifest();
        m.executable_status = "pending";
        const std::string key(which);
        if (key == "mips") m.mips_analysis_status = "verified";
        else if (key == "reference") m.reference_runtime_status = "verified";
        else if (key == "native") m.native_codegen_status = "verified";
        else if (key == "hardware") m.hardware_runtime_status = "verified";
        else if (key == "boot") m.boot_status = "verified";
        else if (key == "render") m.rendering_status = "verified";
        else if (key == "audio") m.audio_status = "verified";
        else if (key == "input") m.input_status = "verified";
        else if (key == "gameplay") m.gameplay_status = "verified";
        expect_invalid(std::move(m));
    }
}

static void test_strict_v2_load_rejects_duplicate_malformed_overflow_and_missing_keys() {
    const auto path = temp_manifest();
    const auto manifest = valid_manifest();
    CHECK(jojo::save_conversion_manifest_atomic(path, manifest));
    const auto baseline = read_text(path);

    write_text(path, baseline + "media_status=verified\n");
    expect_load_invalid(path);

    auto malformed = baseline;
    const std::string entry_line = "psx_exe_entry=0x80010000";
    const auto entry_pos = malformed.find(entry_line);
    CHECK(entry_pos != std::string::npos);
    if (entry_pos != std::string::npos) {
        malformed.replace(entry_pos, entry_line.size(), "psx_exe_entry=0xzzzzzzzz");
        write_text(path, malformed);
        expect_load_invalid(path);
    }

    auto overflow = baseline;
    const std::string size_line = "psx_exe_text_size=16";
    const auto size_pos = overflow.find(size_line);
    CHECK(size_pos != std::string::npos);
    if (size_pos != std::string::npos) {
        overflow.replace(size_pos, size_line.size(), "psx_exe_text_size=4294967296");
        write_text(path, overflow);
        expect_load_invalid(path);
    }

    auto missing = baseline;
    const std::string required_line = "game_id=jojo-ps1\n";
    const auto required_pos = missing.find(required_line);
    CHECK(required_pos != std::string::npos);
    if (required_pos != std::string::npos) {
        missing.erase(required_pos, required_line.size());
        write_text(path, missing);
        expect_load_invalid(path);
    }

    std::error_code ec;
    fs::remove(path, ec);
}

int main() {
    test_round_trip_all_v2_fields();
    test_truth_validation();
    test_strict_v2_load_rejects_duplicate_malformed_overflow_and_missing_keys();
    return failures ? 1 : 0;
}
