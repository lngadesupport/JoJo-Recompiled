#include "core/mod_runtime.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(...) do { if (!(static_cast<bool>(__VA_ARGS__))) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string data_manifest(std::string id, std::string version = "1.0.0", bool gameplay = false) {
    return "id=" + id + "\n"
           "name=Test Mod\n"
           "version=" + version + "\n"
           "api_version=1.0.0\n"
           "kind=data\n"
           "gameplay=" + std::string(gameplay ? "true" : "false") + "\n";
}

jojo::DiscoveredMod make_mod(
    std::string id,
    jojo::SemanticVersion version = {1, 0, 0},
    std::vector<jojo::ModDependency> dependencies = {},
    std::vector<std::string> conflicts = {}) {
    jojo::ModManifest manifest{};
    manifest.id = std::move(id);
    manifest.name = manifest.id;
    manifest.version = version;
    manifest.api_version = jojo::kModApiVersion;
    manifest.kind = jojo::ModKind::data;
    manifest.dependencies = std::move(dependencies);
    manifest.conflicts = std::move(conflicts);
    return {std::move(manifest), std::filesystem::path("mods")};
}

jojo::ModDependency dep(std::string id, std::string requirement = {}) {
    auto parsed = jojo::parse_version_requirement(requirement);
    if (!parsed) {
        ++failures;
        return {};
    }
    return {std::move(id), parsed.value};
}
}

