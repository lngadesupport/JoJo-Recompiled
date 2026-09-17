#include "core/settings.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace jojo {
namespace {

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

Result<int> parse_int(const std::string& text) {
    int value{};
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return Result<int>::failure(ErrorCode::invalid_settings, "invalid integer: " + text);
    }
    return Result<int>::success(value);
}

Result<bool> parse_bool(const std::string& text) {
    if (text == "1" || text == "true") return Result<bool>::success(true);
    if (text == "0" || text == "false") return Result<bool>::success(false);
    return Result<bool>::failure(ErrorCode::invalid_settings, "invalid boolean: " + text);
}

bool valid_filter(TextureFilter f) noexcept {
    switch (f) {
        case TextureFilter::off:
        case TextureFilter::x2:
        case TextureFilter::x4:
        case TextureFilter::x8:
        case TextureFilter::x16: return true;
    }
    return false;
}

bool valid_msaa(Msaa m) noexcept {
    switch (m) {
        case Msaa::off:
        case Msaa::x2:
        case Msaa::x4:
        case Msaa::x8: return true;
    }
    return false;
}

bool valid_display_mode(DisplayMode mode) noexcept {
    switch (mode) {
        case DisplayMode::windowed:
        case DisplayMode::fullscreen:
        case DisplayMode::borderless: return true;
    }
    return false;
}

bool valid_ui_scale(UiScale scale) noexcept {
    switch (scale) {
        case UiScale::automatic:
        case UiScale::percent_75:
        case UiScale::percent_80:
        case UiScale::percent_90:
        case UiScale::percent_100:
        case UiScale::percent_110:
        case UiScale::percent_125:
        case UiScale::percent_150: return true;
    }
    return false;
}

bool valid_hud_safe_area(HudSafeArea area) noexcept {
    switch (area) {
        case HudSafeArea::safe_16_9:
        case HudSafeArea::expanded: return true;
    }
    return false;
}

bool valid_aspect(AspectRatio a) noexcept {
    switch (a) {
        case AspectRatio::ratio_4_3:
        case AspectRatio::ratio_16_9:
        case AspectRatio::ratio_16_10:
        case AspectRatio::ratio_21_9:
        case AspectRatio::ratio_32_9: return true;
    }
    return false;
}

bool valid_binding_kind(BindingKind kind) noexcept {
    switch (kind) {
        case BindingKind::keyboard_key:
        case BindingKind::gamepad_button:
        case BindingKind::gamepad_axis: return true;
    }
    return false;
}

Result<void> replace_file(const std::filesystem::path& temp, const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::io_error, "failed to replace settings file");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) return Result<void>::failure(ErrorCode::io_error, "failed to replace settings file: " + ec.message());
    return Result<void>::success();
#endif
}

Result<void> load_binding_key(InputSettings& input,
                              std::size_t player,
                              std::string_view action_text,
                              const std::string& value) {
    if (player >= input.players.size()) {
        return Result<void>::failure(ErrorCode::invalid_settings, "input player index is outside supported range");
    }
    auto action = game_action_from_string(action_text);
    if (!action) return Result<void>::failure(action.error, action.detail);
    auto binding = parse_binding(value);
    if (!binding) return Result<void>::failure(binding.error, binding.detail);
    input.players[player].bindings[action.value] = std::move(binding.value);
    return Result<void>::success();
}

}

bool validate_graphics(const GraphicsSettings& s) noexcept {
    return s.width >= 640 && s.width <= 7680 && s.height >= 480 && s.height <= 4320 &&
           valid_aspect(s.aspect_ratio) && valid_filter(s.texture_filter) && valid_msaa(s.msaa) &&
           valid_display_mode(s.display_mode) && valid_ui_scale(s.ui_scale) &&
           valid_hud_safe_area(s.hud_safe_area);
}

bool validate_audio(const AudioSettings& s) noexcept {
    const auto valid_volume = [](int value) { return value >= 0 && value <= 100; };
    return valid_volume(s.master_volume) && valid_volume(s.music_volume) && valid_volume(s.effects_volume);
}

bool validate_input(const InputSettings& settings) noexcept {
    for (const auto& player : settings.players) {
        if (player.selected_device.empty()) return false;
        for (const auto& [action, binding] : player.bindings) {
            (void)action;
            if (binding.device_id.empty() || binding.code.empty() || !valid_binding_kind(binding.kind)) return false;
        }
    }
    return true;
}

