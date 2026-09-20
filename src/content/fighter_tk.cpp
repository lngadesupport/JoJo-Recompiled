#include "content/fighter_tk.h"

#include <limits>

namespace jojo::content {
namespace {

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
    constexpr std::size_t kRootBytes =
        fighter_tk_slot_count * sizeof(std::uint32_t);

    if (tkc.size() < kRootBytes ||
        tkd.size() < kRootBytes) {
        return Result<FighterTkRoots>::failure(
            ErrorCode::unsupported_format,
            "fighter TKC/TKD pair is smaller than 27 root entries");
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

    for (std::size_t slot = 0u;
         slot < fighter_tk_slot_count;
         ++slot) {
        const auto tkc_pointer =
            le32(tkc.data() + slot * 4u);
        const auto tkd_value =
            le32(tkd.data() + slot * 4u);

        auto& output = roots.slots[slot];
        output.tkd_value = tkd_value;

        if (tkc_pointer == 0xFFFFFFFFu) {
            output.tkc_null = true;
        } else {
            if (tkc_pointer < fighter_tkc_load_base) {
                return Result<FighterTkRoots>::failure(
                    ErrorCode::invalid_installation,
                    "TKC root pointer is below the retail load base");
            }
            const auto offset =
                tkc_pointer - fighter_tkc_load_base;
            // Retail data uses the one-past-end address for some empty roots.
            if (offset > tkc.size()) {
                return Result<FighterTkRoots>::failure(
                    ErrorCode::invalid_installation,
                    "TKC root pointer is outside the TKC blob");
            }
            output.tkc_offset = offset;
        }

        // TKD roots are stored as raw offsets/values. In the retail set every
        // root is 4-byte aligned and falls inside the file. Keep the name
        // tkd_value until its exact gameplay semantics are proven.
        if ((tkd_value & 3u) != 0u ||
            tkd_value >= tkd.size()) {
            return Result<FighterTkRoots>::failure(
                ErrorCode::invalid_installation,
                "TKD root value is not an aligned in-file reference");
        }
    }

    return Result<FighterTkRoots>::success(roots);
}

} // namespace jojo::content
