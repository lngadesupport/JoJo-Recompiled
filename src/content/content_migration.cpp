#include "content/content_migration.h"
#include "content/pac_archive.h"
#include "content/tim_image.h"

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

            if (imported.kind == ContentKind::graphics_pack) {
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
