#pragma once

#include "core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace jojo {

class Ps1MemoryCard {
public:
    static constexpr std::size_t raw_size = 128u * 1024u;
    static constexpr std::size_t sector_size = 128u;
    static constexpr std::size_t sector_count = raw_size / sector_size;
    using Sector = std::array<std::uint8_t, sector_size>;

    Ps1MemoryCard();

    [[nodiscard]] static Result<Ps1MemoryCard> load(
        const std::filesystem::path& path);
    [[nodiscard]] static Result<Ps1MemoryCard> load_or_create(
        const std::filesystem::path& path);

    [[nodiscard]] std::optional<Sector> read_sector(
        std::uint32_t sector) const noexcept;
    [[nodiscard]] bool write_sector(
        std::uint32_t sector,
        const Sector& data) noexcept;

    void set_backing_path(std::filesystem::path path);
    [[nodiscard]] Result<void> flush();

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint8_t byte(std::size_t offset) const noexcept;
    [[nodiscard]] std::uint8_t flag_byte() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] bool has_backing_path() const noexcept;
    [[nodiscard]] std::uint64_t content_hash() const noexcept;

private:
    void format_blank() noexcept;
    void refresh_content_hash() noexcept;
    static void set_frame_checksum(
        std::vector<std::uint8_t>& data,
        std::size_t frame) noexcept;

    std::vector<std::uint8_t> data_;
    std::filesystem::path backing_path_{};
    std::uint8_t flag_byte_{0x08u};
    bool dirty_{};
    std::uint64_t content_hash_{};
};

} // namespace jojo
