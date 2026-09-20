#include "content/pac_archive.h"

#include <cstddef>
#include <limits>

namespace jojo::content {
namespace {

constexpr std::size_t kPacSectorSize = 2048u;
constexpr std::size_t kPacHeaderBytes = 8u;
constexpr std::size_t kPacEntryBytes = 8u;

std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8u) |
        (static_cast<std::uint32_t>(p[2]) << 16u) |
        (static_cast<std::uint32_t>(p[3]) << 24u);
}

bool align_sector(
    std::size_t value,
    std::size_t& aligned) noexcept {
    if (value >
        std::numeric_limits<std::size_t>::max() -
            (kPacSectorSize - 1u)) {
        return false;
    }
    aligned =
        (value + kPacSectorSize - 1u) &
        ~(kPacSectorSize - 1u);
    return true;
}

} // namespace

Result<std::vector<PacChunk>> parse_pac_archive(
    std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kPacSectorSize) {
        return Result<std::vector<PacChunk>>::failure(
            ErrorCode::unsupported_format,
            "PAC is smaller than one 2048-byte sector");
    }

    const auto count =
        static_cast<std::size_t>(le32(bytes.data()));
    const auto declared_size =
        static_cast<std::size_t>(le32(bytes.data() + 4u));

    if (declared_size != bytes.size()) {
        return Result<std::vector<PacChunk>>::failure(
            ErrorCode::invalid_installation,
            "PAC declared size does not match file size");
    }
    if (count == 0u ||
        count >
            (kPacSectorSize - kPacHeaderBytes) /
                kPacEntryBytes) {
        return Result<std::vector<PacChunk>>::failure(
            ErrorCode::invalid_installation,
            "PAC chunk table does not fit in its header sector");
    }

    std::vector<PacChunk> chunks;
    chunks.reserve(count);
    std::size_t payload_offset = kPacSectorSize;

    for (std::size_t index = 0u; index < count; ++index) {
        const auto table =
            kPacHeaderBytes + index * kPacEntryBytes;
        const auto type =
            le32(bytes.data() + table);
        const auto size =
            static_cast<std::size_t>(
                le32(bytes.data() + table + 4u));

        if (payload_offset > bytes.size() ||
            size > bytes.size() - payload_offset) {
            return Result<std::vector<PacChunk>>::failure(
                ErrorCode::invalid_installation,
                "PAC chunk extends past the container");
        }

        PacChunk chunk{};
        chunk.type = type;
        chunk.bytes.assign(
            bytes.begin() +
                static_cast<std::ptrdiff_t>(payload_offset),
            bytes.begin() +
                static_cast<std::ptrdiff_t>(
                    payload_offset + size));
        chunks.push_back(std::move(chunk));

        std::size_t aligned{};
        if (!align_sector(size, aligned) ||
            payload_offset >
                std::numeric_limits<std::size_t>::max() -
                    aligned) {
            return Result<std::vector<PacChunk>>::failure(
                ErrorCode::invalid_installation,
                "PAC chunk alignment overflows");
        }
        payload_offset += aligned;
    }

    if (payload_offset != bytes.size()) {
        return Result<std::vector<PacChunk>>::failure(
            ErrorCode::invalid_installation,
            "PAC payload layout does not consume the container exactly");
    }

    return Result<std::vector<PacChunk>>::success(
        std::move(chunks));
}

} // namespace jojo::content
