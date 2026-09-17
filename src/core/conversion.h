#pragma once
#include "core/result.h"
#include "core/revision.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

enum class ConversionStage {
    validating_source,
    fingerprinting_source,
    discovering_filesystem,
    identifying_revision,
    reading_system_cnf,
    reading_psx_exe,
    preparing_installation,
    copying_local_data,
    writing_manifest,
    activating_installation,
    completed
};

struct ConversionProgress {
    ConversionStage stage{ConversionStage::validating_source};
    int percent{};
    std::string message_key;
    std::string detail;
};

using ConversionProgressCallback = std::function<void(const ConversionProgress&)>;

struct ConversionOptions {
    std::vector<GameRevisionProfile> revision_profiles;
};

struct ConversionManifest {
    std::string manifest_version{"2"};
    std::string converter_version;
    std::string platform{"playstation"};
    std::string game_id{"jojo-ps1"};
    std::string source_name;
    std::string source_format;
    std::uint64_t source_size{};
    std::string source_hash_fnv1a64;
    std::string revision_id;
    std::string system_cnf_path;
    std::string boot_executable;
    std::string psx_exe_hash_fnv1a64;
    std::optional<std::uint32_t> psx_exe_entry;
    std::optional<std::uint32_t> psx_exe_load_address;
    std::optional<std::uint32_t> psx_exe_initial_gp;
    std::optional<std::uint32_t> psx_exe_text_size;
    std::optional<std::uint32_t> psx_exe_stack_base;
    std::optional<std::uint32_t> psx_exe_stack_size;
    std::string media_status{"pending"};
    std::string executable_status{"pending"};
    std::string mips_analysis_status{"pending"};
    std::string reference_runtime_status{"pending"};
    std::string native_codegen_status{"pending"};
    std::string hardware_runtime_status{"pending"};
    std::string boot_status{"pending"};
    std::string rendering_status{"pending"};
    std::string audio_status{"pending"};
    std::string input_status{"pending"};
    std::string gameplay_status{"pending"};

    // Temporary v1 compatibility bridge. The legacy Dreamcast conversion/runtime
    // path is removed later in this milestone; new PS1 manifests never serialize
    // these fields.
    std::string hash_hex;
    std::string backend{"pending-game-specific-recompiler"};
    std::string boot_program_hash_hex;
    std::optional<std::uint32_t> backend_abi_version;
    std::string backend_program_hash;
    std::optional<std::uint64_t> backend_block_count;
    std::optional<std::uint64_t> backend_native_block_count;
    std::optional<std::uint64_t> backend_fallback_block_count;
    std::optional<std::uint64_t> backend_native_code_bytes;
};

[[nodiscard]] bool has_complete_native_backend_metadata(
    const ConversionManifest& manifest) noexcept;
[[nodiscard]] Result<GameRevisionMatch> identify_observed_disc_revision(
    std::string_view source_format,
    std::uint64_t source_size,
    std::string_view hash_hex);
[[nodiscard]] Result<ConversionManifest> convert_image(
    const std::filesystem::path& source,
    const std::filesystem::path& install_dir,
    const ConversionOptions& options,
    const ConversionProgressCallback& on_progress = {});
[[nodiscard]] Result<ConversionManifest> convert_image(
    const std::filesystem::path& source,
    const std::filesystem::path& install_dir,
    const ConversionProgressCallback& on_progress = {});
[[nodiscard]] Result<ConversionManifest> load_conversion_manifest(
    const std::filesystem::path& manifest_path);
[[nodiscard]] Result<void> save_conversion_manifest_atomic(
    const std::filesystem::path& manifest_path,
    const ConversionManifest& manifest);

}
