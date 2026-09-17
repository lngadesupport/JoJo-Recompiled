#pragma once

#include "core/revision.h"
#include "iso_fixture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace test_ps1 {

inline void write_le32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

inline std::vector<std::uint8_t> make_psx_exe() {
    constexpr std::uint32_t entry_pc = 0x80010000u;
    constexpr std::uint32_t initial_gp = 0x80018000u;
    constexpr std::uint32_t text_load_address = 0x80010000u;
    constexpr std::uint32_t text_size = 16u;
    constexpr std::uint32_t stack_base = 0x801FFF00u;
    constexpr std::uint32_t stack_size = 0x00000100u;

    std::vector<std::uint8_t> bytes(0x800u + text_size, 0u);
    constexpr std::string_view magic = "PS-X EXE";
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write_le32(bytes, 0x010, entry_pc);
    write_le32(bytes, 0x014, initial_gp);
    write_le32(bytes, 0x018, text_load_address);
    write_le32(bytes, 0x01C, text_size);
    write_le32(bytes, 0x030, stack_base);
    write_le32(bytes, 0x034, stack_size);

    const std::uint8_t payload[16] = {
        0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x08, 0x24,
        0x02, 0x00, 0x09, 0x24,
        0x21, 0x50, 0x09, 0x01,
    };
    std::copy(std::begin(payload), std::end(payload), bytes.begin() + 0x800);
    return bytes;
}

inline std::vector<std::uint8_t> make_psx_exe_from_words(
    const std::vector<std::uint32_t>& words,
    std::uint32_t entry_pc = 0x80010000u) {
    auto bytes = make_psx_exe();
    bytes.resize(0x800u + words.size() * 4u, 0u);
    write_le32(bytes, 0x010, entry_pc);
    write_le32(bytes, 0x018, entry_pc);
    write_le32(bytes, 0x01C, static_cast<std::uint32_t>(words.size() * 4u));
    for (std::size_t i = 0; i < words.size(); ++i) {
        write_le32(bytes, 0x800u + i * 4u, words[i]);
    }
    return bytes;
}

struct Ps1DiscFixture {
    std::string system_cnf;
    std::vector<std::uint8_t> executable;
    std::vector<std::uint8_t> asset;
    bool include_system_cnf{true};
};

inline Ps1DiscFixture make_disc_fixture() {
    Ps1DiscFixture fixture{};
    fixture.system_cnf =
        "BOOT = cdrom:\\SLUS_TEST.00;1\r\n"
        "TCB = 4\r\n"
        "EVENT = 10\r\n"
        "STACK = 801FFF00\r\n";
    fixture.executable = make_psx_exe();
    fixture.asset = {'A', 'S', 'S', 'E', 'T'};
    return fixture;
}

inline std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

inline std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

inline jojo::GameRevisionProfile make_revision_profile(
    const Ps1DiscFixture& fixture,
    std::string revision_id = "synthetic-ps1-jojo") {
    jojo::GameRevisionProfile profile{};
    profile.revision_id = std::move(revision_id);
    if (fixture.include_system_cnf) {
        const auto system = bytes_of(fixture.system_cnf);
        profile.files.push_back({"/SYSTEM.CNF", system.size(), fnv1a64(system)});
    }
    profile.files.push_back(
        {"/SLUS_TEST.00", fixture.executable.size(), fnv1a64(fixture.executable)});
    profile.files.push_back(
        {"/DATA/ASSET.DAT", fixture.asset.size(), fnv1a64(fixture.asset)});
    return profile;
}

inline std::filesystem::path write_cooked_iso(
    const std::filesystem::path& path,
    const Ps1DiscFixture& fixture) {
    constexpr std::size_t sector = test_iso::sector;
    constexpr std::uint32_t root_lba = 20u;
    constexpr std::uint32_t system_lba = 21u;
    constexpr std::uint32_t executable_lba = 22u;
    constexpr std::uint32_t data_dir_lba = 24u;
    constexpr std::uint32_t asset_lba = 25u;

    std::vector<std::uint8_t> image(28u * sector, 0u);

    const std::size_t pvd = 16u * sector;
    image[pvd] = 1u;
    std::copy_n("CD001", 5, image.begin() + static_cast<std::ptrdiff_t>(pvd + 1u));
    image[pvd + 6u] = 1u;
    test_iso::dir_record(image, pvd + 156u, root_lba,
                         static_cast<std::uint32_t>(sector), 2u, {}, 0);

    const std::size_t term = 17u * sector;
    image[term] = 255u;
    std::copy_n("CD001", 5, image.begin() + static_cast<std::ptrdiff_t>(term + 1u));
    image[term + 6u] = 1u;

    std::size_t root = root_lba * sector;
    root += test_iso::dir_record(image, root, root_lba,
                                 static_cast<std::uint32_t>(sector), 2u, {}, 0);
    root += test_iso::dir_record(image, root, root_lba,
                                 static_cast<std::uint32_t>(sector), 2u, {}, 1);
    if (fixture.include_system_cnf) {
        root += test_iso::dir_record(image, root, system_lba,
                                     static_cast<std::uint32_t>(fixture.system_cnf.size()),
                                     0u, "SYSTEM.CNF;1");
    }
    root += test_iso::dir_record(image, root, executable_lba,
                                 static_cast<std::uint32_t>(fixture.executable.size()),
                                 0u, "SLUS_TEST.00;1");
    root += test_iso::dir_record(image, root, data_dir_lba,
                                 static_cast<std::uint32_t>(sector), 2u, "DATA");

    if (fixture.include_system_cnf) {
        std::copy(fixture.system_cnf.begin(), fixture.system_cnf.end(),
                  image.begin() + static_cast<std::ptrdiff_t>(system_lba * sector));
    }
    std::copy(fixture.executable.begin(), fixture.executable.end(),
              image.begin() + static_cast<std::ptrdiff_t>(executable_lba * sector));

    std::size_t data = data_dir_lba * sector;
    data += test_iso::dir_record(image, data, data_dir_lba,
                                 static_cast<std::uint32_t>(sector), 2u, {}, 0);
    data += test_iso::dir_record(image, data, root_lba,
                                 static_cast<std::uint32_t>(sector), 2u, {}, 1);
    data += test_iso::dir_record(image, data, asset_lba,
                                 static_cast<std::uint32_t>(fixture.asset.size()),
                                 0u, "ASSET.DAT;1");
    std::copy(fixture.asset.begin(), fixture.asset.end(),
              image.begin() + static_cast<std::ptrdiff_t>(asset_lba * sector));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
    return path;
}

inline std::filesystem::path write_mode2_bin(
    const std::filesystem::path& cooked_path,
    const std::filesystem::path& raw_path,
    const Ps1DiscFixture& fixture) {
    write_cooked_iso(cooked_path, fixture);
    return test_iso::write_raw2352_from_iso(cooked_path, raw_path, 2u);
}

}
