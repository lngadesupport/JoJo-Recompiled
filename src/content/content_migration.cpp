#include "content/content_migration.h"
#include "content/fighter_overlay.h"
#include "content/fighter_tk.h"
#include "content/hit_table.h"
#include "content/pac_archive.h"
#include "content/tim_image.h"
#include "content/xa_audio.h"

#include "core/disc_media.h"
#include "core/iso9660.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace jojo::content {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::string ascii_upper(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
        value.substr(0u, prefix.size()) == prefix;
}

bool ends_with(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
        value.substr(value.size() - suffix.size()) == suffix;
}

std::string json_escape(std::string_view value) {
    std::ostringstream out;
    for (const auto ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20u) {
                    out << "\\u"
                        << std::hex << std::setw(4)
                        << std::setfill('0')
                        << static_cast<unsigned>(
                            static_cast<unsigned char>(ch))
                        << std::dec;
                } else {
                    out << ch;
                }
                break;
        }
    }
    return out.str();
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) noexcept {
    auto hash = kFnvOffset;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

std::string path_relative_to(
    const std::filesystem::path& path,
    const std::filesystem::path& root);

std::filesystem::path raw_output_path(
    const std::filesystem::path& root,
    std::string_view iso_path) {
    std::string relative{iso_path};
    while (!relative.empty() && relative.front() == '/') {
        relative.erase(relative.begin());
    }
    return root / "raw" / std::filesystem::path{relative};
}

Result<void> write_bytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "cannot create content output directory: " + ec.message());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "cannot create content output file: " + path.string());
    }
    if (!bytes.empty()) {
        out.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!out) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "failed while writing content output file: " + path.string());
    }
    return Result<void>::success();
}

Result<std::vector<std::uint8_t>> read_raw_xa_sectors(
    const LogicalSectorSource& source,
    const DiscFileEntry& entry) {
    if (source.physical_sector_size != xa_raw_sector_size) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::unsupported_format,
            "complete XA migration requires a raw 2352-byte BIN/CUE source");
    }

    const auto sector_count =
        static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(entry.size_bytes) +
             2047u) /
            2048u);
    if (sector_count == 0u) {
        return Result<std::vector<std::uint8_t>>::success({});
    }
    if (static_cast<std::uint64_t>(entry.extent_lba) +
            sector_count >
        source.logical_sector_count) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::invalid_installation,
            "XA extent runs past the source track");
    }

    std::ifstream in(source.file_path, std::ios::binary);
    if (!in) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error,
            "cannot open raw XA source track");
    }

    std::vector<std::uint8_t> raw(
        static_cast<std::size_t>(sector_count) *
        xa_raw_sector_size);
    const auto physical =
        source.file_offset +
        static_cast<std::uint64_t>(entry.extent_lba) *
            source.physical_sector_size;
    in.seekg(static_cast<std::streamoff>(physical));
    if (!in) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error,
            "cannot seek XA extent");
    }
    in.read(
        reinterpret_cast<char*>(raw.data()),
        static_cast<std::streamsize>(raw.size()));
    if (in.gcount() !=
        static_cast<std::streamsize>(raw.size())) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::io_error,
            "short read while loading raw XA sectors");
    }
    return Result<std::vector<std::uint8_t>>::success(
        std::move(raw));
}