std::string to_string(AspectRatio value) {
    switch (value) {
        case AspectRatio::ratio_4_3: return "4:3";
        case AspectRatio::ratio_16_9: return "16:9";
        case AspectRatio::ratio_16_10: return "16:10";
        case AspectRatio::ratio_21_9: return "21:9";
        case AspectRatio::ratio_32_9: return "32:9";
    }
    return {};
}

Result<AspectRatio> aspect_ratio_from_string(const std::string& value) {
    if (value == "4:3") return Result<AspectRatio>::success(AspectRatio::ratio_4_3);
    if (value == "16:9") return Result<AspectRatio>::success(AspectRatio::ratio_16_9);
    if (value == "16:10") return Result<AspectRatio>::success(AspectRatio::ratio_16_10);
    if (value == "21:9") return Result<AspectRatio>::success(AspectRatio::ratio_21_9);
    if (value == "32:9") return Result<AspectRatio>::success(AspectRatio::ratio_32_9);
    return Result<AspectRatio>::failure(ErrorCode::invalid_settings, "invalid aspect ratio: " + value);
}

std::string to_string(DisplayMode value) {
    switch (value) {
        case DisplayMode::windowed: return "windowed";
        case DisplayMode::fullscreen: return "fullscreen";
        case DisplayMode::borderless: return "borderless";
    }
    return {};
}

std::string to_string(HudSafeArea value) {
    switch (value) {
        case HudSafeArea::safe_16_9: return "safe_16_9";
        case HudSafeArea::expanded: return "expanded";
    }
    return {};
}

Result<DisplayMode> display_mode_from_string(const std::string& value) {
    if (value == "windowed") return Result<DisplayMode>::success(DisplayMode::windowed);
    if (value == "fullscreen") return Result<DisplayMode>::success(DisplayMode::fullscreen);
    if (value == "borderless") return Result<DisplayMode>::success(DisplayMode::borderless);
    return Result<DisplayMode>::failure(ErrorCode::invalid_settings, "invalid display mode: " + value);
}

Result<HudSafeArea> hud_safe_area_from_string(const std::string& value) {
    if (value == "safe_16_9") return Result<HudSafeArea>::success(HudSafeArea::safe_16_9);
    if (value == "expanded") return Result<HudSafeArea>::success(HudSafeArea::expanded);
    return Result<HudSafeArea>::failure(ErrorCode::invalid_settings, "invalid HUD safe area: " + value);
}

