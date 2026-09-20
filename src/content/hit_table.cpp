#include "content/hit_table.h"

namespace jojo::content {
namespace {

std::int16_t le16s(const std::uint8_t* p) noexcept {
    const auto value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8u));
    return static_cast<std::int16_t>(value);
}

} // namespace

Result<HitTable> parse_hit_table(
    std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kRecordBytes = 8u;
    constexpr std::size_t kRecordCount = 512u;
    constexpr std::size_t kExpectedBytes =
        kRecordBytes * kRecordCount;

    if (bytes.size() != kExpectedBytes) {
        return Result<HitTable>::failure(
            ErrorCode::unsupported_format,
            "HIT table must be exactly 4096 bytes");
    }

    HitTable table{};
    for (std::size_t index = 0u;
         index < kRecordCount;
         ++index) {
        const auto* p =
            bytes.data() + index * kRecordBytes;
        auto& record = table.records[index];
        record.a = le16s(p + 0u);
        record.b = le16s(p + 2u);
        record.c = le16s(p + 4u);
        record.d = le16s(p + 6u);
        if (!record.empty()) {
            ++table.nonzero_records;
        }
    }

    return Result<HitTable>::success(table);
}

} // namespace jojo::content
