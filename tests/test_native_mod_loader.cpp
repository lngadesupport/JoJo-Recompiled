#include "core/native_mod_loader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;
#define CHECK(...) do { if (!(static_cast<bool>(__VA_ARGS__))) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)

bool set_callback_log(const std::filesystem::path& path) {
#if defined(_WIN32)
    return _putenv_s("JOJO_NATIVE_MOD_TEST_LOG", path.string().c_str()) == 0;
#else
    return setenv("JOJO_NATIVE_MOD_TEST_LOG", path.string().c_str(), 1) == 0;
#endif
}

void truncate_file(const std::filesystem::path& path) {
    std::ofstream(path, std::ios::trunc);
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(std::move(line));
    return lines;
}

std::filesystem::path install_fixture(
    const std::filesystem::path& source,
    const std::filesystem::path& mod_root,
    std::string_view stem) {
    std::error_code ec;
    std::filesystem::create_directories(mod_root / "bin", ec);
    if (ec) {
        std::cerr << "failed to create fixture directory: " << ec.message() << '\n';
        ++failures;
        return {};
    }

    const auto filename = std::string(stem) + source.extension().string();
    const auto destination = mod_root / "bin" / filename;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "failed to install fixture " << source << ": " << ec.message() << '\n';
        ++failures;
        return {};
    }
    return std::filesystem::path("bin") / filename;
}

jojo::DiscoveredMod make_native_mod(
    std::string id,
    const std::filesystem::path& root,
    const std::filesystem::path& entry) {
    jojo::ModManifest manifest{};
    manifest.id = std::move(id);
    manifest.name = manifest.id;
    manifest.version = {1, 0, 0};
    manifest.api_version = jojo::kModApiVersion;
    manifest.kind = jojo::ModKind::native;
    manifest.entry = entry;
    return {std::move(manifest), root};
}

}

int main(int argc, char** argv) {
    using namespace jojo;

    if (argc != 6) {
        std::cerr << "expected five native fixture paths\n";
        return 2;
    }

    const auto root = std::filesystem::temp_directory_path() / "jojo_native_mod_loader_tests";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ec.clear();
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);

    const auto first_root = root / "first";
    const auto second_root = root / "second";
    const auto failure_root = root / "failure";
    const auto bad_abi_root = root / "bad-abi";
    const auto no_export_root = root / "no-export";

    const auto first_entry = install_fixture(argv[1], first_root, "first");
    const auto second_entry = install_fixture(argv[2], second_root, "second");
    const auto failure_entry = install_fixture(argv[3], failure_root, "failure");
    const auto bad_abi_entry = install_fixture(argv[4], bad_abi_root, "bad-abi");
    const auto no_export_entry = install_fixture(argv[5], no_export_root, "no-export");

    auto first = make_native_mod("native.first", first_root, first_entry);
    auto second = make_native_mod("native.second", second_root, second_entry);
    auto load_failure = make_native_mod("native.failure", failure_root, failure_entry);
    auto bad_abi = make_native_mod("native.badabi", bad_abi_root, bad_abi_entry);
    auto no_export = make_native_mod("native.noexport", no_export_root, no_export_entry);

    const auto log_path = root / "callbacks.log";
    CHECK(set_callback_log(log_path));

    ResolvedModSet first_only{};
    first_only.load_order = {&first};
    const auto disabled = load_native_mods(first_only, {});
    CHECK(!disabled);
    if (!disabled) CHECK(disabled.detail.find("native.first") != std::string::npos);

    truncate_file(log_path);
    {
        auto loaded = load_native_mods(first_only, {true});
        CHECK(loaded);
        if (loaded) {
            CHECK(loaded.value.size() == 1u);
            CHECK(loaded.value.loaded_mod_ids() == std::vector<std::string>{"native.first"});
            NativeModSession moved = std::move(loaded.value);
            CHECK(loaded.value.empty());
            CHECK(moved.size() == 1u);
        }
    }
    CHECK(read_lines(log_path) == std::vector<std::string>({"load:native.first", "unload:native.first"}));

    auto wrong_id = make_native_mod("native.wrong", first_root, first_entry);
    ResolvedModSet wrong_id_set{};
    wrong_id_set.load_order = {&wrong_id};
    const auto mismatched_id = load_native_mods(wrong_id_set, {true});
    CHECK(!mismatched_id);
    if (!mismatched_id) CHECK(mismatched_id.detail.find("mod id") != std::string::npos);

    ResolvedModSet bad_abi_set{};
    bad_abi_set.load_order = {&bad_abi};
    const auto incompatible_abi = load_native_mods(bad_abi_set, {true});
    CHECK(!incompatible_abi);
    if (!incompatible_abi) CHECK(incompatible_abi.detail.find("ABI") != std::string::npos);

    ResolvedModSet no_export_set{};
    no_export_set.load_order = {&no_export};
    const auto missing_export = load_native_mods(no_export_set, {true});
    CHECK(!missing_export);
    if (!missing_export) CHECK(missing_export.detail.find("jojo_get_native_mod_v1") != std::string::npos);

    truncate_file(log_path);
    ResolvedModSet partial_set{};
    partial_set.load_order = {&first, &load_failure};
    const auto partial = load_native_mods(partial_set, {true});
    CHECK(!partial);
    if (!partial) {
        CHECK(partial.detail.find("native.failure") != std::string::npos);
        CHECK(partial.detail.find("on_load") != std::string::npos);
    }
    CHECK(read_lines(log_path) == std::vector<std::string>({
        "load:native.first",
        "load:native.failure",
        "unload:native.first",
    }));

    truncate_file(log_path);
    {
        ResolvedModSet two_mods{};
        two_mods.load_order = {&first, &second};
        const auto loaded = load_native_mods(two_mods, {true});
        CHECK(loaded);
        if (loaded) CHECK(loaded.value.size() == 2u);
    }
    CHECK(read_lines(log_path) == std::vector<std::string>({
        "load:native.first",
        "load:native.second",
        "unload:native.second",
        "unload:native.first",
    }));

    auto unsafe = make_native_mod("native.unsafe", first_root, "../outside.dll");
    ResolvedModSet unsafe_set{};
    unsafe_set.load_order = {&unsafe};
    const auto unsafe_result = load_native_mods(unsafe_set, {true});
    CHECK(!unsafe_result);
    if (!unsafe_result) CHECK(unsafe_result.detail.find("unsafe") != std::string::npos);

    std::filesystem::remove_all(root, ec);

    if (failures != 0) {
        std::cerr << failures << " native mod loader test(s) failed\n";
        return 1;
    }
    std::cout << "native mod loader tests passed\n";
    return 0;
}
