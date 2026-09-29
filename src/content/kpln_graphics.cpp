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

Result<KplnElementTable> parse_kpln_element_table_0802(
    std::span<const std::uint8_t> bytes,
    std::size_t source_0801_size) {
    constexpr std::size_t kDescriptorBytes = 12u;
    constexpr std::uint32_t kSourceOffsetMask = 0x00FFFFFFu;

    if (bytes.size() < kDescriptorBytes ||
        bytes.size() % 4u != 0u) {
        return Result<KplnElementTable>::failure(
            ErrorCode::unsupported_format,
            "KPLN 0x0802 is not a whole dword stream");
    }

    const auto first_block_dword_offset =
        static_cast<std::uint32_t>(le16(bytes.data()));
    const auto first_block_byte_offset =
        static_cast<std::uint64_t>(
            first_block_dword_offset) * 4u;
    if (first_block_dword_offset == 0u ||
        first_block_byte_offset > bytes.size() ||
        first_block_byte_offset % kDescriptorBytes != 0u) {
        return Result<KplnElementTable>::failure(
            ErrorCode::invalid_installation,
            "KPLN 0x0802 first block offset does not terminate the descriptor table");
    }

    const auto descriptor_count =
        static_cast<std::size_t>(
            first_block_byte_offset / kDescriptorBytes);
    const auto total_dwords =
        static_cast<std::uint32_t>(bytes.size() / 4u);

    KplnElementTable table{};
    table.descriptor_count =
        static_cast<std::uint32_t>(descriptor_count);
    table.first_block_dword_offset =
        first_block_dword_offset;
    table.records.reserve(descriptor_count);

    for (std::size_t index = 0u;
         index < descriptor_count;
         ++index) {
        const auto offset = index * kDescriptorBytes;
        KplnElementRecord record{};
        record.block_dword_offset =
            le16(bytes.data() + offset + 0u);
        const auto packed_layout =
            le16(bytes.data() + offset + 2u);
        record.layout_width =
            static_cast<std::uint8_t>(
                packed_layout & 0x00FFu);
        record.layout_height =
            static_cast<std::uint8_t>(
                packed_layout >> 8u);
        record.raw_field2 =
            le16(bytes.data() + offset + 4u);
        record.raw_field3 =
            le16(bytes.data() + offset + 6u);
        record.signed_field2 =
            le16s(bytes.data() + offset + 4u);
        record.signed_field3 =
            le16s(bytes.data() + offset + 6u);
        record.payload_count =
            le16(bytes.data() + offset + 8u);
        const auto reserved =
            le16(bytes.data() + offset + 10u);

        if (reserved != 0u ||
            record.layout_width == 0u ||
            record.layout_height == 0u ||
            record.block_dword_offset <
                first_block_dword_offset) {
            return Result<KplnElementTable>::failure(
                ErrorCode::invalid_installation,
                "KPLN 0x0802 descriptor contains invalid retail fields");
        }

        const auto layout_cells =
            static_cast<std::uint32_t>(
                record.layout_width) *
            record.layout_height;
        const auto mask_dword_count =
            (layout_cells + 31u) / 32u;
        const auto block_end =
            static_cast<std::uint64_t>(
                record.block_dword_offset) +
            mask_dword_count +
            record.payload_count;
        if (block_end > total_dwords) {
            return Result<KplnElementTable>::failure(
                ErrorCode::invalid_installation,
                "KPLN 0x0802 element block extends past the chunk");
        }

        const auto block_byte_offset =
            static_cast<std::size_t>(
                record.block_dword_offset) * 4u;
        record.mask_dwords.reserve(mask_dword_count);
        for (std::uint32_t mask = 0u;
             mask < mask_dword_count;
             ++mask) {
            record.mask_dwords.push_back(
                le32(
                    bytes.data() +
                    block_byte_offset +
                    static_cast<std::size_t>(mask) * 4u));
        }

        const auto payload_byte_offset =
            block_byte_offset +
            static_cast<std::size_t>(
                mask_dword_count) * 4u;
        record.cells.reserve(record.payload_count);
        for (std::uint32_t cell = 0u;
             cell < record.payload_count;
             ++cell) {
            const auto packed_reference =
                le32(
                    bytes.data() +
                    payload_byte_offset +
                    static_cast<std::size_t>(cell) * 4u);
            KplnCellReference reference{};
            reference.source_offset =
                packed_reference & kSourceOffsetMask;
            reference.flags =
                static_cast<std::uint8_t>(
                    packed_reference >> 24u);
            if (reference.source_offset >=
                source_0801_size) {
                return Result<KplnElementTable>::failure(
                    ErrorCode::invalid_installation,
                    "KPLN 0x0802 cell reference is outside 0x0801");
            }
            record.cells.push_back(reference);
        }

        table.records.push_back(std::move(record));
    }

    return Result<KplnElementTable>::success(
        std::move(table));
}

} // namespace jojo::content