Result<std::filesystem::path> write_xa_audio(
    const std::filesystem::path& output_root,
    const LogicalSectorSource& source,
    const DiscFileEntry& entry) {
    const auto raw =
        read_raw_xa_sectors(source, entry);
    if (!raw) {
        return Result<std::filesystem::path>::failure(
            raw.error, raw.detail);
    }
    const auto parsed =
        parse_xa_audio_sectors(raw.value);
    if (!parsed) {
        return Result<std::filesystem::path>::failure(
            parsed.error, parsed.detail);
    }

    const auto stem =
        std::filesystem::path{entry.name}.stem().string();
    const auto directory =
        output_root / "derived" / "xa" / stem;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create XA output directory: " + ec.message());
    }

    const auto index_path = directory / "stream.json";
    std::ofstream index(index_path, std::ios::trunc);
    if (!index) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create XA stream manifest");
    }

    index << "{\n"
          << "  \"source\": \"" << json_escape(entry.path) << "\",\n"
          << "  \"source_representation\": \"raw_mode2_form2\",\n"
          << "  \"runtime_representation\": \"xa_adpcm_payload_only\",\n"
          << "  \"total_source_sectors\": "
          << parsed.value.total_sectors << ",\n"
          << "  \"audio_sectors\": "
          << parsed.value.audio_sectors << ",\n"
          << "  \"skipped_non_audio_sectors\": "
          << parsed.value.skipped_non_audio_sectors << ",\n"
          << "  \"streams\": [\n";

    for (std::size_t stream_index = 0u;
         stream_index < parsed.value.streams.size();
         ++stream_index) {
        const auto& stream =
            parsed.value.streams[stream_index];
        std::ostringstream filename;
        filename << "channel_"
                 << std::setfill('0')
                 << std::setw(2)
                 << static_cast<unsigned>(stream.channel)
                 << ".xaadpcm";
        const auto payload_path =
            directory / filename.str();
        const auto written =
            write_bytes(payload_path, stream.adpcm_payload);
        if (!written) {
            return Result<std::filesystem::path>::failure(
                written.error, written.detail);
        }

        index << "    {"
              << "\"channel\":"
              << static_cast<unsigned>(stream.channel)
              << ",\"coding\":"
              << static_cast<unsigned>(stream.coding)
              << ",\"sample_rate_hz\":"
              << stream.sample_rate_hz
              << ",\"channels\":"
              << stream.channel_count
              << ",\"sector_count\":"
              << stream.packets.size()
              << ",\"payload\":\""
              << json_escape(filename.str())
              << "\",\"eof_sector_indices\":[";
        bool first_eof = true;
        for (const auto& packet : stream.packets) {
            if (!packet.end_of_file) continue;
            if (!first_eof) index << ",";
            first_eof = false;
            index << packet.source_sector_index;
        }
        index << "]}";
        if (stream_index + 1u !=
            parsed.value.streams.size()) {
            index << ",";
        }
        index << "\n";
    }
    index << "  ]\n}\n";

    if (!index) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing XA stream manifest");
    }
    return Result<std::filesystem::path>::success(index_path);
}

Result<void> write_rgba_tga(
    const std::filesystem::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint32_t>& rgba8) {
    if (width == 0u || height == 0u ||
        rgba8.size() !=
            static_cast<std::size_t>(width) * height ||
        width > 0xFFFFu || height > 0xFFFFu) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "invalid RGBA image dimensions");
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "cannot create image output directory: " + ec.message());
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "cannot create TGA output: " + path.string());
    }

    std::array<std::uint8_t, 18> header{};
    header[2] = 2u;
    header[12] = static_cast<std::uint8_t>(width);
    header[13] = static_cast<std::uint8_t>(width >> 8u);
    header[14] = static_cast<std::uint8_t>(height);
    header[15] = static_cast<std::uint8_t>(height >> 8u);
    header[16] = 32u;
    header[17] = 0x28u; // top-left origin + 8 alpha bits
    out.write(
        reinterpret_cast<const char*>(header.data()),
        static_cast<std::streamsize>(header.size()));

    for (const auto rgba : rgba8) {
        const std::array<std::uint8_t, 4> bgra{
            static_cast<std::uint8_t>(rgba >> 16u),
            static_cast<std::uint8_t>(rgba >> 8u),
            static_cast<std::uint8_t>(rgba),
            static_cast<std::uint8_t>(rgba >> 24u),
        };
        out.write(
            reinterpret_cast<const char*>(bgra.data()),
            static_cast<std::streamsize>(bgra.size()));
    }

    if (!out) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "failed while writing TGA output: " + path.string());
    }
    return Result<void>::success();
}

std::uint8_t expand5(std::uint16_t value) noexcept {
    value &= 0x1Fu;
    return static_cast<std::uint8_t>(
        (value << 3u) | (value >> 2u));
}

