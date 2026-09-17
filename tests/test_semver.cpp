#include "core/semver.h"

#include <iostream>
#include <string_view>

namespace {
int failures = 0;
#define CHECK(...) do { if (!(static_cast<bool>(__VA_ARGS__))) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)
}

int main() {
    using namespace jojo;

    const auto v = parse_semver("1.2.3");
    CHECK(v);
    if (v) CHECK(v.value == SemanticVersion{1, 2, 3});

    CHECK(!parse_semver("1.2"));
    CHECK(!parse_semver("1.2.3.4"));
    CHECK(!parse_semver("1.2.-3"));
    CHECK(!parse_semver("1.2.3-beta"));
    CHECK(!parse_semver("4294967296.0.0"));

    const auto any = parse_version_requirement("");
    CHECK(any);
    if (any) {
        CHECK(matches(any.value, SemanticVersion{0, 0, 1}));
        CHECK(matches(any.value, SemanticVersion{99, 8, 7}));
    }

    const auto exact = parse_version_requirement("=1.2.3");
    CHECK(exact);
    if (exact) {
        CHECK(matches(exact.value, SemanticVersion{1, 2, 3}));
        CHECK(!matches(exact.value, SemanticVersion{1, 2, 4}));
    }

    const auto at_least = parse_version_requirement(">=1.2.3");
    CHECK(at_least);
    if (at_least) {
        CHECK(matches(at_least.value, SemanticVersion{1, 2, 3}));
        CHECK(matches(at_least.value, SemanticVersion{1, 9, 0}));
        CHECK(matches(at_least.value, SemanticVersion{2, 0, 0}));
        CHECK(!matches(at_least.value, SemanticVersion{1, 2, 2}));
    }

    const auto compatible = parse_version_requirement("^1.2.3");
    CHECK(compatible);
    if (compatible) {
        CHECK(matches(compatible.value, SemanticVersion{1, 2, 3}));
        CHECK(matches(compatible.value, SemanticVersion{1, 8, 0}));
        CHECK(!matches(compatible.value, SemanticVersion{1, 2, 2}));
        CHECK(!matches(compatible.value, SemanticVersion{2, 0, 0}));
    }

    CHECK(!parse_version_requirement("~1.2.3"));
    CHECK(!parse_version_requirement(">1.2.3"));
    CHECK(!parse_version_requirement("^1.2"));

    if (failures != 0) {
        std::cerr << failures << " semver test(s) failed\n";
        return 1;
    }
    std::cout << "semver tests passed\n";
    return 0;
}
