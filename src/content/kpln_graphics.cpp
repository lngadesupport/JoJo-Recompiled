#include "content/kpln_graphics.h"

namespace jojo::content {

Result<KplnIndexedPage4bpp>
parse_kpln_indexed_page_0202(
    std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kExpectedBytes =
        1024u * 256u / 2u;
    if (bytes.size() != kExpectedBytes) {
        return Result<KplnIndexedPage4bpp>::failure(
            ErrorCode::unsupported_format,
            "KPLN 0x0202 page is not the retail 1024x256 4bpp size");
    }

    KplnIndexedPage4bpp page{};
    page.indices.reserve(bytes.size() * 2u);
    for (const auto byte : bytes) {
        page.indices.push_back(
            static_cast<std::uint8_t>(byte & 0x0Fu));
        page.indices.push_back(
            static_cast<std::uint8_t>(byte >> 4u));
    }
    return Result<KplnIndexedPage4bpp>::success(
        std::move(page));
}

} // namespace jojo::content
