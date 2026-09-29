#include "content/content_migration.h"
#include "content/fighter_native_links.h"
#include "content/fighter_overlay.h"
#include "content/fighter_render_context.h"
#include "content/fighter_tk.h"
#include "content/hit_table.h"
#include "content/kpln_graphics.h"
#include "content/kpln_sprite_frames.h"
#include "content/kpln_renderer.h"
#include "content/kpln_clut.h"
#include "content/pac_archive.h"
#include "content/pcm_wav.h"
#include "content/tim_image.h"
#include "content/xa_adpcm_decoder.h"
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
          << "  \"runtime_representation\": \"pcm16_wav\",\n"
          << "  \"source_adpcm_payload_retained\": false,\n"
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
        const auto decoded =
            decode_xa_adpcm(
                stream.coding,
                stream.adpcm_payload);
        if (!decoded) {
            return Result<std::filesystem::path>::failure(
                decoded.error,
                entry.path + " channel " +
                    std::to_string(
                        static_cast<unsigned>(stream.channel)) +
                    ": " + decoded.detail);
        }
        const auto wav =
            encode_pcm16_wav(
                decoded.value.sample_rate_hz,
                decoded.value.channel_count,
                decoded.value.samples);
        if (!wav) {
            return Result<std::filesystem::path>::failure(
                wav.error,
                entry.path + " channel " +
                    std::to_string(
                        static_cast<unsigned>(stream.channel)) +
                    ": " + wav.detail);
        }

        std::ostringstream wav_filename;
        wav_filename << "channel_"
                     << std::setfill('0')
                     << std::setw(2)
                     << static_cast<unsigned>(stream.channel)
                     << ".wav";
        const auto wav_path =
            directory / wav_filename.str();
        const auto wav_written =
            write_bytes(wav_path, wav.value);
        if (!wav_written) {
            return Result<std::filesystem::path>::failure(
                wav_written.error, wav_written.detail);
        }

        index << "    {"
              << "\"channel\":"
              << static_cast<unsigned>(stream.channel)
              << ",\"coding\":"
              << static_cast<unsigned>(stream.coding)
              << ",\"sample_rate_hz\":"
              << decoded.value.sample_rate_hz
              << ",\"channels\":"
              << decoded.value.channel_count
              << ",\"sector_count\":"
              << stream.packets.size()
              << ",\"wav\":\""
              << json_escape(wav_filename.str())
              << "\",\"pcm_sample_count\":"
              << decoded.value.samples.size()
              << ",\"eof_sector_indices\":[";
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

Result<std::filesystem::path> write_pac_palette_bank_preview(
    const std::filesystem::path& output_root,
    const DiscFileEntry& entry,
    std::size_t chunk_index,
    const PacChunk& chunk,
    std::size_t& palette_count) {
    constexpr std::size_t kPaletteBytes = 32u;
    constexpr std::size_t kColorsPerPalette = 16u;
    if (chunk.bytes.empty() ||
        chunk.bytes.size() % kPaletteBytes != 0u) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::unsupported_format,
            "generic PAC palette preview requires whole 32-byte BGR555 rows");
    }
    palette_count =
        chunk.bytes.size() / kPaletteBytes;

    std::vector<std::uint32_t> pixels;
    pixels.reserve(palette_count * kColorsPerPalette);
    for (std::size_t palette = 0u;
         palette < palette_count;
         ++palette) {
        for (std::size_t color_index = 0u;
             color_index < kColorsPerPalette;
             ++color_index) {
            const auto offset =
                palette * kPaletteBytes +
                color_index * 2u;
            const auto color =
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(
                        chunk.bytes[offset]) |
                    (static_cast<std::uint16_t>(
                        chunk.bytes[offset + 1u]) << 8u));
            const auto red = expand5(color);
            const auto green = expand5(color >> 5u);
            const auto blue = expand5(color >> 10u);
            const auto alpha =
                (color & 0x7FFFu) == 0u ? 0u : 255u;
            pixels.push_back(
                static_cast<std::uint32_t>(red) |
                (static_cast<std::uint32_t>(green) << 8u) |
                (static_cast<std::uint32_t>(blue) << 16u) |
                (static_cast<std::uint32_t>(alpha) << 24u));
        }
    }

    const auto stem =
        std::filesystem::path{entry.name}.stem().string();
    std::ostringstream filename;
    filename << std::setfill('0')
             << std::setw(3) << chunk_index
             << "_type_"
             << std::hex << std::setw(4)
             << chunk.type << std::dec
             << ".tga";
    const auto path =
        output_root / "derived" / "pac_palettes" /
        stem / filename.str();
    const auto written =
        write_rgba_tga(
            path,
            16u,
            static_cast<std::uint32_t>(palette_count),
            pixels);
    if (!written) {
        return Result<std::filesystem::path>::failure(
            written.error, written.detail);
    }
    return Result<std::filesystem::path>::success(path);
}