Result<AppSettings> load_settings(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return Result<AppSettings>::failure(ErrorCode::file_not_found, "settings file not found: " + path.string());

    AppSettings result{};
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto key = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));

        if (key == "install_root") result.install_root = value;
        else if (key == "install_dir" && result.install_root.empty()) result.install_root = value;
        else if (key == "selected_device" || key == "selected_device.p1") result.input.players[0].selected_device = value;
        else if (key == "selected_device.p2") result.input.players[1].selected_device = value;
        else if (key.rfind("bind.p1.", 0) == 0) {
            auto loaded = load_binding_key(result.input, 0, key.substr(8), value);
            if (!loaded) return Result<AppSettings>::failure(loaded.error, loaded.detail);
        } else if (key.rfind("bind.p2.", 0) == 0) {
            auto loaded = load_binding_key(result.input, 1, key.substr(8), value);
            if (!loaded) return Result<AppSettings>::failure(loaded.error, loaded.detail);
        } else if (key.rfind("bind.", 0) == 0) {
            auto loaded = load_binding_key(result.input, 0, key.substr(5), value);
            if (!loaded) return Result<AppSettings>::failure(loaded.error, loaded.detail);
        } else if (key == "width") {
            auto p = parse_int(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.width = p.value;
        } else if (key == "height") {
            auto p = parse_int(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.height = p.value;
        } else if (key == "aspect_ratio") {
            auto p = aspect_ratio_from_string(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.aspect_ratio = p.value;
        } else if (key == "texture_filter") {
            auto p = parse_int(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.texture_filter = static_cast<TextureFilter>(p.value);
        } else if (key == "msaa") {
            auto p = parse_int(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.msaa = static_cast<Msaa>(p.value);
        } else if (key == "display_mode") {
            auto p = display_mode_from_string(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.display_mode = p.value;
        } else if (key == "fullscreen") {
            auto p = parse_bool(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail);
            result.graphics.display_mode = p.value ? DisplayMode::fullscreen : DisplayMode::windowed;
        } else if (key == "ui_scale") {
            auto p = parse_int(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.ui_scale = static_cast<UiScale>(p.value);
        } else if (key == "hud_safe_area") {
            auto p = hud_safe_area_from_string(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.hud_safe_area = p.value;
        } else if (key == "vsync") {
            auto p = parse_bool(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.graphics.vsync = p.value;
        } else if (key == "master_volume") {
            auto p = parse_int(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.audio.master_volume = p.value;
        } else if (key == "music_volume") {
            auto p = parse_int(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.audio.music_volume = p.value;
        } else if (key == "effects_volume") {
            auto p = parse_int(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.audio.effects_volume = p.value;
        } else if (key == "mute_when_unfocused") {
            auto p = parse_bool(value); if (!p) return Result<AppSettings>::failure(p.error, p.detail); result.audio.mute_when_unfocused = p.value;
        }
    }

    if (!validate_graphics(result.graphics)) {
        return Result<AppSettings>::failure(ErrorCode::invalid_settings, "graphics settings are outside supported ranges");
    }
    if (!validate_audio(result.audio)) {
        return Result<AppSettings>::failure(ErrorCode::invalid_settings, "audio settings are outside supported ranges");
    }
    if (!validate_input(result.input)) {
        return Result<AppSettings>::failure(ErrorCode::invalid_settings, "input settings are invalid");
    }
    return Result<AppSettings>::success(std::move(result));
}

Result<void> save_settings_atomic(const std::filesystem::path& path, const AppSettings& settings) {
    if (!validate_graphics(settings.graphics)) {
        return Result<void>::failure(ErrorCode::invalid_settings, "graphics settings are outside supported ranges");
    }
    if (!validate_audio(settings.audio)) {
        return Result<void>::failure(ErrorCode::invalid_settings, "audio settings are outside supported ranges");
    }
    if (!validate_input(settings.input)) {
        return Result<void>::failure(ErrorCode::invalid_settings, "input settings are invalid");
    }
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return Result<void>::failure(ErrorCode::io_error, "failed to create settings directory: " + ec.message());

    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) return Result<void>::failure(ErrorCode::io_error, "failed to open temporary settings file");
        out << "# JOJO Recompiled settings\n";
        out << "install_root=" << settings.install_root << '\n';
        out << "width=" << settings.graphics.width << '\n';
        out << "height=" << settings.graphics.height << '\n';
        out << "aspect_ratio=" << to_string(settings.graphics.aspect_ratio) << '\n';
        out << "texture_filter=" << static_cast<int>(settings.graphics.texture_filter) << '\n';
        out << "msaa=" << static_cast<int>(settings.graphics.msaa) << '\n';
        out << "display_mode=" << to_string(settings.graphics.display_mode) << '\n';
        out << "vsync=" << (settings.graphics.vsync ? 1 : 0) << '\n';
        out << "ui_scale=" << static_cast<int>(settings.graphics.ui_scale) << '\n';
        out << "hud_safe_area=" << to_string(settings.graphics.hud_safe_area) << '\n';
        out << "master_volume=" << settings.audio.master_volume << '\n';
        out << "music_volume=" << settings.audio.music_volume << '\n';
        out << "effects_volume=" << settings.audio.effects_volume << '\n';
        out << "mute_when_unfocused=" << (settings.audio.mute_when_unfocused ? 1 : 0) << '\n';
        for (std::size_t player = 0; player < settings.input.players.size(); ++player) {
            out << "selected_device.p" << (player + 1) << '=' << settings.input.players[player].selected_device << '\n';
            for (const auto action : all_game_actions()) {
                const auto it = settings.input.players[player].bindings.find(action);
                if (it != settings.input.players[player].bindings.end()) {
                    out << "bind.p" << (player + 1) << '.' << to_string(action) << '=' << serialize_binding(it->second) << '\n';
                }
            }
        }
        out.flush();
        if (!out) return Result<void>::failure(ErrorCode::io_error, "failed while writing settings");
    }
    return replace_file(temp, path);
}

}