Result<std::filesystem::path> write_pac_split(
    const std::filesystem::path& output_root,
    const DiscFileEntry& entry,
    const std::vector<std::uint8_t>& bytes) {
    const auto parsed = parse_pac_archive(bytes);
    if (!parsed) {
        return Result<std::filesystem::path>::failure(
            parsed.error, parsed.detail);
    }

    const auto stem =
        std::filesystem::path{entry.name}.stem().string();
    const auto directory =
        output_root / "derived" / "pac" / stem;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create PAC output directory: " + ec.message());
    }

    const auto index_path = directory / "pack.json";
    std::ofstream index(index_path, std::ios::trunc);
    if (!index) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create PAC chunk manifest");
    }

    index << "{\n"
          << "  \"source\": \"" << json_escape(entry.path) << "\",\n"
          << "  \"chunks\": [\n";

    for (std::size_t i = 0u; i < parsed.value.size(); ++i) {
        const auto& chunk = parsed.value[i];
        std::ostringstream filename;
        filename << std::setfill('0')
                 << std::setw(3) << i
                 << "_type_"
                 << std::hex << std::setw(4)
                 << chunk.type << std::dec
                 << ".bin";
        const auto chunk_path = directory / filename.str();
        const auto written = write_bytes(chunk_path, chunk.bytes);
        if (!written) {
            return Result<std::filesystem::path>::failure(
                written.error, written.detail);
        }

        const auto tim_images =
            decode_sector_aligned_tim_images(chunk.bytes);
        if (!tim_images) {
            return Result<std::filesystem::path>::failure(
                tim_images.error,
                entry.path + " chunk " + std::to_string(i) +
                    ": " + tim_images.detail);
        }

        index << "    {\"index\":" << i
              << ",\"type\":\"0x"
              << std::hex << std::setw(4)
              << std::setfill('0') << chunk.type << std::dec
              << "\",\"size\":" << chunk.bytes.size()
              << ",\"file\":\""
              << json_escape(filename.str()) << "\"";

        if (!tim_images.value.empty()) {
            index << ",\"tim_images\":[";
            const auto tim_directory =
                output_root / "derived" / "tim" / stem;
            for (std::size_t tim_index = 0u;
                 tim_index < tim_images.value.size();
                 ++tim_index) {
                const auto& image = tim_images.value[tim_index];
                std::ostringstream tim_name;
                tim_name << std::setfill('0')
                         << std::setw(3) << i
                         << "_type_"
                         << std::hex << std::setw(4)
                         << chunk.type << std::dec
                         << "_off_" << image.source_offset
                         << "_p" << image.palette_index
                         << ".tga";
                const auto tim_path =
                    tim_directory / tim_name.str();
                const auto image_written =
                    write_rgba_tga(
                        tim_path,
                        image.width,
                        image.height,
                        image.rgba8);
                if (!image_written) {
                    return Result<std::filesystem::path>::failure(
                        image_written.error,
                        image_written.detail);
                }

                if (tim_index != 0u) index << ",";
                index << "{"
                      << "\"path\":\""
                      << json_escape(
                          path_relative_to(
                              tim_path, output_root))
                      << "\",\"source_offset\":"
                      << image.source_offset
                      << ",\"flags\":" << image.flags
                      << ",\"width\":" << image.width
                      << ",\"height\":" << image.height
                      << ",\"palette\":"
                      << image.palette_index
                      << ",\"palette_count\":"
                      << image.palette_count
                      << ",\"has_stp\":"
                      << (image.has_semitransparent_pixels
                              ? "true"
                              : "false")
                      << "}";
            }
            index << "]";
        }

        index << "}";
        if (i + 1u != parsed.value.size()) {
            index << ",";
        }
        index << "\n";
    }
    index << "  ]\n}\n";

    if (!index) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing PAC chunk manifest");
    }
    return Result<std::filesystem::path>::success(index_path);
}