Result<std::filesystem::path> write_kpln_native_graphics(
    const std::filesystem::path& output_root,
    const DiscFileEntry& entry,
    const std::vector<PacChunk>& chunks) {
    const auto stem =
        std::filesystem::path{entry.name}.stem().string();
    const auto directory =
        output_root / "derived" / "kpln" / stem;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create KPLN output directory: " + ec.message());
    }

    const PacChunk* direct_0800 = nullptr;
    const PacChunk* tiles_0801 = nullptr;
    const PacChunk* cached_0802 = nullptr;
    const PacChunk* surface_0202 = nullptr;
    const PacChunk* surface_0204 = nullptr;
    const PacChunk* pool_0803 = nullptr;
    const PacChunk* pool_0804 = nullptr;
    const PacChunk* pool_0805 = nullptr;
    const PacChunk* pool_0806 = nullptr;
    const PacChunk* pool_0807 = nullptr;

    for (const auto& chunk : chunks) {
        switch (chunk.type) {
            case 0x0800u: direct_0800 = &chunk; break;
            case 0x0801u: tiles_0801 = &chunk; break;
            case 0x0802u: cached_0802 = &chunk; break;
            case 0x0202u: surface_0202 = &chunk; break;
            case 0x0204u: surface_0204 = &chunk; break;
            case 0x0803u: pool_0803 = &chunk; break;
            case 0x0804u: pool_0804 = &chunk; break;
            case 0x0805u: pool_0805 = &chunk; break;
            case 0x0806u: pool_0806 = &chunk; break;
            case 0x0807u: pool_0807 = &chunk; break;
            default: break;
        }
    }

    auto write_index_preview =
        [&](const std::filesystem::path& path,
            std::uint32_t width,
            std::uint32_t height,
            const std::vector<std::uint8_t>& indices)
            -> Result<void> {
        if (indices.size() !=
            static_cast<std::size_t>(width) * height) {
            return Result<void>::failure(
                ErrorCode::invalid_argument,
                "indexed preview dimensions do not match pixel data");
        }
        std::vector<std::uint32_t> rgba;
        rgba.reserve(indices.size());
        for (const auto index : indices) {
            const auto level =
                static_cast<std::uint32_t>(index) * 17u;
            rgba.push_back(
                level |
                (level << 8u) |
                (level << 16u) |
                0xFF000000u);
        }
        return write_rgba_tga(path, width, height, rgba);
    };

    std::vector<KplnDirectFrame> direct_frames;
    if (direct_0800) {
        const auto parsed =
            parse_kpln_direct_frames_0800(
                direct_0800->bytes);
        if (!parsed) {
            return Result<std::filesystem::path>::failure(
                parsed.error,
                entry.path + " 0x0800: " + parsed.detail);
        }
        direct_frames = parsed.value;
    }

    KplnCachedFrameSet cached_frames{};
    bool has_cached_frames = false;
    if (cached_0802 && tiles_0801) {
        const auto parsed =
            parse_kpln_cached_frames_0802(
                cached_0802->bytes,
                tiles_0801->bytes);
        if (!parsed) {
            return Result<std::filesystem::path>::failure(
                parsed.error,
                entry.path + " 0x0802/0x0801: " +
                    parsed.detail);
        }
        cached_frames = parsed.value;
        has_cached_frames = true;
    }

    KplnClutWindows clut_windows{};
    bool has_clut_windows = false;
    if (pool_0803 && pool_0804 && pool_0805 &&
        pool_0806 && pool_0807) {
        const auto parsed =
            build_kpln_clut_windows(
                pool_0803->bytes,
                pool_0804->bytes,
                pool_0805->bytes,
                pool_0806->bytes,
                pool_0807->bytes);
        if (!parsed) {
            return Result<std::filesystem::path>::failure(
                parsed.error,
                entry.path + " CLUT pools: " +
                    parsed.detail);
        }
        clut_windows = parsed.value;
        has_clut_windows = true;
    }

    KplnIndexedPage4bpp direct_atlas_0202{};
    bool has_direct_atlas_0202 = false;
    if (surface_0202) {
        const auto parsed =
            parse_kpln_indexed_page_0202(
                surface_0202->bytes);
        if (!parsed) {
            return Result<std::filesystem::path>::failure(
                parsed.error,
                entry.path + " 0x0202: " +
                    parsed.detail);
        }
        direct_atlas_0202 = parsed.value;
        has_direct_atlas_0202 = true;
    }

    const auto index_path = directory / "graphics.json";
    std::ofstream out(index_path, std::ios::trunc);
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create KPLN graphics JSON");
    }

    out << "{\n"
        << "  \"source\": \""
        << json_escape(entry.path) << "\",\n"
        << "  \"schema\": 2,\n"
        << "  \"tile_size\": 16,\n"
        << "  \"direct_frames_0800\": {"
        << "\"frame_count\":" << direct_frames.size()
        << ",\"frames\":[";

    for (std::size_t frame_index = 0u;
         frame_index < direct_frames.size();
         ++frame_index) {
        const auto& frame = direct_frames[frame_index];
        std::string preview_relative;
        std::int32_t preview_origin_x = 0;
        std::int32_t preview_origin_y = 0;
        if (has_direct_atlas_0202 &&
            has_clut_windows &&
            !clut_windows.windows.empty()) {
            const auto rendered =
                render_kpln_direct_frame(
                    frame,
                    direct_atlas_0202,
                    clut_windows.windows[0],
                    0u,
                    0u);
            if (rendered) {
                std::ostringstream preview_name;
                preview_name << "frame_"
                             << std::setfill('0')
                             << std::setw(4)
                             << frame_index
                             << "_default.tga";
                const auto preview_path =
                    directory / "direct_previews" /
                    preview_name.str();
                const auto preview_written =
                    write_rgba_tga(
                        preview_path,
                        rendered.value.width,
                        rendered.value.height,
                        rendered.value.rgba8);
                if (!preview_written) {
                    return Result<std::filesystem::path>::failure(
                        preview_written.error,
                        preview_written.detail);
                }
                preview_relative =
                    path_relative_to(
                        preview_path, output_root);
                preview_origin_x =
                    rendered.value.origin_x;
                preview_origin_y =
                    rendered.value.origin_y;
            }
        }

        if (frame_index != 0u) out << ",";
        out << "{\"index\":" << frame_index
            << ",\"source_record_index\":"
            << frame.source_record_index
            << ",\"preview_default_context\":";
        if (preview_relative.empty()) {
            out << "null";
        } else {
            out << "\"" << json_escape(
                preview_relative) << "\"";
        }
        out << ",\"preview_origin_x\":"
            << preview_origin_x
            << ",\"preview_origin_y\":"
            << preview_origin_y
            << ",\"parts\":[";
        for (std::size_t part_index = 0u;
             part_index < frame.parts.size();
             ++part_index) {
            const auto& part = frame.parts[part_index];
            if (part_index != 0u) out << ",";
            out << "{\"data_offset_words\":"
                << part.header.data_offset_units
                << ",\"columns\":"
                << static_cast<unsigned>(
                    part.header.columns)
                << ",\"rows\":"
                << static_cast<unsigned>(
                    part.header.rows)
                << ",\"x_offset\":"
                << part.header.x_offset
                << ",\"y_offset\":"
                << part.header.y_offset
                << ",\"marker\":"
                << part.header.marker
                << ",\"continues\":"
                << (part.header.continues
                        ? "true"
                        : "false")
                << ",\"cells\":[";
            for (std::size_t cell_index = 0u;
                 cell_index < part.cells.size();
                 ++cell_index) {
                const auto& cell = part.cells[cell_index];
                if (cell_index != 0u) out << ",";
                out << "{\"column\":" << cell.column
                    << ",\"row\":" << cell.row
                    << ",\"tile_word\":"
                    << cell.tile_word
                    << ",\"empty\":"
                    << (cell.empty ? "true" : "false")
                    << "}";
            }
            out << "]}";
        }
        out << "]}";
    }
    out << "]},\n";

    out << "  \"cached_frames_0802\": ";
    if (has_cached_frames) {
        out << "{\"frame_count\":"
            << cached_frames.frames.size()
            << ",\"unique_tile_count\":"
            << cached_frames.unique_tile_offsets.size()
            << ",\"frames\":[";

        for (std::size_t frame_index = 0u;
             frame_index < cached_frames.frames.size();
             ++frame_index) {
            const auto& frame =
                cached_frames.frames[frame_index];

            std::string preview_relative;
            std::int32_t preview_origin_x = 0;
            std::int32_t preview_origin_y = 0;
            if (has_clut_windows &&
                !clut_windows.windows.empty()) {
                const auto rendered =
                    render_kpln_cached_frame(
                        frame,
                        clut_windows.windows[0],
                        0u,
                        0u,
                        0u,
                        0u,
                        0u);
                if (rendered) {
                    std::ostringstream preview_name;
                    preview_name << "frame_"
                                 << std::setfill('0')
                                 << std::setw(4)
                                 << frame_index
                                 << "_default.tga";
                    const auto preview_path =
                        directory / "cached_previews" /
                        preview_name.str();
                    const auto preview_written =
                        write_rgba_tga(
                            preview_path,
                            rendered.value.width,
                            rendered.value.height,
                            rendered.value.rgba8);
                    if (!preview_written) {
                        return Result<std::filesystem::path>::failure(
                            preview_written.error,
                            preview_written.detail);
                    }
                    preview_relative =
                        path_relative_to(
                            preview_path, output_root);
                    preview_origin_x =
                        rendered.value.origin_x;
                    preview_origin_y =
                        rendered.value.origin_y;
                }
            }

            if (frame_index != 0u) out << ",";
            out << "{\"index\":" << frame_index
                << ",\"source_record_index\":"
                << frame.source_record_index
                << ",\"preview_default_context\":";
            if (preview_relative.empty()) {
                out << "null";
            } else {
                out << "\"" << json_escape(
                    preview_relative) << "\"";
            }
            out << ",\"preview_origin_x\":"
                << preview_origin_x
                << ",\"preview_origin_y\":"
                << preview_origin_y
                << ",\"parts\":[";

            for (std::size_t part_index = 0u;
                 part_index < frame.parts.size();
                 ++part_index) {
                const auto& part =
                    frame.parts[part_index];
                if (part_index != 0u) out << ",";
                out << "{\"data_offset_dwords\":"
                    << part.header.data_offset_units
                    << ",\"columns\":"
                    << static_cast<unsigned>(
                        part.header.columns)
                    << ",\"rows\":"
                    << static_cast<unsigned>(
                        part.header.rows)
                    << ",\"x_offset\":"
                    << part.header.x_offset
                    << ",\"y_offset\":"
                    << part.header.y_offset
                    << ",\"descriptor_limit\":"
                    << part.header.marker
                    << ",\"visible_bit_count\":"
                    << part.visible_bit_count
                    << ",\"continues\":"
                    << (part.header.continues
                            ? "true"
                            : "false")
                    << ",\"cells\":[";

                for (std::size_t cell_index = 0u;
                     cell_index < part.cells.size();
                     ++cell_index) {
                    const auto& cell =
                        part.cells[cell_index];
                    if (cell_index != 0u) out << ",";
                    out << "{\"column\":"
                        << cell.column
                        << ",\"row\":"
                        << cell.row
                        << ",\"visible\":"
                        << (cell.visible
                                ? "true"
                                : "false");
                    if (cell.has_descriptor) {
                        const auto& descriptor =
                            cell.descriptor;
                        out << ",\"descriptor\":{"
                            << "\"raw\":"
                            << descriptor.raw
                            << ",\"stream_offset\":"
                            << descriptor.stream_offset
                            << ",\"clut_selector\":"
                            << static_cast<unsigned>(
                                descriptor.clut_selector)
                            << ",\"transform\":"
                            << static_cast<unsigned>(
                                descriptor.transform)
                            << ",\"compressed_bytes\":"
                            << descriptor.compressed_bytes_consumed
                            << "}";
                    }
                    out << "}";
                }
                out << "]}";
            }
            out << "]}";
        }
        out << "]}";
    } else {
        out << "null";
    }
    out << ",\n";

    out << "  \"indexed_surfaces\": [";
    bool first_surface = true;

    if (has_direct_atlas_0202) {
        const auto page_path =
            directory / "surface_0202_4bpp.tga";
        const auto written =
            write_index_preview(
                page_path,
                direct_atlas_0202.width,
                direct_atlas_0202.height,
                direct_atlas_0202.indices);
        if (!written) {
            return Result<std::filesystem::path>::failure(
                written.error, written.detail);
        }
        out << "{\"source_type\":\"0x0202\""
            << ",\"width\":" << direct_atlas_0202.width
            << ",\"height\":" << direct_atlas_0202.height
            << ",\"index_bits\":4"
            << ",\"preview\":\""
            << json_escape(
                path_relative_to(
                    page_path, output_root))
            << "\"}";
        first_surface = false;
    }

    if (surface_0204) {
        const auto page =
            parse_kpln_indexed_surface_0204(
                surface_0204->bytes);
        if (!page) {
            return Result<std::filesystem::path>::failure(
                page.error,
                entry.path + " 0x0204: " + page.detail);
        }
        const auto page_path =
            directory / "surface_0204_4bpp.tga";
        const auto written =
            write_index_preview(
                page_path,
                page.value.width,
                page.value.height,
                page.value.indices);
        if (!written) {
            return Result<std::filesystem::path>::failure(
                written.error, written.detail);
        }
        if (!first_surface) out << ",";
        out << "{\"source_type\":\"0x0204\""
            << ",\"width\":" << page.value.width
            << ",\"height\":" << page.value.height
            << ",\"index_bits\":4"
            << ",\"preview\":\""
            << json_escape(
                path_relative_to(
                    page_path, output_root))
            << "\"}";
        first_surface = false;
    }
    out << "],\n";

    // Compatibility alias for the current Godot inspector. Prefer 0x0202,
    // otherwise expose the placed 0x0204 surface.
    out << "  \"indexed_page_4bpp\": ";
    if (surface_0202) {
        const auto page =
            parse_kpln_indexed_page_0202(
                surface_0202->bytes);
        out << "{\"source_type\":\"0x0202\""
            << ",\"width\":" << page.value.width
            << ",\"height\":" << page.value.height
            << ",\"preview\":\""
            << json_escape(
                path_relative_to(
                    directory /
                        "surface_0202_4bpp.tga",
                    output_root))
            << "\"}";
    } else if (surface_0204) {
        const auto page =
            parse_kpln_indexed_surface_0204(
                surface_0204->bytes);
        out << "{\"source_type\":\"0x0204\""
            << ",\"width\":" << page.value.width
            << ",\"height\":" << page.value.height
            << ",\"preview\":\""
            << json_escape(
                path_relative_to(
                    directory /
                        "surface_0204_4bpp.tga",
                    output_root))
            << "\"}";
    } else {
        out << "null";
    }
    out << ",\n";

    out << "  \"clut_windows\": ";
    if (has_clut_windows) {
        out << "{\"palette_count\":"
            << clut_windows.palette_count
            << ",\"width\":"
            << kpln_clut_width_words
            << ",\"height\":"
            << kpln_clut_height
            << ",\"previews\":[";
        for (std::size_t palette_id = 0u;
             palette_id < clut_windows.windows.size();
             ++palette_id) {
            const auto& window =
                clut_windows.windows[palette_id];
            std::vector<std::uint32_t> rgba;
            rgba.reserve(window.bgr555.size());
            for (const auto color : window.bgr555) {
                const auto red = expand5(color);
                const auto green = expand5(color >> 5u);
                const auto blue = expand5(color >> 10u);
                const auto alpha =
                    (color & 0x7FFFu) == 0u
                    ? 0u
                    : 255u;
                rgba.push_back(
                    static_cast<std::uint32_t>(red) |
                    (static_cast<std::uint32_t>(green) << 8u) |
                    (static_cast<std::uint32_t>(blue) << 16u) |
                    (static_cast<std::uint32_t>(alpha) << 24u));
            }

            std::ostringstream name;
            name << "clut_palette_"
                 << palette_id
                 << ".tga";
            const auto clut_path =
                directory / name.str();
            const auto written =
                write_rgba_tga(
                    clut_path,
                    window.width,
                    window.height,
                    rgba);
            if (!written) {
                return Result<std::filesystem::path>::failure(
                    written.error, written.detail);
            }

            if (palette_id != 0u) out << ",";
            out << "{\"palette_id\":"
                << palette_id
                << ",\"preview\":\""
                << json_escape(
                    path_relative_to(
                        clut_path, output_root))
                << "\"}";
        }
        out << "]}";
    } else {
        out << "null";
    }

    // Compatibility count for code that previously called these "groups".
    out << ",\n  \"group_table\": {"
        << "\"record_count\":"
        << (has_cached_frames
                ? cached_frames.frames.size()
                : direct_frames.size())
        << ",\"semantic_alias\":"
           "\"deprecated_use_cached_or_direct_frames\""
        << ",\"records\":[]},\n";

    out << "  \"format_notes\": {"
        << "\"0800\":\"direct_12_byte_frame_records\","
        << "\"0801\":\"compressed_16x16_4bpp_tile_streams\","
        << "\"0802\":\"cached_frame_records_masks_descriptors\","
        << "\"descriptor_low24\":\"0801_stream_offset\","
        << "\"descriptor_bits24_29\":\"relative_clut_selector\","
        << "\"descriptor_bits30_31\":\"tile_transform\","
        << "\"tile_previews\":\"omitted_runtime_uses_composed_frames\""
        << "}\n"
        << "}\n";

    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing KPLN graphics JSON");
    }
    return Result<std::filesystem::path>::success(index_path);
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

        if (chunk.type >= 0x0803u &&
            chunk.type <= 0x0807u) {
            std::size_t palette_count = 0u;
            const auto preview =
                write_pac_palette_bank_preview(
                    output_root,
                    entry,
                    i,
                    chunk,
                    palette_count);
            if (!preview) {
                return Result<std::filesystem::path>::failure(
                    preview.error,
                    entry.path + " palette chunk " +
                        std::to_string(i) + ": " +
                        preview.detail);
            }
            index << ",\"palette_bank\":{"
                  << "\"palette_count\":"
                  << palette_count
                  << ",\"colors_per_palette\":16"
                  << ",\"preview\":\""
                  << json_escape(
                      path_relative_to(
                          preview.value,
                          output_root))
                  << "\"}";
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

    const auto upper_name = ascii_upper(entry.name);
    if (starts_with(upper_name, "KPLN") &&
        ends_with(upper_name, ".PAC")) {
        const auto native_graphics =
            write_kpln_native_graphics(
                output_root,
                entry,
                parsed.value);
        if (!native_graphics) {
            return Result<std::filesystem::path>::failure(
                native_graphics.error,
                native_graphics.detail);
        }
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
        << "  \"runtime_representation\": \"native_offsets_and_records\",\n"
        << "  \"slot_count\": "
        << fighter_tk_slot_count << ",\n"
        << "  \"tkc_end_offset\": "
        << parsed.value.tkc_end_offset << ",\n"
        << "  \"tkd_block_size\": "
        << parsed.value.tkd_block_size << ",\n"
        << "  \"slots\": [\n";

    for (std::size_t index = 0u;
         index < parsed.value.slots.size();
         ++index) {
        const auto& slot = parsed.value.slots[index];
        out << "    {\"index\":" << index
            << ",\"tkc_offset\":"
            << slot.tkc_offset
            << ",\"tkd_offset\":"
            << slot.tkd_offset
            << ",\"tkc_records\":[";

        for (std::size_t record_index = 0u;
             record_index < slot.tkc_records.size();
             ++record_index) {
            const auto& record =
                slot.tkc_records[record_index];
            if (record_index != 0u) out << ",";
            out << "{\"source_offset\":"
                << record.source_offset
                << ",\"opcode\":"
                << static_cast<unsigned>(record.opcode)
                << ",\"variant\":"
                << static_cast<unsigned>(record.variant)
                << ",\"reference_index\":"
                << record.reference_index
                << ",\"reference_flags\":"
                << record.reference_flags
                << ",\"parameter2\":"
                << record.parameter2
                << ",\"parameter3\":"
                << record.parameter3
                << ",\"parameter4\":"
                << record.parameter4
                << ",\"raw_words\":[";
            for (std::size_t field = 0u;
                 field < record.raw_words.size();
                 ++field) {
                if (field != 0u) out << ",";
                out << record.raw_words[field];
            }
            out << "]}";
        }

        out << "],\"tkd_records\":[";
        for (std::size_t record_index = 0u;
             record_index < slot.tkd_records.size();
             ++record_index) {
            const auto& record =
                slot.tkd_records[record_index];
            if (record_index != 0u) out << ",";
            out << "{\"source_offset\":"
                << record.source_offset
                << ",\"offset_x\":"
                << record.offset_x
                << ",\"offset_y\":"
                << record.offset_y
                << ",\"packed_element\":"
                << record.packed_element
                << ",\"element_index\":"
                << record.element_index
                << ",\"element_flags\":"
                << record.element_flags
                << ",\"reserved\":"
                << record.reserved
                << "}";
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
        << "  \"field_semantics\": \"x_offset_width_y_offset_height\",\n"
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
            << ",\"x_offset\":" << record.x_offset
            << ",\"width\":" << record.width
            << ",\"y_offset\":" << record.y_offset
            << ",\"height\":" << record.height
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

bool summary_has_source(
    const ContentImportSummary& summary,
    std::string_view path) {
    return std::any_of(
        summary.entries.begin(),
        summary.entries.end(),
        [path](const ContentEntry& entry) {
            return entry.source_path == path;
        });
}

Result<std::filesystem::path> write_fighter_native_links_json(
    const std::filesystem::path& output_root,
    const Iso9660Image& image,
    const ContentImportSummary& summary,
    std::string_view fighter_id) {
    const auto overlay_path =
        std::string{"/M/PL"} + std::string{fighter_id} + ".BIN";
    const auto hit_path =
        std::string{"/M/PL"} + std::string{fighter_id} + "_HIT.BIN";
    const auto tkc_path =
        std::string{"/M/PL"} + std::string{fighter_id} + "_TKC.BIN";
    const auto tkd_path =
        std::string{"/M/PL"} + std::string{fighter_id} + "_TKD.BIN";
    const auto kpln_path =
        std::string{"/P/KPLN"} + std::string{fighter_id} + ".PAC";

    if (!summary_has_source(summary, overlay_path) ||
        !summary_has_source(summary, hit_path) ||
        !summary_has_source(summary, tkc_path) ||
        !summary_has_source(summary, tkd_path) ||
        !summary_has_source(summary, kpln_path)) {
        return Result<std::filesystem::path>::success({});
    }

    const auto overlay_bytes =
        read_iso9660_file(image, overlay_path);
    const auto hit_bytes =
        read_iso9660_file(image, hit_path);
    const auto tkc_bytes =
        read_iso9660_file(image, tkc_path);
    const auto tkd_bytes =
        read_iso9660_file(image, tkd_path);
    const auto kpln_bytes =
        read_iso9660_file(image, kpln_path);
    if (!overlay_bytes || !hit_bytes || !tkc_bytes ||
        !tkd_bytes || !kpln_bytes) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed to reload fighter source data for native link analysis");
    }

    const auto hit = parse_hit_table(hit_bytes.value);
    const auto tk =
        parse_fighter_tk_roots(
            tkc_bytes.value,
            tkd_bytes.value);
    const auto kpln =
        parse_pac_archive(kpln_bytes.value);
    if (!hit || !tk || !kpln) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::invalid_installation,
            "fighter source data failed structural parsing for native link analysis");
    }

    const PacChunk* direct_chunk = nullptr;
    const PacChunk* tile_pool_chunk = nullptr;
    const PacChunk* cached_chunk = nullptr;
    const PacChunk* atlas_0202_chunk = nullptr;
    const PacChunk* pool_0803 = nullptr;
    const PacChunk* pool_0804 = nullptr;
    const PacChunk* pool_0805 = nullptr;
    const PacChunk* pool_0806 = nullptr;
    const PacChunk* pool_0807 = nullptr;
    for (const auto& chunk : kpln.value) {
        if (chunk.type == 0x0800u) {
            direct_chunk = &chunk;
        } else if (chunk.type == 0x0801u) {
            tile_pool_chunk = &chunk;
        } else if (chunk.type == 0x0802u) {
            cached_chunk = &chunk;
        } else if (chunk.type == 0x0202u) {
            atlas_0202_chunk = &chunk;
        } else if (chunk.type == 0x0803u) {
            pool_0803 = &chunk;
        } else if (chunk.type == 0x0804u) {
            pool_0804 = &chunk;
        } else if (chunk.type == 0x0805u) {
            pool_0805 = &chunk;
        } else if (chunk.type == 0x0806u) {
            pool_0806 = &chunk;
        } else if (chunk.type == 0x0807u) {
            pool_0807 = &chunk;
        }
    }

    std::vector<KplnDirectFrame> direct_frames;
    KplnCachedFrameSet cached_frames{};
    bool has_cached_frames = false;
    KplnIndexedPage4bpp direct_atlas{};
    bool has_direct_atlas = false;
    KplnClutWindows clut_windows{};
    bool has_clut_windows = false;

    if (direct_chunk) {
        const auto direct =
            parse_kpln_direct_frames_0800(
                direct_chunk->bytes);
        if (!direct) {
            return Result<std::filesystem::path>::failure(
                direct.error,
                "KPLN 0x0800 failed native link parsing: " +
                    direct.detail);
        }
        direct_frames = direct.value;
    }

    if (cached_chunk && tile_pool_chunk) {
        const auto cached =
            parse_kpln_cached_frames_0802(
                cached_chunk->bytes,
                tile_pool_chunk->bytes);
        if (!cached) {
            return Result<std::filesystem::path>::failure(
                cached.error,
                "KPLN 0x0802/0x0801 failed native link parsing: " +
                    cached.detail);
        }
        cached_frames = cached.value;
        has_cached_frames = true;
    }

    if (atlas_0202_chunk) {
        const auto parsed =
            parse_kpln_indexed_page_0202(
                atlas_0202_chunk->bytes);
        if (!parsed) {
            return Result<std::filesystem::path>::failure(
                parsed.error,
                "KPLN 0x0202 failed native link parsing: " +
                    parsed.detail);
        }
        direct_atlas = parsed.value;
        has_direct_atlas = true;
    }

    if (pool_0803 && pool_0804 && pool_0805 &&
        pool_0806 && pool_0807) {
        const auto parsed =
            build_kpln_clut_windows(
                pool_0803->bytes,
                pool_0804->bytes,
                pool_0805->bytes,
                pool_0806->bytes,
                pool_0807->bytes);
        if (!parsed) {
            return Result<std::filesystem::path>::failure(
                parsed.error,
                "KPLN CLUT failed native link parsing: " +
                    parsed.detail);
        }
        clut_windows = parsed.value;
        has_clut_windows = true;
    }

    const auto direct_frame_count =
        static_cast<std::uint32_t>(
            direct_frames.size());
    const auto cached_frame_count =
        static_cast<std::uint32_t>(
            cached_frames.frames.size());

    const auto analysis =
        analyze_fighter_native_links(
            hit.value,
            tk.value,
            direct_frame_count,
            cached_frame_count);

    const auto context_frame_count =
        std::max(
            direct_frame_count,
            cached_frame_count);
    const auto render_context_candidates =
        scan_compact_render_context_candidates(
            overlay_bytes.value,
            context_frame_count,
            0u);
    const auto animation_script_candidates =
        scan_animation_script_candidates(
            overlay_bytes.value,
            context_frame_count);
    std::uint32_t frame_sequence_candidate_count = 0u;
    std::uint32_t canonical_frame_sequence_candidate_count = 0u;
    std::uint32_t tkc_like_candidate_count = 0u;
    for (const auto& candidate : animation_script_candidates) {
        if (candidate.classification ==
            FighterAnimationCandidateClass::frame_sequence_like) {
            ++frame_sequence_candidate_count;
            if (candidate.canonical_sequence_root) {
                ++canonical_frame_sequence_candidate_count;
            }
        } else if (
            candidate.classification ==
            FighterAnimationCandidateClass::tkc_like) {
            ++tkc_like_candidate_count;
        }
    }

    const auto directory =
        output_root / "derived" / "fighters" /
        std::string{fighter_id};
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create fighter native link directory: " +
                ec.message());
    }

    std::vector<std::int32_t> best_context_index_by_frame(
        context_frame_count,
        -1);
    for (std::size_t index = 0u;
         index < render_context_candidates.size();
         ++index) {
        const auto frame =
            render_context_candidates[index].frame_index;
        if (frame >= best_context_index_by_frame.size()) {
            continue;
        }
        const auto current =
            best_context_index_by_frame[frame];
        if (current < 0 ||
            render_context_candidates[index].confidence_score >
                render_context_candidates[
                    static_cast<std::size_t>(current)]
                    .confidence_score) {
            best_context_index_by_frame[frame] =
                static_cast<std::int32_t>(index);
        }
    }

    std::vector<std::array<std::string, 2>>
        cached_context_previews(
            render_context_candidates.size());
    std::vector<std::array<std::string, 2>>
        direct_context_previews(
            render_context_candidates.size());

    if (has_clut_windows) {
        const auto preview_directory =
            output_root / "derived" / "kpln" /
            ("KPLN" + std::string{fighter_id}) /
            "context_previews";
        std::filesystem::create_directories(
            preview_directory, ec);
        if (ec) {
            return Result<std::filesystem::path>::failure(
                ErrorCode::io_error,
                "cannot create KPLN context preview directory: " +
                    ec.message());
        }

        for (std::size_t frame = 0u;
             frame < best_context_index_by_frame.size();
             ++frame) {
            const auto selected =
                best_context_index_by_frame[frame];
            if (selected < 0) continue;
            const auto candidate_index =
                static_cast<std::size_t>(selected);
            const auto& context =
                render_context_candidates[candidate_index];

            for (std::size_t palette_id = 0u;
                 palette_id < clut_windows.windows.size() &&
                 palette_id < 2u;
                 ++palette_id) {
                const auto& clut =
                    clut_windows.windows[palette_id];

                if (has_cached_frames &&
                    frame < cached_frames.frames.size()) {
                    const auto rendered =
                        render_kpln_cached_frame(
                            cached_frames.frames[frame],
                            clut,
                            0u,
                            context.clut_base,
                            context.clut_mode,
                            0u,
                            context.orientation,
                            context.clut_row_base);
                    if (rendered) {
                        std::ostringstream name;
                        name << "cached_frame_"
                             << std::setfill('0')
                             << std::setw(4)
                             << frame
                             << "_ctx_"
                             << std::hex
                             << context.source_offset
                             << std::dec
                             << "_p" << palette_id
                             << ".tga";
                        const auto path =
                            preview_directory /
                            name.str();
                        const auto written =
                            write_rgba_tga(
                                path,
                                rendered.value.width,
                                rendered.value.height,
                                rendered.value.rgba8);
                        if (!written) {
                            return Result<std::filesystem::path>::failure(
                                written.error,
                                written.detail);
                        }
                        cached_context_previews[
                            candidate_index][palette_id] =
                            path_relative_to(
                                path, output_root);
                    }
                }

                if (has_direct_atlas &&
                    frame < direct_frames.size()) {
                    const auto rendered =
                        render_kpln_direct_frame(
                            direct_frames[frame],
                            direct_atlas,
                            clut,
                            0u,
                            context.clut_base,
                            context.clut_row_base);
                    if (rendered) {
                        std::ostringstream name;
                        name << "direct_frame_"
                             << std::setfill('0')
                             << std::setw(4)
                             << frame
                             << "_ctx_"
                             << std::hex
                             << context.source_offset
                             << std::dec
                             << "_p" << palette_id
                             << ".tga";
                        const auto path =
                            preview_directory /
                            name.str();
                        const auto written =
                            write_rgba_tga(
                                path,
                                rendered.value.width,
                                rendered.value.height,
                                rendered.value.rgba8);
                        if (!written) {
                            return Result<std::filesystem::path>::failure(
                                written.error,
                                written.detail);
                        }
                        direct_context_previews[
                            candidate_index][palette_id] =
                            path_relative_to(
                                path, output_root);
                    }
                }
            }
        }
    }

    const auto out_path = directory / "native_links.json";
    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create fighter native link JSON");
    }

    out << "{\n"
        << "  \"schema\": 2,\n"
        << "  \"fighter_id\": \""
        << json_escape(fighter_id) << "\",\n"
        << "  \"policy\": "
           "\"structural_candidates_only_until_consumer_semantics_are_proven\",\n"
        << "  \"tkc_record_count\": "
        << analysis.tkc_record_count << ",\n"
        << "  \"tkc_hit_index_in_range_count\": "
        << analysis.tkc_hit_index_in_range_count << ",\n"
        << "  \"tkc_nonempty_hit_candidate_count\": "
        << analysis.tkc_nonempty_hit_candidate_count << ",\n"
        << "  \"tkd_record_count\": "
        << analysis.tkd_record_count << ",\n"
        << "  \"tkd_direct_frame_index_in_range_count\": "
        << analysis.tkd_direct_frame_index_in_range_count << ",\n"
        << "  \"tkd_cached_frame_index_in_range_count\": "
        << analysis.tkd_cached_frame_index_in_range_count << ",\n"
        << "  \"direct_frame_count\": "
        << direct_frame_count << ",\n"
        << "  \"cached_frame_count\": "
        << cached_frame_count << ",\n"
        << "  \"render_context_candidate_count\": "
        << render_context_candidates.size() << ",\n"
        << "  \"animation_script_candidate_count\": "
        << animation_script_candidates.size() << ",\n"
        << "  \"frame_sequence_candidate_count\": "
        << frame_sequence_candidate_count << ",\n"
        << "  \"canonical_frame_sequence_candidate_count\": "
        << canonical_frame_sequence_candidate_count << ",\n"
        << "  \"tkc_like_candidate_count\": "
        << tkc_like_candidate_count << ",\n"
        << "  \"tkc_hit_candidates\": [\n";

    for (std::size_t index = 0u;
         index < analysis.tkc_hit_candidates.size();
         ++index) {
        const auto& candidate =
            analysis.tkc_hit_candidates[index];
        out << "    {\"slot\":"
            << candidate.slot_index
            << ",\"record\":"
            << candidate.record_index
            << ",\"candidate_hit_table_index\":"
            << candidate.hit_index
            << ",\"target_in_range\":"
            << (candidate.target_in_range ? "true" : "false")
            << ",\"target_nonempty\":"
            << (candidate.target_nonempty ? "true" : "false");

        if (candidate.target_in_range &&
            candidate.target_nonempty) {
            const auto& rectangle =
                hit.value.records[candidate.hit_index];
            out << ",\"candidate_rect\":{"
                << "\"x_offset\":" << rectangle.x_offset
                << ",\"y_offset\":" << rectangle.y_offset
                << ",\"width\":" << rectangle.width
                << ",\"height\":" << rectangle.height
                << "}";
        }
        out << "}";
        if (index + 1u !=
            analysis.tkc_hit_candidates.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "  ],\n"
        << "  \"tkd_graphics_candidates\": [\n";
    for (std::size_t index = 0u;
         index < analysis.tkd_graphics_candidates.size();
         ++index) {
        const auto& candidate =
            analysis.tkd_graphics_candidates[index];
        out << "    {\"slot\":"
            << candidate.slot_index
            << ",\"record\":"
            << candidate.record_index
            << ",\"candidate_element_index\":"
            << candidate.element_index
            << ",\"candidate_direct_frame_index\":"
            << candidate.element_index
            << ",\"candidate_cached_frame_index\":"
            << candidate.element_index
            << ",\"direct_target_in_range\":"
            << (candidate.direct_target_in_range ? "true" : "false")
            << ",\"cached_target_in_range\":"
            << (candidate.cached_target_in_range ? "true" : "false")
            << "}";
        if (index + 1u !=
            analysis.tkd_graphics_candidates.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n"
        << "  \"render_context_candidates\": [\n";
    for (std::size_t index = 0u;
         index < render_context_candidates.size();
         ++index) {
        const auto& candidate =
            render_context_candidates[index];
        out << "    {\"source_offset\":"
            << candidate.source_offset
            << ",\"frame_index\":"
            << candidate.frame_index
            << ",\"clut_mode\":"
            << static_cast<int>(candidate.clut_mode)
            << ",\"clut_base\":"
            << static_cast<unsigned>(candidate.clut_base)
            << ",\"clut_row_base\":"
            << candidate.clut_row_base
            << ",\"asset_slot\":"
            << static_cast<unsigned>(candidate.asset_slot)
            << ",\"flip_a\":"
            << static_cast<unsigned>(candidate.flip_a)
            << ",\"flip_b\":"
            << static_cast<unsigned>(candidate.flip_b)
            << ",\"orientation\":"
            << static_cast<unsigned>(candidate.orientation)
            << ",\"confidence_score\":"
            << candidate.confidence_score
            << ",\"selected_best_for_frame\":"
            << ((candidate.frame_index <
                    best_context_index_by_frame.size() &&
                 best_context_index_by_frame[
                    candidate.frame_index] ==
                    static_cast<std::int32_t>(index))
                    ? "true"
                    : "false")
            << ",\"cached_previews\":[";
        for (std::size_t palette_id = 0u;
             palette_id < 2u;
             ++palette_id) {
            if (palette_id != 0u) out << ",";
            const auto& path =
                cached_context_previews[index][palette_id];
            if (path.empty()) {
                out << "null";
            } else {
                out << "\"" << json_escape(path) << "\"";
            }
        }
        out << "],\"direct_previews\":[";
        for (std::size_t palette_id = 0u;
             palette_id < 2u;
             ++palette_id) {
            if (palette_id != 0u) out << ",";
            const auto& path =
                direct_context_previews[index][palette_id];
            if (path.empty()) {
                out << "null";
            } else {
                out << "\"" << json_escape(path) << "\"";
            }
        }
        out << "]}";
        if (index + 1u !=
            render_context_candidates.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n"
        << "  \"animation_script_candidates\": [\n";
    for (std::size_t index = 0u;
         index < animation_script_candidates.size();
         ++index) {
        const auto& script =
            animation_script_candidates[index];
        const char* classification = "generic";
        if (script.classification ==
            FighterAnimationCandidateClass::frame_sequence_like) {
            classification = "frame_sequence_like";
        } else if (
            script.classification ==
            FighterAnimationCandidateClass::tkc_like) {
            classification = "tkc_like";
        }
        out << "    {\"source_pointer_offset\":"
            << script.source_pointer_offset
            << ",\"target_offset\":"
            << script.target_offset
            << ",\"confidence_score\":"
            << script.confidence_score
            << ",\"classification\":\""
            << classification
            << "\",\"unique_frame_count\":"
            << script.unique_frame_count
            << ",\"command_46_count\":"
            << script.command_46_count
            << ",\"command_8a_count\":"
            << script.command_8a_count
            << ",\"canonical_sequence_root\":"
            << (script.canonical_sequence_root ? "true" : "false")
            << ",\"records\":[";
        for (std::size_t record_index = 0u;
             record_index < script.records.size();
             ++record_index) {
            const auto& record =
                script.records[record_index];
            if (record_index != 0u) out << ",";
            out << "{\"source_offset\":"
                << record.source_offset
                << ",\"command\":"
                << static_cast<unsigned>(record.command)
                << ",\"record_length\":"
                << static_cast<unsigned>(record.record_length)
                << ",\"frame_index\":"
                << record.frame_index
                << "}";
        }
        out << "]}";
        if (index + 1u !=
            animation_script_candidates.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";

    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing fighter native link JSON");
    }
    return Result<std::filesystem::path>::success(out_path);
}

bool fighter_pac_matches(
    std::string_view filename,
    std::string_view fighter_id) {
    const auto upper = ascii_upper(std::string{filename});
    const std::array<std::string, 7> exact{
        "KPLN" + std::string{fighter_id} + ".PAC",
        "PLK" + std::string{fighter_id} + ".PAC",
        "KOP_PL" + std::string{fighter_id} + ".PAC",
        "KACCNT" + std::string{fighter_id} + ".PAC",
        "KACEND" + std::string{fighter_id} + ".PAC",
        "KRA" + std::string{fighter_id} + ".PAC",
        "KSYO" + std::string{fighter_id} + ".PAC",
    };
    return std::find(
        exact.begin(), exact.end(), upper) != exact.end();
}

Result<std::filesystem::path> write_fighter_catalog(
    const std::filesystem::path& output_root,
    const ContentImportSummary& summary) {
    const auto out_path =
        output_root / "derived" / "fighters" / "catalog.json";
    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);
    if (ec) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create fighter catalog directory: " + ec.message());
    }

    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "cannot create fighter catalog");
    }

    out << "{\n"
        << "  \"schema\": 1,\n"
        << "  \"identity_policy\": "
           "\"retail_hex_id_only_until_names_are_proven\",\n"
        << "  \"fighters\": [\n";

    for (std::uint32_t fighter = 0u;
         fighter < 0x1Au;
         ++fighter) {
        std::ostringstream id_stream;
        id_stream << std::uppercase
                  << std::hex
                  << std::setw(2)
                  << std::setfill('0')
                  << fighter;
        const auto id = id_stream.str();

        const auto overlay =
            output_root / "derived" / "fighter_overlay" /
            ("PL" + id + ".json");
        const auto hit =
            output_root / "derived" / "hit" /
            ("PL" + id + "_HIT.json");
        const auto tk =
            output_root / "derived" / "fighter_tk" /
            ("PL" + id + ".json");
        const auto graphics =
            output_root / "derived" / "kpln" /
            ("KPLN" + id) / "graphics.json";
        const auto native_links =
            output_root / "derived" / "fighters" /
            id / "native_links.json";

        out << "    {\n"
            << "      \"id\": \"" << id << "\",\n"
            << "      \"overlay\": ";
        if (std::filesystem::exists(overlay)) {
            out << "\"" << json_escape(
                path_relative_to(overlay, output_root)) << "\"";
        } else {
            out << "null";
        }
        out << ",\n      \"hit_table\": ";
        if (std::filesystem::exists(hit)) {
            out << "\"" << json_escape(
                path_relative_to(hit, output_root)) << "\"";
        } else {
            out << "null";
        }
        out << ",\n      \"tk\": ";
        if (std::filesystem::exists(tk)) {
            out << "\"" << json_escape(
                path_relative_to(tk, output_root)) << "\"";
        } else {
            out << "null";
        }
        out << ",\n      \"graphics\": ";
        if (std::filesystem::exists(graphics)) {
            out << "\"" << json_escape(
                path_relative_to(graphics, output_root)) << "\"";
        } else {
            out << "null";
        }
        out << ",\n      \"native_links\": ";
        if (std::filesystem::exists(native_links)) {
            out << "\"" << json_escape(
                path_relative_to(native_links, output_root)) << "\"";
        } else {
            out << "null";
        }

        out << ",\n      \"packs\": [";
        bool first_pack = true;
        std::vector<std::string> visual_paths;
        for (const auto& entry : summary.entries) {
            if (entry.kind != ContentKind::graphics_pack) continue;
            const auto filename =
                std::filesystem::path{entry.source_path}
                    .filename()
                    .string();
            if (!fighter_pac_matches(filename, id)) continue;
            if (!first_pack) out << ",";
            first_pack = false;
            out << "{\"source\":\""
                << json_escape(entry.source_path)
                << "\",\"derived\":\""
                << json_escape(entry.derived_path)
                << "\"}";

            const auto stem =
                std::filesystem::path{filename}.stem().string();
            const auto tim_directory =
                output_root / "derived" / "tim" / stem;
            std::error_code scan_ec;
            if (std::filesystem::is_directory(tim_directory, scan_ec) &&
                !scan_ec) {
                for (std::filesystem::directory_iterator it(
                         tim_directory, scan_ec), end;
                     !scan_ec && it != end;
                     it.increment(scan_ec)) {
                    if (!it->is_regular_file()) continue;
                    if (ascii_upper(
                            it->path().extension().string()) != ".TGA") {
                        continue;
                    }
                    visual_paths.push_back(
                        path_relative_to(
                            it->path(), output_root));
                }
            }
        }
        std::sort(visual_paths.begin(), visual_paths.end());
        visual_paths.erase(
            std::unique(
                visual_paths.begin(),
                visual_paths.end()),
            visual_paths.end());

        out << "],\n      \"visuals\": [";
        for (std::size_t visual = 0u;
             visual < visual_paths.size();
             ++visual) {
            if (visual != 0u) out << ",";
            out << "\"" << json_escape(visual_paths[visual]) << "\"";
        }
        out << "]\n"
            << "    }";
        if (fighter + 1u != 0x1Au) out << ",";
        out << "\n";
    }

    out << "  ]\n}\n";
    if (!out) {
        return Result<std::filesystem::path>::failure(
            ErrorCode::io_error,
            "failed while writing fighter catalog");
    }
    return Result<std::filesystem::path>::success(out_path);
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

    for (std::uint32_t fighter = 0u;
         fighter < 0x1Au;
         ++fighter) {
        std::ostringstream id_stream;
        id_stream << std::uppercase
                  << std::hex
                  << std::setw(2)
                  << std::setfill('0')
                  << fighter;
        const auto links =
            write_fighter_native_links_json(
                output_root,
                image.value,
                summary,
                id_stream.str());
        if (!links) {
            return Result<ContentImportSummary>::failure(
                links.error,
                "fighter " + id_stream.str() +
                    " native links: " + links.detail);
        }
    }

    const auto fighter_catalog =
        write_fighter_catalog(output_root, summary);
    if (!fighter_catalog) {
        return Result<ContentImportSummary>::failure(
            fighter_catalog.error,
            fighter_catalog.detail);
    }

    const auto manifest = write_manifest(output_root, summary);
    if (!manifest) {
        return Result<ContentImportSummary>::failure(
            manifest.error, manifest.detail);
    }

    return Result<ContentImportSummary>::success(std::move(summary));
}

} // namespace jojo::content
