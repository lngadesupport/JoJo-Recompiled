#include "core/semver.h"

#include <charconv>
#include <limits>

namespace jojo {
namespace {

Result<std::uint32_t> parse_component(std::string_view text) {
    if (text.empty()) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "empty semantic version component");
    }
    if (text.size() > 1 && text.front() == '0') {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "semantic version components must not contain leading zeroes");
    }

    std::uint64_t value{};
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || value > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_argument, "invalid semantic version component");
    }
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(value));
}

}

Result<SemanticVersion> parse_semver(std::string_view text) {
    const auto first = text.find('.');
    if (first == std::string_view::npos) {
        return Result<SemanticVersion>::failure(ErrorCode::invalid_argument, "semantic version must contain major.minor.patch");
    }
    const auto second = text.find('.', first + 1);
    if (second == std::string_view::npos || text.find('.', second + 1) != std::string_view::npos) {
        return Result<SemanticVersion>::failure(ErrorCode::invalid_argument, "semantic version must contain exactly three numeric components");
    }

    const auto major = parse_component(text.substr(0, first));
    if (!major) return Result<SemanticVersion>::failure(major.error, major.detail);
    const auto minor = parse_component(text.substr(first + 1, second - first - 1));
    if (!minor) return Result<SemanticVersion>::failure(minor.error, minor.detail);
    const auto patch = parse_component(text.substr(second + 1));
    if (!patch) return Result<SemanticVersion>::failure(patch.error, patch.detail);

    return Result<SemanticVersion>::success({major.value, minor.value, patch.value});
}

std::string to_string(const SemanticVersion& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

Result<VersionRequirement> parse_version_requirement(std::string_view text) {
    VersionRequirementKind kind = VersionRequirementKind::any;
    if (text.empty()) {
        return Result<VersionRequirement>::success({kind, {}});
    }

    if (text.starts_with(">=")) {
        kind = VersionRequirementKind::at_least;
        text.remove_prefix(2);
    } else if (text.starts_with("=")) {
        kind = VersionRequirementKind::exact;
        text.remove_prefix(1);
    } else if (text.starts_with("^")) {
        kind = VersionRequirementKind::compatible_major;
        text.remove_prefix(1);
    } else {
        return Result<VersionRequirement>::failure(ErrorCode::invalid_argument, "unsupported semantic version requirement operator");
    }

    const auto version = parse_semver(text);
    if (!version) return Result<VersionRequirement>::failure(version.error, version.detail);
    return Result<VersionRequirement>::success({kind, version.value});
}

int compare_semver(const SemanticVersion& lhs, const SemanticVersion& rhs) noexcept {
    if (lhs.major != rhs.major) return lhs.major < rhs.major ? -1 : 1;
    if (lhs.minor != rhs.minor) return lhs.minor < rhs.minor ? -1 : 1;
    if (lhs.patch != rhs.patch) return lhs.patch < rhs.patch ? -1 : 1;
    return 0;
}

bool matches(const VersionRequirement& requirement, const SemanticVersion& version) noexcept {
    switch (requirement.kind) {
        case VersionRequirementKind::any:
            return true;
        case VersionRequirementKind::exact:
            return compare_semver(version, requirement.version) == 0;
        case VersionRequirementKind::at_least:
            return compare_semver(version, requirement.version) >= 0;
        case VersionRequirementKind::compatible_major:
            return version.major == requirement.version.major && compare_semver(version, requirement.version) >= 0;
    }
    return false;
}

}