Result<std::filesystem::path> write_palette_preview(
    const std::filesystem::path& output_root,
    const DiscFileEntry& entry,
    const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != 512u) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::unsupported_format,
            "CLT palette is not 256 BGR555 colors");
    }

    std::filesystem::path stem = std::filesystem::path{entry.name}.stem();
    const auto out_path =
        output_root / "derived" / "palettes" /
        (stem.string() + ".tga");

    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create palette directory: " + ec.message());
    }

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create palette preview");
    }

    std::array<std::uint8_t, 18> header{};
    header[2] = 2u;
    header[12] = 0u;
    header[13] = 1u;
    header[14] = 1u;
    header[15] = 0u;
    header[16] = 32u;
    header[17] = 0x20u;
    out.write(
        reinterpret_cast<const char*>(header.data()),
        static_cast<std::streamsize>(header.size()));

    for (std::size_t i = 0u; i < 256u; ++i) {
        const auto value = static_cast<std::uint16_t>(
            bytes[i * 2u] |
            (static_cast<std::uint16_t>(bytes[i * 2u + 1u]) << 8u));
        const std::array<std::uint8_t, 4> bgra{
            expand5(value >> 10u),
            expand5(value >> 5u),
            expand5(value),
            255u,
        };
        out.write(
            reinterpret_cast<const char*>(bgra.data()),
            static_cast<std::streamsize>(bgra.size()));
    }

    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing palette preview");
    }
    return Result<std::filesystem::path>::success(out_path);
}

bool useful_ascii_string(std::string_view value) {
    if (value.size() < 4u) return false;
    std::size_t letters = 0u;
    for (const char ch : value) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            ++letters;
        }
    }
    return letters >= 2u;
}

Result<std::filesystem::path> write_fighter_overlay_json(
    const std::filesystem::path& output_root,
    const DiscFileEntry& entry,
    const std::vector<std::uint8_t>& primary,
    const std::vector<std::uint8_t>& mirror) {
    const auto analyzed =
        analyze_fighter_overlay_pair(primary, mirror);
    if (!analyzed) {
        return Result<std::filesystem::path>::failure(
            analyzed.error, analyzed.detail);
    }

    auto stem = std::filesystem::path{entry.name}.stem().string();
    const auto out_path =
        output_root / "derived" / "fighter_overlay" /
        (stem + ".json");
    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create fighter overlay output directory: " +
                ec.message());
    }

    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create fighter overlay JSON");
    }

    out << "{\n"
        << "  \"source_primary\": \""
        << json_escape(entry.path) << "\",\n"
        << "  \"source_mirror\": \"/M/"
        << json_escape(stem) << "X.BIN\",\n"
        << "  \"primary_load_base\": \"0x800DF000\",\n"
        << "  \"mirror_delta\": \"0x00015800\",\n"
        << "  \"size\": " << analyzed.value.size_bytes << ",\n"
        << "  \"verified_direct_relocations\": "
        << analyzed.value.relocation_count << ",\n"
        << "  \"residual_pair_difference_bytes\": "
        << analyzed.value.residual_difference_bytes << ",\n"
        << "  \"relocations\": [\n";

    for (std::size_t index = 0u;
         index < analyzed.value.relocations.size();
         ++index) {
        const auto& relocation =
            analyzed.value.relocations[index];
        out << "    {\"field_offset\":"
            << relocation.field_offset
            << ",\"target_offset\":"
            << relocation.target_offset
            << "}";
        if (index + 1u !=
            analyzed.value.relocations.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";

    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing fighter overlay JSON");
    }
    return Result<std::filesystem::path>::success(out_path);
}

