#include "content/fighter_tk.h"

#include <limits>

namespace jojo::content {
namespace {

std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8u));
}

std::int16_t le16s(const std::uint8_t* p) noexcept {
    return static_cast<std::int16_t>(le16(p));
}

std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8u) |
        (static_cast<std::uint32_t>(p[2]) << 16u) |
        (static_cast<std::uint32_t>(p[3]) << 24u);
}

} // namespace

Result<FighterTkRoots> parse_fighter_tk_roots(
    std::span<const std::uint8_t> tkc,
    std::span<const std::uint8_t> tkd) {
    constexpr std::size_t kTkcHeaderWords =
        fighter_tk_slot_count + 1u;
    constexpr std::size_t kTkcHeaderBytes =
        kTkcHeaderWords * sizeof(std::uint32_t);
    constexpr std::size_t kTkdHeaderWords =
        1u + fighter_tk_slot_count;
    constexpr std::size_t kTkdHeaderBytes =
        kTkdHeaderWords * sizeof(std::uint32_t);

    if (tkc.size() < kTkcHeaderBytes ||
        tkd.size() < kTkdHeaderBytes) {
        return Result<FighterTkRoots>::failure(
            ErrorCode::unsupported_format,
            "fighter TKC/TKD pair is smaller than the retail headers");
    }
    if (tkc.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        tkd.size() >
            std::numeric_limits<std::uint32_t>::max()) {
        return Result<FighterTkRoots>::failure(
            ErrorCode::unsupported_format,
            "fighter TKC/TKD pair is too large");
    }

    FighterTkRoots roots{};
    roots.tkc_size = static_cast<std::uint32_t>(tkc.size());
    roots.tkd_size = static_cast<std::uint32_t>(tkd.size());

    const auto tkc_end_pointer =
        le32(tkc.data() + fighter_tk_slot_count * 4u);
    if (tkc_end_pointer < fighter_tkc_load_base) {
        return Result<FighterTkRoots>::failure(
            ErrorCode::invalid_installation,
            "TKC end pointer is below the retail load base");
    }
    roots.tkc_end_offset =
        tkc_end_pointer - fighter_tkc_load_base;
    // PL09 carries two bytes of trailing alignment after the logical end.
    if (roots.tkc_end_offset > tkc.size() ||
        tkc.size() - roots.tkc_end_offset > 3u) {
        return Result<FighterTkRoots>::failure(
            ErrorCode::invalid_installation,
            "TKC end pointer is outside the logical blob");
    }

    roots.tkd_block_size = le32(tkd.data());
    if (roots.tkd_block_size == 0u ||
        (roots.tkd_block_size & 7u) != 0u) {
        return Result<FighterTkRoots>::failure(
            ErrorCode::invalid_installation,
            "TKD block size is not a non-zero multiple of 8");
    }

    for (std::size_t slot = 0u;
         slot < fighter_tk_slot_count;
         ++slot) {
        const auto tkc_pointer =
            le32(tkc.data() + slot * 4u);
        const auto tkd_offset =
            le32(tkd.data() + (slot + 1u) * 4u);
        auto& output = roots.slots[slot];

        if (tkc_pointer < fighter_tkc_load_base) {
            return Result<FighterTkRoots>::failure(
                ErrorCode::invalid_installation,
                "TKC root pointer is below the retail load base");
        }
        output.tkc_offset =
            tkc_pointer - fighter_tkc_load_base;

        // Retail PL09 uses the physical one-past-file address for an empty
        // slot while the logical end pointer is two alignment bytes earlier.
        // Accept only that exact sentinel; all other out-of-range roots remain
        // invalid.
        if (output.tkc_offset == tkc.size()) {
            continue;
        }
        if (output.tkc_offset >= roots.tkc_end_offset ||
            output.tkc_offset > tkc.size()) {
            return Result<FighterTkRoots>::failure(
                ErrorCode::invalid_installation,
                "TKC slot root is outside the logical TKC content");
        }

        std::size_t cursor = output.tkc_offset;
        bool terminated = false;
        while (cursor + 4u <= roots.tkc_end_offset) {
            const auto leaf_pointer =
                le32(tkc.data() + cursor);
            cursor += 4u;
            if (leaf_pointer == 0xFFFFFFFFu) {
                terminated = true;
                break;
            }
            if (leaf_pointer < fighter_tkc_load_base) {
                return Result<FighterTkRoots>::failure(
                    ErrorCode::invalid_installation,
                    "TKC leaf pointer is below the retail load base");
            }
            const auto leaf_offset =
                leaf_pointer - fighter_tkc_load_base;
            if (leaf_offset > roots.tkc_end_offset ||
                roots.tkc_end_offset - leaf_offset < 10u) {
                return Result<FighterTkRoots>::failure(
                    ErrorCode::invalid_installation,
                    "TKC leaf record is outside the logical TKC content");
            }

            FighterTkcRecord record{};
            record.source_offset = leaf_offset;
            for (std::size_t field = 0u;
                 field < record.raw_words.size();
                 ++field) {
                record.raw_words[field] =
                    le16(
                        tkc.data() +
                        leaf_offset +
                        field * 2u);
            }
            if ((record.raw_words[0] & 0x00FFu) != 0x008Au) {
                return Result<FighterTkRoots>::failure(
                    ErrorCode::invalid_installation,
                    "TKC leaf record does not use retail opcode 0x8A");
            }
            record.opcode =
                static_cast<std::uint8_t>(
                    record.raw_words[0] & 0x00FFu);
            record.variant =
                static_cast<std::uint8_t>(
                    record.raw_words[0] >> 8u);
            record.reference_index =
                static_cast<std::uint16_t>(
                    record.raw_words[1] & 0x3FFFu);
            record.reference_flags =
                static_cast<std::uint16_t>(
                    record.raw_words[1] & 0xC000u);
            record.parameter2 = record.raw_words[2];
            record.parameter3 = record.raw_words[3];
            record.parameter4 = record.raw_words[4];
            output.tkc_records.push_back(record);
        }
        if (!terminated) {
            return Result<FighterTkRoots>::failure(
                ErrorCode::invalid_installation,
                "TKC slot pointer list is not terminated");
        }

        if ((tkd_offset & 3u) != 0u ||
            tkd_offset < kTkdHeaderBytes ||
            tkd_offset > tkd.size() ||
            roots.tkd_block_size >
                tkd.size() - tkd_offset) {
            return Result<FighterTkRoots>::failure(
                ErrorCode::invalid_installation,
                "TKD slot block is outside the TKD blob");
        }
        output.tkd_offset = tkd_offset;

        const auto record_count =
            roots.tkd_block_size / 8u;
        output.tkd_records.reserve(record_count);
        for (std::uint32_t record_index = 0u;
             record_index < record_count;
             ++record_index) {
            const auto record_offset =
                tkd_offset + record_index * 8u;
            FighterTkdRecord record{};
            record.source_offset = record_offset;
            record.offset_x =
                le16s(tkd.data() + record_offset + 0u);
            record.offset_y =
                le16s(tkd.data() + record_offset + 2u);
            record.packed_element =
                le16(tkd.data() + record_offset + 4u);
            record.element_index =
                static_cast<std::uint16_t>(
                    record.packed_element & 0x0FFFu);
            record.element_flags =
                static_cast<std::uint16_t>(
                    record.packed_element & 0xF000u);
            record.reserved =
                le16s(tkd.data() + record_offset + 6u);
            if (record.reserved != 0) {
                return Result<FighterTkRoots>::failure(
                    ErrorCode::invalid_installation,
                    "TKD reserved field is non-zero");
            }
            output.tkd_records.push_back(record);
        }
    }

    return Result<FighterTkRoots>::success(
        std::move(roots));
}

} // namespace jojo::content
