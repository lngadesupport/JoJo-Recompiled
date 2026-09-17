#include "core/sha256.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
#define CHECK(...) do { if (!(static_cast<bool>(__VA_ARGS__))) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)
}

int main() {
    using namespace jojo;

    const std::vector<std::uint8_t> empty;
    CHECK(sha256_hex(sha256(empty)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
    CHECK(sha256_hex(sha256(abc)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    Sha256Hasher incremental;
    const std::vector<std::uint8_t> a{'a'};
    const std::vector<std::uint8_t> bc{'b', 'c'};
    CHECK(incremental.update(a));
    CHECK(incremental.update(bc));
    const auto incremental_digest = incremental.finalize();
    CHECK(incremental_digest);
    if (incremental_digest) CHECK(incremental_digest.value == sha256(abc));
    CHECK(!incremental.update(a));
    CHECK(!incremental.finalize());

    const std::string long_text = "The quick brown fox jumps over the lazy dog";
    const std::vector<std::uint8_t> quick(long_text.begin(), long_text.end());
    CHECK(sha256_hex(sha256(quick)) == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    const auto root = std::filesystem::temp_directory_path() / "jojo_sha256_tests";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);
    const auto file = root / "payload.bin";
    {
        std::ofstream out(file, std::ios::binary);
        out.write(long_text.data(), static_cast<std::streamsize>(long_text.size()));
    }
    const auto file_hash = sha256_file(file);
    CHECK(file_hash);
    if (file_hash) CHECK(file_hash.value == sha256(quick));

    const auto missing = sha256_file(root / "missing.bin");
    CHECK(!missing);

    std::filesystem::remove_all(root, ec);

    if (failures != 0) {
        std::cerr << failures << " sha256 test(s) failed\n";
        return 1;
    }
    std::cout << "sha256 tests passed\n";
    return 0;
}