Result<std::filesystem::path> write_fighter_tk_json(
    const std::filesystem::path& output_root,
    const DiscFileEntry& entry,
    const std::vector<std::uint8_t>& tkc,
    const std::vector<std::uint8_t>& tkd) {
    const auto parsed = parse_fighter_tk_roots(tkc, tkd);
    if (!parsed) {
        return Result<std::filesystem::path>::failure(
            parsed.error, parsed.detail);
    }

    auto stem = std::filesystem::path{entry.name}.stem().string();
    const auto suffix = std::string{"_TKC"};
    if (stem.size() >= suffix.size() &&
        stem.compare(
            stem.size() - suffix.size(),
            suffix.size(),
            suffix) == 0) {
        stem.resize(stem.size() - suffix.size());
    }

    const auto out_path =
        output_root / "derived" / "fighter_tk" /
        (stem + ".json");
    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create fighter TK output directory: " + ec.message());
    }

    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create fighter TK JSON");
    }

    out << "{\n"
        << "  \"source_tkc\": \""
        << json_escape(entry.path) << "\",\n"
        << "  \"source_tkd\": \"/M/"
        << json_escape(stem) << "_TKD.BIN\",\n"
        << "  \"tkc_original_load_base\": \"0x8010D800\",\n"
        << "  \"runtime_representation\": \"native_offsets_only\",\n"
        << "  \"slot_count\": "
        << fighter_tk_slot_count << ",\n"
        << "  \"slots\": [\n";

    for (std::size_t index = 0u;
         index < parsed.value.slots.size();
         ++index) {
        const auto& slot = parsed.value.slots[index];
        out << "    {\"index\":" << index
            << ",\"tkc_null\":"
            << (slot.tkc_null ? "true" : "false")
            << ",\"tkc_offset\":";
        if (slot.tkc_null) {
            out << "null";
        } else {
            out << slot.tkc_offset;
        }
        out << ",\"tkd_value\":"
            << slot.tkd_value
            << ",\"tkc_records\":[";
        for (std::size_t record_index = 0u;
             record_index < slot.tkc_records.size();
             ++record_index) {
            const auto& record =
                slot.tkc_records[record_index];
            if (record_index != 0u) out << ",";
            out << "{\"source_offset\":"
                << record.source_offset
                << ",\"fields\":[";
            for (std::size_t field = 0u;
                 field < record.fields.size();
                 ++field) {
                if (field != 0u) out << ",";
                out << record.fields[field];
            }
            out << "]}";
        }
        out << "]}";
        if (index + 1u != parsed.value.slots.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";

    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing fighter TK JSON");
    }
    return Result<std::filesystem::path>::success(out_path);
}

Result<std::filesystem::path> write_hit_table_json(
    const std::filesystem::path& output_root,
    const DiscFileEntry& entry,
    const std::vector<std::uint8_t>& bytes) {
    const auto parsed = parse_hit_table(bytes);
    if (!parsed) {
        return Result<std::filesystem::path>::failure(
            parsed.error, parsed.detail);
    }

    const auto out_path =
        output_root / "derived" / "hit" /
        (std::filesystem::path{entry.name}.stem().string() + ".json");
    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create HIT output directory: " + ec.message());
    }

    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create HIT JSON");
    }

    out << "{\n"
        << "  \"source\": \"" << json_escape(entry.path) << "\",\n"
        << "  \"record_count\": 512,\n"
        << "  \"nonzero_records\": "
        << parsed.value.nonzero_records << ",\n"
        << "  \"field_semantics\": \"unresolved_four_signed_int16\",\n"
        << "  \"records\": [\n";

    bool first = true;
    for (std::size_t index = 0u;
         index < parsed.value.records.size();
         ++index) {
        const auto& record = parsed.value.records[index];
        if (record.empty()) continue;
        if (!first) out << ",\n";
        first = false;
        out << "    {\"index\":" << index
            << ",\"a\":" << record.a
            << ",\"b\":" << record.b
            << ",\"c\":" << record.c
            << ",\"d\":" << record.d
            << "}";
    }
    out << "\n  ]\n}\n";

    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing HIT JSON");
    }
    return Result<std::filesystem::path>::success(out_path);
}

