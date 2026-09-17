#include "core/mod_runtime.h"

#include <iostream>

namespace {
int failures = 0;
#define CHECK(...) do { if (!(static_cast<bool>(__VA_ARGS__))) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)

jojo::DiscoveredMod make_policy_mod(std::string id, bool gameplay) {
    jojo::ModManifest manifest{};
    manifest.id = std::move(id);
    manifest.name = manifest.id;
    manifest.version = {1, 0, 0};
    manifest.api_version = jojo::kModApiVersion;
    manifest.kind = jojo::ModKind::data;
    manifest.gameplay = gameplay;
    return {std::move(manifest), {}};
}
}

int main() {
    using namespace jojo;

    auto cosmetic = make_policy_mod("cosmetic.ui", false);
    auto gameplay = make_policy_mod("gameplay.rules", true);

    ResolvedModSet cosmetic_only{};
    cosmetic_only.load_order = {&cosmetic};
    ResolvedModSet mixed{};
    mixed.load_order = {&cosmetic, &gameplay};

    const ModSetHashes cosmetic_hashes{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"};
    const ModSetHashes mixed_hashes{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                                    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};

    CHECK(validate_mod_session(cosmetic_only, cosmetic_hashes, {ModSessionMode::offline, {}}));
    CHECK(validate_mod_session(cosmetic_only, cosmetic_hashes, {ModSessionMode::ranked, {}}));

    const auto ranked_gameplay = validate_mod_session(mixed, mixed_hashes, {ModSessionMode::ranked, {}});
    CHECK(!ranked_gameplay);
    if (!ranked_gameplay) CHECK(ranked_gameplay.detail.find("gameplay.rules") != std::string::npos);

    CHECK(validate_mod_session(mixed, mixed_hashes, {ModSessionMode::custom, {}}));
    CHECK(validate_mod_session(mixed, mixed_hashes, {ModSessionMode::custom, mixed_hashes.mod_set_hash}));

    const auto mismatch = validate_mod_session(
        mixed,
        mixed_hashes,
        {ModSessionMode::custom, "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"});
    CHECK(!mismatch);
    if (!mismatch) CHECK(mismatch.detail.find("hash") != std::string::npos);

    if (failures != 0) {
        std::cerr << failures << " mod policy test(s) failed\n";
        return 1;
    }
    std::cout << "mod policy tests passed\n";
    return 0;
}
