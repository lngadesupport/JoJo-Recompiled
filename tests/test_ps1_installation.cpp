#include "core/ps1_installation.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static fs::path temp_root(std::string_view suffix) {
    auto root = fs::temp_directory_path() /
        (std::string("jojo-ps1-installation-") + std::string(suffix));
    std::error_code ec;
    fs::remove_all(root, ec);
    return root;
}

static void write_manifest(const fs::path& staging_dir) {
    std::ofstream out(staging_dir / "game_manifest.ini", std::ios::trunc);
    out << "manifest_version=2\n";
}

static void test_destination_validation() {
    CHECK(!jojo::validate_install_destination({}, 0));

    const auto file_root = temp_root("file-root");
    {
        std::ofstream out(file_root, std::ios::trunc);
        out << "not-a-directory";
    }
    CHECK(!jojo::validate_install_destination(file_root, 0));

    const auto directory_root = temp_root("directory-root");
    std::error_code ec;
    fs::create_directories(directory_root, ec);
    CHECK(!ec);
    CHECK(jojo::validate_install_destination(directory_root, 0));

    ec.clear();
    const auto space = fs::space(directory_root, ec);
    if (!ec && space.available < std::numeric_limits<std::uint64_t>::max()) {
        CHECK(!jojo::validate_install_destination(directory_root, space.available + 1u));
    }

    fs::remove(file_root, ec);
    fs::remove_all(directory_root, ec);
}

static void test_transactional_generations_and_active_pointer() {
    const auto root = temp_root("generations");
    std::error_code ec;
    fs::create_directories(root, ec);
    CHECK(!ec);

    const auto first = jojo::begin_install_generation(root);
    CHECK(first);
    if (!first) {
        fs::remove_all(root, ec);
        return;
    }
    CHECK(first.value.generation_id == "generation-000001");
    CHECK(first.value.staging_dir == root / ".staging" / "generation-000001");
    CHECK(first.value.final_dir == root / "generations" / "generation-000001");
    CHECK(fs::is_directory(first.value.staging_dir));

    write_manifest(first.value.staging_dir);
    CHECK(jojo::commit_install_generation(root, first.value));

    const auto active_after_first = jojo::resolve_active_install_generation(root);
    CHECK(active_after_first);
    if (active_after_first) {
        CHECK(active_after_first.value.install_root == root);
        CHECK(active_after_first.value.generation_id == "generation-000001");
        CHECK(active_after_first.value.generation_dir ==
              root / "generations" / "generation-000001");
        CHECK(active_after_first.value.manifest_path ==
              root / "generations" / "generation-000001" / "game_manifest.ini");
    }

    const auto second = jojo::begin_install_generation(root);
    CHECK(second);
    if (second) {
        CHECK(second.value.generation_id == "generation-000002");
        CHECK(second.value.staging_dir == root / ".staging" / "generation-000002");
        CHECK(second.value.final_dir == root / "generations" / "generation-000002");

        const auto still_first = jojo::resolve_active_install_generation(root);
        CHECK(still_first);
        if (still_first) CHECK(still_first.value.generation_id == "generation-000001");
    }

    {
        std::ofstream out(root / "active_install.ini", std::ios::trunc);
        out << "format=1\n";
        out << "generation_id=generation-000001\n";
        out << "manifest=../escape\n";
    }
    CHECK(!jojo::resolve_active_install_generation(root));

    fs::remove_all(root, ec);
}

static void test_commit_requires_staging_manifest() {
    const auto root = temp_root("missing-manifest");
    std::error_code ec;
    fs::create_directories(root, ec);
    CHECK(!ec);

    const auto generation = jojo::begin_install_generation(root);
    CHECK(generation);
    if (generation) {
        CHECK(!jojo::commit_install_generation(root, generation.value));
        CHECK(!fs::exists(root / "active_install.ini"));
    }
    fs::remove_all(root, ec);
}

int main() {
    test_destination_validation();
    test_transactional_generations_and_active_pointer();
    test_commit_requires_staging_manifest();
    if (failures) {
        std::cerr << failures << " PS1 installation assertion(s) failed\n";
        return 1;
    }
    std::cout << "all PS1 installation assertions passed\n";
    return 0;
}
