#include "core/device_id.h"
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace jojo {
std::string make_xinput_device_id(unsigned index) {
    return "xinput:" + std::to_string(index);
}

std::string make_hid_device_id(std::string_view interface_path) {
    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    for (const char ch : interface_path) {
        const auto lower = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(ch)));
        hash ^= lower;
        hash *= prime;
    }
    std::ostringstream out;
    out << "hid:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}
}