Result<std::filesystem::path> write_candidate_strings(
    const std::filesystem::path& output_root,
    const DiscFileEntry& entry,
    const std::vector<std::uint8_t>& bytes) {
    std::vector<std::string> values;
    std::set<std::string> seen;
    std::string current;

    const auto flush = [&]() {
        if (useful_ascii_string(current) &&
            seen.insert(current).second) {
            values.push_back(current);
        }
        current.clear();
    };

    for (const auto byte : bytes) {
        if (byte >= 0x20u && byte <= 0x7Eu) {
            if (current.size() < 160u) {
                current.push_back(static_cast<char>(byte));
            }
        } else {
            flush();
        }
    }
    flush();

    if (values.empty()) {
        return Result<std::filesystem::path>::success({});
    }

    const auto out_path =
        output_root / "derived" / "text" /
        (std::filesystem::path{entry.name}.stem().string() + ".txt");
    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create text extraction directory: " + ec.message());
    }

    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create extracted text file");
    }
    for (const auto& value : values) {
        out << value << '\n';
    }
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing extracted strings");
    }
    return Result<std::filesystem::path>::success(out_path);
}

std::string path_relative_to(
    const std::filesystem::path& path,
    const std::filesystem::path& root) {
    std::error_code ec;
    const auto relative = std::filesystem::relative(path, root, ec);
    return ec ? path.generic_string() : relative.generic_string();
}

Result<void> write_manifest(
    const std::filesystem::path& output_root,
    const ContentImportSummary& summary) {
    const auto path = output_root / "manifest.json";
    std::error_code ec;
    std::filesystem::create_directories(output_root, ec);
    if (ec) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "cannot create content root: " + ec.message());
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "cannot create content manifest");
    }

    out << "{\n";
    out << "  \"schema\": 1,\n";
    out << "  \"engine_target\": \"Godot 4.7.2\",\n";
    out << "  \"runtime_policy\": \"content-only; no PS1 CPU/GPU/BIOS/SPU/SIO/timers\",\n";
    out << "  \"source_format\": \""
        << json_escape(summary.source_format) << "\",\n";
    out << "  \"logical_sector_count\": "
        << summary.logical_sector_count << ",\n";
    out << "  \"files_imported\": "
        << summary.files_imported << ",\n";
    out << "  \"bytes_imported\": "
        << summary.bytes_imported << ",\n";
    out << "  \"files_excluded\": "
        << summary.files_excluded << ",\n";
    out << "  \"excluded_runtime_sources\": ["
        << "\"/SLUS_010.60\", \"/SYSTEM.CNF\", \"/ZNULL.DAT\""
        << "],\n";
    out << "  \"entries\": [\n";

    for (std::size_t i = 0u; i < summary.entries.size(); ++i) {
        const auto& entry = summary.entries[i];
        out << "    {"
            << "\"source\":\"" << json_escape(entry.source_path) << "\","
            << "\"output\":\"" << json_escape(entry.output_path) << "\","
            << "\"kind\":\"" << content_kind_name(entry.kind) << "\","
            << "\"size\":" << entry.size_bytes << ","
            << "\"fnv1a64\":\"" << std::hex << std::setw(16)
            << std::setfill('0') << entry.fnv1a64 << std::dec << "\"";
        if (!entry.derived_path.empty()) {
            out << ",\"derived\":\""
                << json_escape(entry.derived_path) << "\"";
        }
        out << "}";
        if (i + 1u != summary.entries.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    if (!out) {
        return Result<void>::failure(
            ErrorCode::io_error,
            "failed while writing content manifest");
    }
    return Result<void>::success();
}

} // namespace

std::string content_kind_name(ContentKind kind) {
    switch (kind) {
        case ContentKind::graphics_pack: return "graphics_pack";
        case ContentKind::character_data: return "character_data";
        case ContentKind::hitbox_data: return "hitbox_data";
        case ContentKind::script_data: return "script_data";
        case ContentKind::ui_data: return "ui_data";
        case ContentKind::audio_xa: return "audio_xa";
        case ContentKind::palette: return "palette";
        case ContentKind::color_metadata: return "color_metadata";
        case ContentKind::binary_data: return "binary_data";
    }
    return "binary_data";
}

