#include "core/ps1_memory_card.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint8_t xor_checksum(
    const std::vector<std::uint8_t>& data,
    std::size_t offset,
    std::size_t count) noexcept {
    std::uint8_t value = 0u;
    for (std::size_t i = 0u; i < count; ++i) value ^= data[offset + i];
    return value;
}

Result<void> replace_file(
    const std::filesystem::path& temp,
    const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (!ec) return Result<void>::success();

    ec.clear();
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return Result<void>::failure(
            ErrorCode::io_error,
            "failed to replace memory-card file: " + ec.message());
    }
    return Result<void>::success();
}

} // namespace

Ps1MemoryCard::Ps1MemoryCard() : data_(raw_size, 0u) {
    format_blank();
}

void Ps1MemoryCard::set_frame_checksum(
    std::vector<std::uint8_t>& data,
    std::size_t frame) noexcept {
    const auto offset = frame * sector_size;
    data[offset + 127u] = xor_checksum(data, offset, 127u);
}

void Ps1MemoryCard::format_blank() noexcept {
    std::fill(data_.begin(), data_.end(), 0u);

    data_[0u] = static_cast<std::uint8_t>('M');
    data_[1u] = static_cast<std::uint8_t>('C');
    set_frame_checksum(data_, 0u);

    for (std::size_t frame = 1u; frame <= 15u; ++frame) {
        const auto offset = frame * sector_size;
        data_[offset + 0u] = 0xA0u;
        data_[offset + 8u] = 0xFFu;
        data_[offset + 9u] = 0xFFu;
        set_frame_checksum(data_, frame);
    }

    for (std::size_t frame = 16u; frame <= 35u; ++frame) {
        const auto offset = frame * sector_size;
        data_[offset + 0u] = 0xFFu;
        data_[offset + 1u] = 0xFFu;
        data_[offset + 2u] = 0xFFu;
        data_[offset + 3u] = 0xFFu;
        set_frame_checksum(data_, frame);
    }

    for (std::size_t frame = 36u; frame <= 62u; ++frame) {
        const auto offset = frame * sector_size;
        std::fill_n(data_.begin() + static_cast<std::ptrdiff_t>(offset),
                    sector_size,
                    0xFFu);
    }

    const auto frame63 = 63u * sector_size;
    std::copy_n(data_.begin(), sector_size, data_.begin() + static_cast<std::ptrdiff_t>(frame63));

    flag_byte_ = 0x08u;
    dirty_ = false;
    refresh_content_hash();
}

void Ps1MemoryCard::refresh_content_hash() noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const auto value : data_) {
        hash ^= value;
        hash *= kFnvPrime;
    }
    content_hash_ = hash;
}

Result<Ps1MemoryCard> Ps1MemoryCard::load(
    const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<Ps1MemoryCard>::failure(
            ErrorCode::file_not_found,
            "memory-card file not found: " + path.string());
    }

    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
    if (bytes.size() != raw_size) {
        return Result<Ps1MemoryCard>::failure(
            ErrorCode::invalid_argument,
            "PS1 memory-card image must be exactly 128 KiB");
    }
    if (bytes[0u] != static_cast<std::uint8_t>('M') ||
        bytes[1u] != static_cast<std::uint8_t>('C')) {
        return Result<Ps1MemoryCard>::failure(
            ErrorCode::invalid_argument,
            "PS1 memory-card image is missing MC header");
    }

    Ps1MemoryCard card{};
    std::copy(bytes.begin(), bytes.end(), card.data_.begin());
    card.backing_path_ = path;
    card.flag_byte_ = 0x08u;
    card.dirty_ = false;
    card.refresh_content_hash();
    return Result<Ps1MemoryCard>::success(std::move(card));
}

Result<Ps1MemoryCard> Ps1MemoryCard::load_or_create(
    const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        return load(path);
    }
    if (ec) {
        return Result<Ps1MemoryCard>::failure(
            ErrorCode::io_error,
            "failed to inspect memory-card path: " + ec.message());
    }

    Ps1MemoryCard card{};
    card.backing_path_ = path;
    card.dirty_ = true;
    auto saved = card.flush();
    if (!saved) {
        return Result<Ps1MemoryCard>::failure(saved.error, saved.detail);
    }
    return Result<Ps1MemoryCard>::success(std::move(card));
}

std::optional<Ps1MemoryCard::Sector> Ps1MemoryCard::read_sector(
    std::uint32_t sector) const noexcept {
    if (sector >= sector_count) return std::nullopt;
    Sector result{};
    const auto offset = static_cast<std::size_t>(sector) * sector_size;
    std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset),
                sector_size,
                result.begin());
    return result;
}

bool Ps1MemoryCard::write_sector(
    std::uint32_t sector,
    const Sector& data) noexcept {
    if (sector >= sector_count) return false;
    const auto offset = static_cast<std::size_t>(sector) * sector_size;
    std::copy(data.begin(),
              data.end(),
              data_.begin() + static_cast<std::ptrdiff_t>(offset));
    flag_byte_ = 0u;
    dirty_ = true;
    refresh_content_hash();
    return true;
}

void Ps1MemoryCard::set_backing_path(std::filesystem::path path) {
    backing_path_ = std::move(path);
}

Result<void> Ps1MemoryCard::flush() {
    if (backing_path_.empty()) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "memory card has no backing path");
    }
    if (!dirty_ && std::filesystem::exists(backing_path_)) {
        return Result<void>::success();
    }

    std::error_code ec;
    if (backing_path_.has_parent_path()) {
        std::filesystem::create_directories(backing_path_.parent_path(), ec);
        if (ec) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed to create memory-card directory: " + ec.message());
        }
    }

    auto temp = backing_path_;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed to create temporary memory-card image");
        }
        out.write(
            reinterpret_cast<const char*>(data_.data()),
            static_cast<std::streamsize>(data_.size()));
        out.flush();
        if (!out) {
            return Result<void>::failure(
                ErrorCode::io_error,
                "failed while writing memory-card image");
        }
    }

    auto replaced = replace_file(temp, backing_path_);
    if (!replaced) return replaced;
    dirty_ = false;
    return Result<void>::success();
}

std::size_t Ps1MemoryCard::size() const noexcept {
    return data_.size();
}

std::uint8_t Ps1MemoryCard::byte(std::size_t offset) const noexcept {
    return offset < data_.size() ? data_[offset] : 0xFFu;
}

std::uint8_t Ps1MemoryCard::flag_byte() const noexcept {
    return flag_byte_;
}

bool Ps1MemoryCard::dirty() const noexcept {
    return dirty_;
}

bool Ps1MemoryCard::has_backing_path() const noexcept {
    return !backing_path_.empty();
}

std::uint64_t Ps1MemoryCard::content_hash() const noexcept {
    return content_hash_;
}

} // namespace jojo
