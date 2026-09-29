#include "content/kpln_clut.h"

#include <algorithm>

namespace jojo::content {
namespace {

std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8u));
}

bool write_words(
    KplnClutWindow& window,
    std::uint32_t vram_y,
    std::uint32_t x_word,
    std::span<const std::uint8_t> bytes) {
    if ((bytes.size() & 1u) != 0u ||
        vram_y < kpln_clut_base_y ||
        vram_y >= kpln_clut_base_y + kpln_clut_height) {
        return false;
    }

    const auto row =
        vram_y - kpln_clut_base_y;
    const auto word_count = bytes.size() / 2u;
    if (x_word > window.width ||
        word_count > window.width - x_word) {
        return false;
    }

    for (std::size_t i = 0u; i < word_count; ++i) {
        window.bgr555[
            static_cast<std::size_t>(row) *
                window.width +
            x_word + i] =
            le16(bytes.data() + i * 2u);
    }
    return true;
}

std::span<const std::uint8_t> fixed_slice(
    std::span<const std::uint8_t> bytes,
    std::size_t offset,
    std::size_t size) {
    if (offset >= bytes.size()) {
        return {};
    }
    const auto available =
        std::min(size, bytes.size() - offset);
    return bytes.subspan(offset, available);
}

} // namespace

std::uint16_t KplnClutWindow::at(
    std::uint32_t x_word,
    std::uint32_t vram_y) const noexcept {
    if (x_word >= width ||
        vram_y < kpln_clut_base_y ||
        vram_y >= kpln_clut_base_y + height) {
        return 0u;
    }
    return bgr555[
        static_cast<std::size_t>(
            vram_y - kpln_clut_base_y) *
            width +
        x_word];
}

Result<KplnClutWindows> build_kpln_clut_windows(
    std::span<const std::uint8_t> pool_0803,
    std::span<const std::uint8_t> pool_0804,
    std::span<const std::uint8_t> pool_0805,
    std::span<const std::uint8_t> pool_0806,
    std::span<const std::uint8_t> pool_0807) {
    const auto split_two_valid = [](
        std::span<const std::uint8_t> bytes) {
        // Retail KPLN CLUT pools are always two palette IDs. Most 0x0803
        // pools use 0x100 bytes per ID, while KPLN15 legitimately uses a
        // compact 0x40-byte fixed slice per ID.
        return bytes.size() >= 4u &&
            bytes.size() % 4u == 0u;
    };
    if (!split_two_valid(pool_0803)) {
        return Result<KplnClutWindows>::failure(
            ErrorCode::unsupported_format,
            "KPLN 0x0803 CLUT pool cannot split across two palette IDs");
    }

    const auto dynamic_valid = split_two_valid;
    if (!dynamic_valid(pool_0804) ||
        !dynamic_valid(pool_0805) ||
        !dynamic_valid(pool_0806)) {
        return Result<KplnClutWindows>::failure(
            ErrorCode::unsupported_format,
            "KPLN dynamic CLUT pools must split evenly across two palette IDs");
    }

    constexpr std::uint32_t palette_count = 2u;
    const auto stride_0803 = pool_0803.size() / palette_count;

    KplnClutWindows result{};
    result.palette_count = palette_count;
    result.windows.resize(palette_count);

    for (std::uint32_t palette_id = 0u;
         palette_id < palette_count;
         ++palette_id) {
        auto& window = result.windows[palette_id];
        window.bgr555.assign(
            static_cast<std::size_t>(window.width) *
                window.height,
            0u);

        const auto fixed_0803 =
            pool_0803.subspan(
                static_cast<std::size_t>(palette_id) *
                    stride_0803,
                stride_0803);
        const auto stride_0804 = pool_0804.size() / 2u;
        const auto stride_0805 = pool_0805.size() / 2u;
        const auto stride_0806 = pool_0806.size() / 2u;
        const auto dynamic_0804 =
            pool_0804.subspan(
                static_cast<std::size_t>(palette_id) *
                    stride_0804,
                stride_0804);
        const auto dynamic_0805 =
            pool_0805.subspan(
                static_cast<std::size_t>(palette_id) *
                    stride_0805,
                stride_0805);
        const auto dynamic_0806 =
            pool_0806.subspan(
                static_cast<std::size_t>(palette_id) *
                    stride_0806,
                stride_0806);

        for (std::uint32_t side = 0u; side < 2u; ++side) {
            if (!write_words(
                    window,
                    0x1e8u + side,
                    0u,
                    fixed_0803) ||
                !write_words(
                    window,
                    0x1efu + side,
                    0u,
                    dynamic_0804) ||
                !write_words(
                    window,
                    0x1e8u + side,
                    0x80u,
                    dynamic_0805) ||
                !write_words(
                    window,
                    0x1f1u + side,
                    0u,
                    dynamic_0806)) {
                return Result<KplnClutWindows>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN CLUT pool does not fit its retail VRAM window");
            }

            const auto fixed_y = 499u + side * 2u;
            const auto row0 = fixed_slice(
                pool_0807, 0u, 0x300u);
            const auto row1 = fixed_slice(
                pool_0807, 0x300u, 0x300u);

            // 0x0807 is a fixed two-row slab. Missing tail bytes are
            // implicitly zero, matching an untouched CLUT window.
            std::vector<std::uint8_t> padded0(0x300u, 0u);
            std::vector<std::uint8_t> padded1(0x300u, 0u);
            std::copy(row0.begin(), row0.end(), padded0.begin());
            std::copy(row1.begin(), row1.end(), padded1.begin());
            if (!write_words(window, fixed_y, 0u, padded0) ||
                !write_words(window, fixed_y + 1u, 0u, padded1)) {
                return Result<KplnClutWindows>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0807 slab does not fit its retail VRAM window");
            }
        }
    }

    return Result<KplnClutWindows>::success(
        std::move(result));
}

std::uint16_t sample_kpln_clut(
    const KplnClutWindow& window,
    std::uint32_t clut_row_base,
    std::uint32_t selector,
    std::uint32_t pixel_index) noexcept {
    const auto vram_y =
        clut_row_base + selector / 0x18u;
    const auto x_word =
        (selector % 0x18u) * 16u +
        pixel_index;
    return window.at(x_word, vram_y);
}

} // namespace jojo::content