ContentKind classify_content_path(const std::string& iso_path) {
    const auto path = ascii_upper(iso_path);
    if (starts_with(path, "/P/") && ends_with(path, ".PAC")) {
        return ContentKind::graphics_pack;
    }
    if (starts_with(path, "/X/") && ends_with(path, ".XA")) {
        return ContentKind::audio_xa;
    }
    if (starts_with(path, "/C/") && ends_with(path, ".CLT")) {
        return ContentKind::palette;
    }
    if (starts_with(path, "/C/") && ends_with(path, ".FIN")) {
        return ContentKind::color_metadata;
    }
    if (starts_with(path, "/M/") && ends_with(path, "_HIT.BIN")) {
        return ContentKind::hitbox_data;
    }
    if (starts_with(path, "/M/PL") && ends_with(path, ".BIN")) {
        return ContentKind::character_data;
    }
    if (starts_with(path, "/M/") &&
        (path.find("/M/MENU.BIN") == 0u ||
         path.find("/M/OPTION.BIN") == 0u ||
         path.find("/M/GALLERY.BIN") == 0u ||
         path.find("/M/GSS_") == 0u)) {
        return ContentKind::ui_data;
    }
    if (starts_with(path, "/M/") &&
        (path.find("/M/OP") == 0u ||
         path.find("/M/ED") == 0u ||
         path.find("/M/AC_") == 0u ||
         path.find("/M/DEMO.BIN") == 0u ||
         path.find("/M/BOINGO") == 0u)) {
        return ContentKind::script_data;
    }
    return ContentKind::binary_data;
}

bool is_runtime_only_source(const std::string& iso_path) {
    const auto path = ascii_upper(iso_path);
    return path == "/SLUS_010.60" ||
        path == "/SYSTEM.CNF" ||
        path == "/ZNULL.DAT";
}