int main() {
    using namespace jojo;

    const auto parsed = parse_mod_manifest(
        "# comment\n"
        "id=example.ui\n"
        "name=Example UI\n"
        "version=1.2.3\n"
        "api_version=1.0.0\n"
        "kind=data\n"
        "gameplay=false\n"
        "depends=base.assets@>=1.0.0, shared.lib@^2.1.0\n"
        "conflicts=legacy.ui, broken.ui\n",
        std::filesystem::path("mods/example"));
    CHECK(parsed);
    if (parsed) {
        CHECK(parsed.value.id == "example.ui");
        CHECK(parsed.value.name == "Example UI");
        CHECK(parsed.value.version == SemanticVersion{1, 2, 3});
        CHECK(parsed.value.api_version == SemanticVersion{1, 0, 0});
        CHECK(parsed.value.kind == ModKind::data);
        CHECK(!parsed.value.gameplay);
        CHECK(parsed.value.entry.empty());
        CHECK(parsed.value.dependencies.size() == 2u);
        CHECK(parsed.value.dependencies[0].id == "base.assets");
        CHECK(parsed.value.dependencies[0].requirement.kind == VersionRequirementKind::at_least);
        CHECK(parsed.value.dependencies[1].requirement.kind == VersionRequirementKind::compatible_major);
        CHECK(parsed.value.conflicts.size() == 2u);
    }

    const auto native = parse_mod_manifest(
        "id=native.sample\n"
        "name=Native Sample\n"
        "version=3.4.5\n"
        "api_version=1.0.0\n"
        "kind=native\n"
        "gameplay=1\n"
        "entry=bin/sample_mod.dll\n",
        std::filesystem::path("mods/native"));
    CHECK(native);
    if (native) {
        CHECK(native.value.kind == ModKind::native);
        CHECK(native.value.gameplay);
        CHECK(native.value.entry.generic_string() == "bin/sample_mod.dll");
    }

    CHECK(!parse_mod_manifest(data_manifest("Upper.Case"), "mods/bad"));
    CHECK(!parse_mod_manifest(data_manifest("-bad"), "mods/bad"));
    CHECK(!parse_mod_manifest(data_manifest("bad id"), "mods/bad"));
    CHECK(!parse_mod_manifest(data_manifest("bad.unknown") + "mystery=1\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=native.noentry\nname=X\nversion=1.0.0\napi_version=1.0.0\nkind=native\ngameplay=0\n", "mods/bad"));
    CHECK(!parse_mod_manifest(data_manifest("bad.entry") + "entry=plugin.dll\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=native.escape\nname=X\nversion=1.0.0\napi_version=1.0.0\nkind=native\ngameplay=0\nentry=../evil.dll\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=native.normalized-escape\nname=X\nversion=1.0.0\napi_version=1.0.0\nkind=native\ngameplay=0\nentry=bin/../evil.dll\n",
        "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=future.api\nname=X\nversion=1.0.0\napi_version=2.0.0\nkind=data\ngameplay=0\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=future.minor\nname=X\nversion=1.0.0\napi_version=1.1.0\nkind=data\ngameplay=0\n", "mods/bad"));

    const auto root = std::filesystem::temp_directory_path() / "jojo_mod_discovery_tests";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto symlink_catalog_root = std::filesystem::temp_directory_path() / "jojo_mod_symlink_catalog_tests";
    const auto symlink_target_root = std::filesystem::temp_directory_path() / "jojo_mod_symlink_catalog_target";
    std::filesystem::remove_all(symlink_catalog_root, ec);
    std::filesystem::remove_all(symlink_target_root, ec);

    const auto symlink_manifest_root = std::filesystem::temp_directory_path() / "jojo_mod_symlink_manifest_tests";
    const auto symlink_manifest_target = std::filesystem::temp_directory_path() / "jojo_mod_symlink_manifest_target.ini";
    std::filesystem::remove_all(symlink_manifest_root, ec);
    std::filesystem::remove(symlink_manifest_target, ec);

    const auto dangling_manifest_root = std::filesystem::temp_directory_path() / "jojo_mod_dangling_manifest_tests";
    std::filesystem::remove_all(dangling_manifest_root, ec);
    std::filesystem::create_directories(dangling_manifest_root / "linked", ec);
    std::error_code dangling_symlink_ec;
    std::filesystem::create_symlink(
        dangling_manifest_root / "missing.ini",
        dangling_manifest_root / "linked" / "mod.ini",
        dangling_symlink_ec);
    if (!dangling_symlink_ec) {
        const auto dangling_manifest_catalog = discover_mods(dangling_manifest_root);
        CHECK(!dangling_manifest_catalog);
        if (!dangling_manifest_catalog) {
            CHECK(dangling_manifest_catalog.detail.find("symlink") != std::string::npos);
        }
    }
    std::filesystem::remove_all(dangling_manifest_root, ec);
    std::filesystem::create_directories(symlink_manifest_root / "linked", ec);
    write_text(symlink_manifest_target, data_manifest("linked.manifest"));
    std::error_code manifest_symlink_ec;
    std::filesystem::create_symlink(
        symlink_manifest_target,
        symlink_manifest_root / "linked" / "mod.ini",
        manifest_symlink_ec);
    if (!manifest_symlink_ec) {
        const auto linked_manifest_catalog = discover_mods(symlink_manifest_root);
        CHECK(!linked_manifest_catalog);
        if (!linked_manifest_catalog) {
            CHECK(linked_manifest_catalog.detail.find("symlink") != std::string::npos);
        }
    }
    std::filesystem::remove_all(symlink_manifest_root, ec);
    std::filesystem::remove(symlink_manifest_target, ec);
    std::filesystem::create_directories(symlink_catalog_root, ec);
    write_text(symlink_target_root / "mod.ini", data_manifest("linked.mod"));
    std::error_code catalog_symlink_ec;
    std::filesystem::create_directory_symlink(
        symlink_target_root,
        symlink_catalog_root / "linked",
        catalog_symlink_ec);
    if (!catalog_symlink_ec) {
        const auto linked_catalog = discover_mods(symlink_catalog_root);
        CHECK(!linked_catalog);
        if (!linked_catalog) CHECK(linked_catalog.detail.find("symlink") != std::string::npos);
    }
    std::filesystem::remove_all(symlink_catalog_root, ec);
    std::filesystem::remove_all(symlink_target_root, ec);
    write_text(root / "zeta" / "mod.ini", data_manifest("zeta.mod"));
    write_text(root / "alpha" / "mod.ini", data_manifest("alpha.mod"));
    std::filesystem::create_directories(root / "ignored-no-manifest");

    const auto catalog = discover_mods(root);
    CHECK(catalog);
    if (catalog) {
        CHECK(catalog.value.size() == 2u);
        CHECK(catalog.value[0].manifest.id == "alpha.mod");
        CHECK(catalog.value[1].manifest.id == "zeta.mod");
    }

    write_text(root / "duplicate" / "mod.ini", data_manifest("alpha.mod", "2.0.0"));
    const auto duplicate = discover_mods(root);
    CHECK(!duplicate);
    if (!duplicate) {
        CHECK(duplicate.detail.find("alpha.mod") != std::string::npos);
        CHECK(duplicate.detail.find("duplicate") != std::string::npos);
    }
    std::filesystem::remove_all(root, ec);

    ModCatalog graph{
        make_mod("addon.mod", {1, 0, 0}, {dep("base.mod", ">=1.0.0")}),
        make_mod("alpha.mod"),
        make_mod("base.mod", {1, 2, 0}),
        make_mod("zeta.mod"),
    };
    const std::vector<std::string> requested{"addon.mod", "zeta.mod", "alpha.mod"};
    const auto resolved = resolve_mod_set(graph, requested);
    CHECK(resolved);
    if (resolved) {
        CHECK(resolved.value.load_order.size() == 4u);
        if (resolved.value.load_order.size() == 4u) {
            CHECK(resolved.value.load_order[0]->manifest.id == "alpha.mod");
            CHECK(resolved.value.load_order[1]->manifest.id == "base.mod");
            CHECK(resolved.value.load_order[2]->manifest.id == "addon.mod");
            CHECK(resolved.value.load_order[3]->manifest.id == "zeta.mod");
        }
    }

    ModCatalog missing{make_mod("needs.missing", {1, 0, 0}, {dep("absent.mod")})};
    const std::vector<std::string> missing_request{"needs.missing"};
    const auto missing_result = resolve_mod_set(missing, missing_request);
    CHECK(!missing_result);
    if (!missing_result) CHECK(missing_result.detail.find("absent.mod") != std::string::npos);

    ModCatalog bad_version{
        make_mod("needs.new", {1, 0, 0}, {dep("base.mod", ">=2.0.0")}),
        make_mod("base.mod", {1, 9, 0}),
    };
    const std::vector<std::string> version_request{"needs.new"};
    const auto version_result = resolve_mod_set(bad_version, version_request);
    CHECK(!version_result);
    if (!version_result) CHECK(version_result.detail.find("base.mod") != std::string::npos);

    ModCatalog cycle{
        make_mod("cycle.a", {1, 0, 0}, {dep("cycle.b")}),
        make_mod("cycle.b", {1, 0, 0}, {dep("cycle.a")}),
    };
    const std::vector<std::string> cycle_request{"cycle.a"};
    const auto cycle_result = resolve_mod_set(cycle, cycle_request);
    CHECK(!cycle_result);
    if (!cycle_result) CHECK(cycle_result.detail.find("cycle") != std::string::npos);

    ModCatalog conflict{
        make_mod("conflict.a", {1, 0, 0}, {}, {"conflict.b"}),
        make_mod("conflict.b"),
    };
    const std::vector<std::string> conflict_request{"conflict.a", "conflict.b"};
    const auto conflict_result = resolve_mod_set(conflict, conflict_request);
    CHECK(!conflict_result);
    if (!conflict_result) CHECK(conflict_result.detail.find("conflict") != std::string::npos);

    const std::vector<std::string> unknown_request{"does.not.exist"};
    CHECK(!resolve_mod_set(graph, unknown_request));

    ModCatalog incompatible_api{make_mod("future.api")};
    incompatible_api[0].manifest.api_version = {2, 0, 0};
    const std::vector<std::string> incompatible_api_request{"future.api"};
    const auto incompatible_api_result = resolve_mod_set(incompatible_api, incompatible_api_request);
    CHECK(!incompatible_api_result);
    if (!incompatible_api_result) CHECK(incompatible_api_result.detail.find("API") != std::string::npos);

    const auto content_root = std::filesystem::temp_directory_path() / "jojo_mod_content_tests";
    std::filesystem::remove_all(content_root, ec);
    write_text(content_root / "alpha" / "mod.ini", data_manifest("alpha.mod"));
    write_text(content_root / "alpha" / "data" / "ui" / "menu.txt", "alpha-menu");
    write_text(content_root / "alpha" / "data" / "only-alpha.txt", "alpha-only");
    write_text(content_root / "gameplay" / "mod.ini", data_manifest("gameplay.mod", "1.0.0", true));
    write_text(content_root / "gameplay" / "data" / "rules.bin", "rules-v1");
    write_text(content_root / "zeta" / "mod.ini", data_manifest("zeta.mod"));
    write_text(content_root / "zeta" / "data" / "ui" / "menu.txt", "zeta-menu");

    const auto content_catalog = discover_mods(content_root);
    CHECK(content_catalog);
    std::vector<std::string> content_requested{"zeta.mod", "gameplay.mod", "alpha.mod"};
    Result<ResolvedModSet> content_resolved{};
    if (content_catalog) content_resolved = resolve_mod_set(content_catalog.value, content_requested);
    CHECK(content_resolved);
    if (content_resolved) {
        const auto overlay = build_mod_overlay(content_resolved.value);
        CHECK(overlay);
        if (overlay) {
            CHECK(overlay.value.files.size() == 3u);
            CHECK(overlay.value.files.at("ui/menu.txt").mod_id == "zeta.mod");
            CHECK(overlay.value.files.at("ui/menu.txt").host_path == content_root / "zeta" / "data" / "ui" / "menu.txt");
            CHECK(overlay.value.collisions.size() == 1u);
            if (!overlay.value.collisions.empty()) {
                CHECK(overlay.value.collisions[0].logical_path == "ui/menu.txt");
                CHECK(overlay.value.collisions[0].previous_mod_id == "alpha.mod");
                CHECK(overlay.value.collisions[0].replacing_mod_id == "zeta.mod");
            }
        }

        const auto hashes1 = compute_mod_set_hashes(content_resolved.value);
        CHECK(hashes1);
        if (hashes1) {
            CHECK(hashes1.value.mod_set_hash.size() == 64u);
            CHECK(hashes1.value.gameplay_hash.size() == 64u);

            write_text(content_root / "zeta" / "data" / "ui" / "menu.txt", "zeta-menu-v2");
            const auto hashes2 = compute_mod_set_hashes(content_resolved.value);
            CHECK(hashes2);
            if (hashes2) {
                CHECK(hashes2.value.mod_set_hash != hashes1.value.mod_set_hash);
                CHECK(hashes2.value.gameplay_hash == hashes1.value.gameplay_hash);

                write_text(content_root / "gameplay" / "data" / "rules.bin", "rules-v2");
                const auto hashes3 = compute_mod_set_hashes(content_resolved.value);
                CHECK(hashes3);
                if (hashes3) CHECK(hashes3.value.gameplay_hash != hashes2.value.gameplay_hash);
            }
        }
    }

    const auto copy_root = content_root / "alpha-copy";
    write_text(copy_root / "data" / "only-alpha.txt", "alpha-only");
    write_text(copy_root / "data" / "ui" / "menu.txt", "alpha-menu");
    write_text(copy_root / "mod.ini", data_manifest("alpha.mod"));
    if (content_catalog && !content_catalog.value.empty()) {
        const auto* alpha = [&]() -> const DiscoveredMod* {
            for (const auto& mod : content_catalog.value) if (mod.manifest.id == "alpha.mod") return &mod;
            return nullptr;
        }();
        CHECK(alpha != nullptr);
        if (alpha) {
            DiscoveredMod copy{alpha->manifest, copy_root};
            const auto original_hash = compute_mod_content_hash(*alpha);
            const auto copy_hash = compute_mod_content_hash(copy);
            CHECK(original_hash);
            CHECK(copy_hash);
            if (original_hash && copy_hash) CHECK(original_hash.value == copy_hash.value);

            std::error_code symlink_ec;
            std::filesystem::create_symlink(copy_root / "data" / "only-alpha.txt", copy_root / "data" / "link.txt", symlink_ec);
            if (!symlink_ec) {
                CHECK(!compute_mod_content_hash(copy));
                std::filesystem::remove(copy_root / "data" / "link.txt", symlink_ec);
            }
        }
    }

    std::filesystem::remove_all(content_root, ec);

    if (failures != 0) {
        std::cerr << failures << " mod runtime test(s) failed\n";
        return 1;
    }
    std::cout << "mod runtime tests passed\n";
    return 0;
}
