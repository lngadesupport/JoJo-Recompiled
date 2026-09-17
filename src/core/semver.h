#pragma once

#include "core/result.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace jojo {

struct SemanticVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};

    friend bool operator==(const SemanticVersion&, const SemanticVersion&) = default;
};

enum class VersionRequirementKind {
    any,
    exact,
    at_least,
    compatible_major,
};

struct VersionRequirement {
    VersionRequirementKind kind{VersionRequirementKind::any};
    SemanticVersion version{};

    friend bool operator==(const VersionRequirement&, const VersionRequirement&) = default;
};

[[nodiscard]] Result<SemanticVersion> parse_semver(std::string_view text);
[[nodiscard]] std::string to_string(const SemanticVersion& version);
[[nodiscard]] Result<VersionRequirement> parse_version_requirement(std::string_view text);
[[nodiscard]] bool matches(const VersionRequirement& requirement, const SemanticVersion& version) noexcept;
[[nodiscard]] int compare_semver(const SemanticVersion& lhs, const SemanticVersion& rhs) noexcept;

}