Result<ContentImportSummary> import_game_content(
    const std::filesystem::path& source,
    const std::filesystem::path& output_root) {
    auto sectors = open_logical_sector_source(source);
    if (!sectors) {
        return Result<ContentImportSummary>::failure(
            sectors.error, sectors.detail);
    }
    auto image = open_iso9660(sectors.value);
    if (!image) {
        return Result<ContentImportSummary>::failure(
            image.error, image.detail);
    }

    ContentImportSummary summary{};
    summary.source_format = sectors.value.source_format;
    summary.logical_sector_count =
        sectors.value.logical_sector_count;

    struct PendingDirectory {
        std::string path;
    };
    std::vector<PendingDirectory> pending{{"/"}};

    while (!pending.empty()) {
        const auto current = std::move(pending.back());
        pending.pop_back();

        auto listed = list_iso9660_directory(image.value, current.path);
        if (!listed) {
            return Result<ContentImportSummary>::failure(
                listed.error, listed.detail);
        }

        for (const auto& entry : listed.value) {
            if (entry.is_directory) {
                pending.push_back({entry.path});
                continue;
            }
            if (is_runtime_only_source(entry.path)) {
                ++summary.files_excluded;
                continue;
            }

            auto bytes = read_iso9660_file(image.value, entry.path);
            if (!bytes) {
                return Result<ContentImportSummary>::failure(
                    bytes.error, bytes.detail);
            }

            const auto output =
                raw_output_path(output_root, entry.path);
            const auto written = write_bytes(output, bytes.value);
            if (!written) {
                return Result<ContentImportSummary>::failure(
                    written.error, written.detail);
            }

            ContentEntry imported{};
            imported.source_path = entry.path;
            imported.output_path =
                path_relative_to(output, output_root);
            imported.kind = classify_content_path(entry.path);
            imported.size_bytes = bytes.value.size();
            imported.fnv1a64 = fnv1a64(bytes.value);

            if (imported.kind == ContentKind::audio_xa) {
                const auto xa =
                    write_xa_audio(
                        output_root,
                        sectors.value,
                        entry);
                if (!xa) {
                    return Result<ContentImportSummary>::failure(
                        xa.error,
                        entry.path + ": " + xa.detail);
                }
                imported.derived_path =
                    path_relative_to(
                        xa.value, output_root);
            } else if (imported.kind == ContentKind::graphics_pack) {
                const auto split =
                    write_pac_split(
                        output_root, entry, bytes.value);
                if (!split) {
                    return Result<ContentImportSummary>::failure(
                        split.error,
                        entry.path + ": " + split.detail);
                }
                imported.derived_path =
                    path_relative_to(
                        split.value, output_root);
            } else if (imported.kind == ContentKind::palette) {
                const auto preview =
                    write_palette_preview(
                        output_root, entry, bytes.value);
                if (preview) {
                    imported.derived_path =
                        path_relative_to(
                            preview.value, output_root);
                }
            } else if (
                imported.kind == ContentKind::character_data &&
                entry.name.size() == 8u &&
                starts_with(ascii_upper(entry.name), "PL") &&
                ends_with(ascii_upper(entry.name), ".BIN")) {
                auto mirror_path = entry.path;
                mirror_path.insert(
                    mirror_path.size() - 4u,
                    "X");
                const auto mirror =
                    read_iso9660_file(
                        image.value,
                        mirror_path);
                if (!mirror) {
                    return Result<ContentImportSummary>::failure(
                        mirror.error,
                        entry.path +
                            ": paired PLX could not be read: " +
                            mirror.detail);
                }
                const auto overlay =
                    write_fighter_overlay_json(
                        output_root,
                        entry,
                        bytes.value,
                        mirror.value);
                if (!overlay) {
                    return Result<ContentImportSummary>::failure(
                        overlay.error,
                        entry.path + ": " + overlay.detail);
                }
                imported.derived_path =
                    path_relative_to(
                        overlay.value, output_root);
            } else if (
                imported.kind == ContentKind::character_data &&
                ends_with(
                    ascii_upper(entry.path),
                    "_TKC.BIN")) {
                auto tkd_path = entry.path;
                const auto marker =
                    tkd_path.rfind("_TKC.BIN");
                tkd_path.replace(
                    marker,
                    std::string{"_TKC.BIN"}.size(),
                    "_TKD.BIN");
                const auto tkd =
                    read_iso9660_file(
                        image.value,
                        tkd_path);
                if (!tkd) {
                    return Result<ContentImportSummary>::failure(
                        tkd.error,
                        entry.path +
                            ": paired TKD could not be read: " +
                            tkd.detail);
                }
                const auto roots =
                    write_fighter_tk_json(
                        output_root,
                        entry,
                        bytes.value,
                        tkd.value);
                if (!roots) {
                    return Result<ContentImportSummary>::failure(
                        roots.error,
                        entry.path + ": " + roots.detail);
                }
                imported.derived_path =
                    path_relative_to(
                        roots.value, output_root);
            } else if (imported.kind == ContentKind::hitbox_data) {
                const auto hit =
                    write_hit_table_json(
                        output_root, entry, bytes.value);
                if (!hit) {
                    return Result<ContentImportSummary>::failure(
                        hit.error,
                        entry.path + ": " + hit.detail);
                }
                imported.derived_path =
                    path_relative_to(
                        hit.value, output_root);
            } else if (
                starts_with(ascii_upper(entry.path), "/M/") &&
                ends_with(ascii_upper(entry.path), ".BIN")) {
                const auto strings =
                    write_candidate_strings(
                        output_root, entry, bytes.value);
                if (strings && !strings.value.empty()) {
                    imported.derived_path =
                        path_relative_to(
                            strings.value, output_root);
                }
            }

            ++summary.files_imported;
            summary.bytes_imported += bytes.value.size();
            summary.entries.push_back(std::move(imported));
        }
    }

    std::sort(
        summary.entries.begin(),
        summary.entries.end(),
        [](const ContentEntry& lhs, const ContentEntry& rhs) {
            return lhs.source_path < rhs.source_path;
        });

    const auto manifest = write_manifest(output_root, summary);
    if (!manifest) {
        return Result<ContentImportSummary>::failure(
            manifest.error, manifest.detail);
    }

    return Result<ContentImportSummary>::success(std::move(summary));
}

} // namespace jojo::content
