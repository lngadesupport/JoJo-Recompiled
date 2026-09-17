#include "core/ps1_exe.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace jojo {
namespace {
constexpr std::size_t kHeaderSize = 0x800u;

std::uint32_t read_le32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset + 0]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::string fnv1a64_hex(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

Result<Ps1Executable> invalid_exe(std::string detail) {
    return Result<Ps1Executable>::failure(ErrorCode::unsupported_format, std::move(detail));
}
}

Result<Ps1Executable> parse_ps1_executable(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) {
        return invalid_exe("PS-X EXE file is smaller than the 0x800-byte header");
    }
    if (std::memcmp(bytes.data(), "PS-X EXE", 8) != 0) {
        return invalid_exe("PS-X EXE signature is missing");
    }

    Ps1ExeMetadata metadata{};
    metadata.entry_pc = read_le32(bytes, 0x010);
    metadata.initial_gp = read_le32(bytes, 0x014);
    metadata.text_load_address = read_le32(bytes, 0x018);
    metadata.text_size = read_le32(bytes, 0x01C);
    metadata.stack_base = read_le32(bytes, 0x030);
    metadata.stack_size = read_le32(bytes, 0x034);

    if ((metadata.entry_pc & 3u) != 0u) {
        return invalid_exe("PS-X EXE entry PC is not 4-byte aligned");
    }
    if ((metadata.text_load_address & 3u) != 0u) {
        return invalid_exe("PS-X EXE text load address is not 4-byte aligned");
    }
    if ((metadata.text_size & 3u) != 0u) {
        return invalid_exe("PS-X EXE text size is not 4-byte aligned");
    }
    if (static_cast<std::uint64_t>(metadata.text_size) > bytes.size() - kHeaderSize) {
        return invalid_exe("PS-X EXE text size exceeds the available payload");
    }

    metadata.fnv1a64_hex = fnv1a64_hex(bytes);
    Ps1Executable result{};
    result.metadata = std::move(metadata);
    result.file_bytes.assign(bytes.begin(), bytes.end());
    return Result<Ps1Executable>::success(std::move(result));
}

Result<Ps1Executable> read_ps1_executable(const Iso9660Image& image, std::string_view iso_path) {
    auto bytes = read_iso9660_file(image, iso_path);
    if (!bytes) return Result<Ps1Executable>::failure(bytes.error, bytes.detail);
    return parse_ps1_executable(bytes.value);
}

}
