#include "content/kpln_graphics.h"

namespace jojo::content {
namespace {

std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8u));
}

} // namespace

Result<KplnGroupTable> parse_kpln_group_table_0800(
    std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kRecordWords = 6u;
    constexpr std::size_t kRecordBytes =
        kRecordWords * sizeof(std::uint16_t);

    if (bytes.size() < kRecordBytes ||
        (bytes.size() & 1u) != 0u) {
        return Result<KplnGroupTable>::failure(
            ErrorCode::unsupported_format,
            "KPLN 0x0800 data is not a valid 16-bit table");
    }

    const auto first_list_word =
        static_cast<std::uint32_t>(le16(bytes.data()));
    if (first_list_word == 0u ||
        first_list_word % kRecordWords != 0u) {
        return Result<KplnGroupTable>::failure(
            ErrorCode::invalid_installation,
            "KPLN 0x0800 first list offset does not terminate a 6-word record table");
    }

    const auto total_words =
        static_cast<std::uint32_t>(bytes.size() / 2u);
    if (first_list_word > total_words) {
        return Result<KplnGroupTable>::failure(
            ErrorCode::invalid_installation,
            "KPLN 0x0800 record table extends past the chunk");
    }

    KplnGroupTable table{};
    table.table_word_count = first_list_word;
    const auto record_count =
        static_cast<std::size_t>(
            first_list_word / kRecordWords);
    table.records.reserve(record_count);

    for (std::size_t record_index = 0u;
         record_index < record_count;
         ++record_index) {
        const auto offset =
            record_index * kRecordBytes;
        KplnGroupRecord record{};
        record.list_word_offset =
            le16(bytes.data() + offset + 0u);
        record.packed_layout =
            le16(bytes.data() + offset + 2u);
        record.layout_low =
            static_cast<std::uint8_t>(
                record.packed_layout & 0x00FFu);
        record.layout_high =
            static_cast<std::uint8_t>(
                record.packed_layout >> 8u);
        record.field2 =
            le16(bytes.data() + offset + 4u);
        record.field3 =
            le16(bytes.data() + offset + 6u);
        record.field4 =
            le16(bytes.data() + offset + 8u);
        const auto reserved =
            le16(bytes.data() + offset + 10u);

        if (reserved != 0u) {
            return Result<KplnGroupTable>::failure(
                ErrorCode::invalid_installation,
                "KPLN 0x0800 reserved field is non-zero");
        }
        if (record.list_word_offset < first_list_word ||
            record.list_word_offset >= total_words) {
            return Result<KplnGroupTable>::failure(
                ErrorCode::invalid_installation,
                "KPLN 0x0800 list offset is outside the list region");
        }

        auto cursor = record.list_word_offset;
        bool terminated = false;
        while (cursor < total_words) {
            const auto value =
                le16(bytes.data() +
                    static_cast<std::size_t>(cursor) * 2u);
            ++cursor;
            if (value == 0xFFFFu) {
                terminated = true;
                break;
            }
            record.indices.push_back(value);
        }

        // The retail PL16 data has one final one-word list that consumes the
        // final word of the chunk without an explicit 0xFFFF terminator.
        if (!terminated && cursor != total_words) {
            return Result<KplnGroupTable>::failure(
                ErrorCode::invalid_installation,
                "KPLN 0x0800 index list is unterminated");
        }

        table.records.push_back(std::move(record));
    }

    return Result<KplnGroupTable>::success(std::move(table));
}

Result<KplnPaletteBank> parse_kpln_palette_bank(
    std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kPaletteBytes =
        16u * sizeof(std::uint16_t);
    if (bytes.empty() ||
        bytes.size() % kPaletteBytes != 0u) {
        return Result<KplnPaletteBank>::failure(
            ErrorCode::unsupported_format,
            "KPLN palette bank is not a whole number of 16-color BGR555 palettes");
    }

    KplnPaletteBank bank{};
    const auto count =
        bytes.size() / kPaletteBytes;
    bank.palettes.resize(count);
    for (std::size_t palette = 0u;
         palette < count;
         ++palette) {
        for (std::size_t color = 0u;
             color < 16u;
             ++color) {
            bank.palettes[palette].bgr555[color] =
                le16(
                    bytes.data() +
                    palette * kPaletteBytes +
                    color * 2u);
        }
    }
    return Result<KplnPaletteBank>::success(std::move(bank));
}

Result<KplnIndexedPage4bpp> parse_kpln_indexed_page_0202(
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
    return Result<KplnIndexedPage4bpp>::success(std::move(page));
}

} // namespace jojo::content
