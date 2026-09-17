#include "core/ps1_disc_session.h"
#include "ps1_fixture.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
int failures = 0;

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
    return condition;
}

jojo::Ps1DiscOpenOptions options_for(const test_ps1::Ps1DiscFixture& fixture) {
    jojo::Ps1DiscOpenOptions options{};
    options.revision_profiles.push_back(test_ps1::make_revision_profile(fixture));
    return options;
}

void verify_streaming_contract(const fs::path& source,
                               const test_ps1::Ps1DiscFixture& fixture,
                               const char* label) {
    const auto source_size_before = fs::file_size(source);
    const auto opened = jojo::Ps1DiscSession::open(source, options_for(fixture));
    if (!check(static_cast<bool>(opened), label)) return;

    const auto one = opened.value.read_sectors(16u, 1u);
    check(static_cast<bool>(one), "one-sector streaming read succeeds");
    if (one) {
        check(one.value.size() == 2048u, "one-sector read returns exactly 2048 logical bytes");
        check(one.value.size() >= 7u && one.value[0] == 1u &&
                  one.value[1] == 'C' && one.value[2] == 'D' && one.value[3] == '0' &&
                  one.value[4] == '0' && one.value[5] == '1' && one.value[6] == 1u,
              "one-sector read exposes the ISO9660 primary volume descriptor payload");
    }

    const auto multiple = opened.value.read_sectors(16u, 2u);
    check(static_cast<bool>(multiple), "multi-sector streaming read succeeds");
    if (multiple) {
        check(multiple.value.size() == 4096u,
              "two-sector read returns exactly two logical 2048-byte payloads");
        check(multiple.value[0] == 1u && multiple.value[2048u] == 255u,
              "multi-sector read preserves logical sector ordering");
    }

    const auto asset_sector = opened.value.read_sectors(25u, 1u);
    check(static_cast<bool>(asset_sector), "asset sector can be read on demand");
    if (asset_sector) {
        check(asset_sector.value.size() == 2048u, "asset sector has logical sector size");
        check(asset_sector.value.size() >= fixture.asset.size() &&
                  std::equal(fixture.asset.begin(), fixture.asset.end(), asset_sector.value.begin()),
              "asset sector starts with the exact fixture payload");
    }

    const auto zero = opened.value.read_sectors(0u, 0u);
    check(static_cast<bool>(zero), "zero-sector read is a valid no-op");
    if (zero) check(zero.value.empty(), "zero-sector read returns an empty buffer");

    const auto beyond_end = opened.value.read_sectors(28u, 1u);
    check(!beyond_end, "read beginning at end-of-track is rejected");
    if (!beyond_end) {
        check(beyond_end.error == jojo::ErrorCode::invalid_argument,
              "out-of-range sector read reports invalid_argument");
    }

    const auto crossing_end = opened.value.read_sectors(27u, 2u);
    check(!crossing_end, "read crossing end-of-track is rejected before partial output");
    if (!crossing_end) {
        check(crossing_end.error == jojo::ErrorCode::invalid_argument,
              "crossing read reports invalid_argument");
        check(crossing_end.value.empty(), "rejected crossing read does not expose a partial buffer");
    }

    check(fs::file_size(source) == source_size_before,
          "streaming reads never modify or resize the original media file");
}

} // namespace

int main() {
    const auto root = fs::temp_directory_path() / "jojo-ps1-disc-streaming";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    const auto fixture = test_ps1::make_disc_fixture();
    const auto cooked = root / "jojo.iso";
    test_ps1::write_cooked_iso(cooked, fixture);
    verify_streaming_contract(cooked, fixture, "cooked ISO opens for streaming contract");

    const auto cooked_seed = root / "seed.iso";
    const auto raw = root / "jojo.bin";
    test_ps1::write_mode2_bin(cooked_seed, raw, fixture);
    verify_streaming_contract(raw, fixture, "MODE2/2352 BIN opens for streaming contract");

    fs::remove_all(root, ec);
    if (failures) {
        std::cerr << failures << " PS1 disc-streaming assertion(s) failed\n";
        return 1;
    }
    std::cout << "PS1 disc-streaming assertions passed\n";
    return 0;
}
