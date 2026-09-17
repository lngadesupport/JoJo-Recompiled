#include "core/ps1_system_cnf.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace jojo {
namespace {

std::string trim_ascii(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return std::string(value.substr(first, last - first));
}

bool ascii_iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lhs = static_cast<unsigned char>(a[i]);
        const auto rhs = static_cast<unsigned char>(b[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) return false;
    }
    return true;
}

bool ascii_istarts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && ascii_iequals(value.substr(0, prefix.size()), prefix);
}

Result<Ps1SystemCnf> malformed(std::string detail) {
    return Result<Ps1SystemCnf>::failure(ErrorCode::unsupported_format, std::move(detail));
}

Result<std::string> normalize_cdrom_target(std::string value) {
    if (!ascii_istarts_with(value, "cdrom:")) {
        return Result<std::string>::failure(ErrorCode::unsupported_format,
                                            "SYSTEM.CNF BOOT target is not a cdrom path");
    }
    value.erase(0, 6);
    if (value.empty() || (value.front() != '\\' && value.front() != '/')) {
        return Result<std::string>::failure(ErrorCode::unsupported_format,
                                            "SYSTEM.CNF BOOT target is missing a cdrom path separator");
    }
    while (!value.empty() && (value.front() == '\\' || value.front() == '/')) value.erase(value.begin());
    std::replace(value.begin(), value.end(), '\\', '/');
    if (value.size() >= 2 && value[value.size() - 2] == ';' && value.back() == '1') {
        value.resize(value.size() - 2);
    }
    if (value.empty() || value.find(':') != std::string::npos) {
        return Result<std::string>::failure(ErrorCode::unsupported_format,
                                            "SYSTEM.CNF BOOT target is not a valid ISO9660 path");
    }

    std::size_t start = 0;
    while (start <= value.size()) {
        const auto slash = value.find('/', start);
        const auto end = slash == std::string::npos ? value.size() : slash;
        const auto component = std::string_view(value).substr(start, end - start);
        if (component.empty() || component == "." || component == "..") {
            return Result<std::string>::failure(ErrorCode::unsupported_format,
                                                "SYSTEM.CNF BOOT target contains an unsafe path component");
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return Result<std::string>::success("/" + value);
}

}

Result<Ps1SystemCnf> parse_ps1_system_cnf(std::string_view text) {
    std::string boot_value;
    bool found_boot = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto newline = text.find('\n', start);
        const auto end = newline == std::string_view::npos ? text.size() : newline;
        auto line = trim_ascii(text.substr(start, end - start));
        if (!line.empty()) {
            const auto eq = line.find('=');
            if (eq != std::string::npos) {
                const auto key = trim_ascii(std::string_view(line).substr(0, eq));
                if (ascii_iequals(key, "BOOT")) {
                    if (found_boot) return malformed("SYSTEM.CNF contains multiple BOOT declarations");
                    found_boot = true;
                    boot_value = trim_ascii(std::string_view(line).substr(eq + 1));
                }
            }
        }
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
    if (!found_boot || boot_value.empty()) {
        return malformed("SYSTEM.CNF does not contain exactly one usable BOOT declaration");
    }
    auto normalized = normalize_cdrom_target(std::move(boot_value));
    if (!normalized) return Result<Ps1SystemCnf>::failure(normalized.error, normalized.detail);
    return Result<Ps1SystemCnf>::success(Ps1SystemCnf{std::move(normalized.value)});
}

Result<Ps1SystemCnf> read_ps1_system_cnf(const Iso9660Image& image) {
    auto bytes = read_iso9660_file(image, "/SYSTEM.CNF");
    if (!bytes) return Result<Ps1SystemCnf>::failure(bytes.error, bytes.detail);
    const std::string text(bytes.value.begin(), bytes.value.end());
    return parse_ps1_system_cnf(text);
}

}
