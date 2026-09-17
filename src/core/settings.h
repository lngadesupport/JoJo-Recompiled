#pragma once
#include "core/result.h"
#include "core/input.h"
#include <filesystem>
#include <string>

namespace jojo {

enum class AspectRatio { ratio_4_3, ratio_16_9, ratio_16_10, ratio_21_9, ratio_32_9 };
enum class TextureFilter : int { off = 0, x2 = 2, x4 = 4, x8 = 8, x16 = 16 };
enum class Msaa : int { off = 0, x2 = 2, x4 = 4, x8 = 8 };
enum class DisplayMode { windowed, fullscreen, borderless };
enum class UiScale : int {
    automatic = 0,
    percent_75 = 75,
    percent_80 = 80,
    percent_90 = 90,
    percent_100 = 100,
    percent_110 = 110,
    percent_125 = 125,
    percent_150 = 150
};
enum class HudSafeArea { safe_16_9, expanded };

struct GraphicsSettings {
    int width{1920};
    int height{1080};
    AspectRatio aspect_ratio{AspectRatio::ratio_16_9};
    TextureFilter texture_filter{TextureFilter::x16};
    Msaa msaa{Msaa::x4};
    DisplayMode display_mode{DisplayMode::windowed};
    bool vsync{true};
    UiScale ui_scale{UiScale::automatic};
    HudSafeArea hud_safe_area{HudSafeArea::safe_16_9};
    friend bool operator==(const GraphicsSettings&, const GraphicsSettings&) = default;
};

struct AudioSettings {
    int master_volume{100};
    int music_volume{100};
    int effects_volume{100};
    bool mute_when_unfocused{false};
    friend bool operator==(const AudioSettings&, const AudioSettings&) = default;
};

struct AppSettings {
    std::string install_root{};
    GraphicsSettings graphics{};
    AudioSettings audio{};
    InputSettings input{};
};

[[nodiscard]] bool validate_graphics(const GraphicsSettings& settings) noexcept;
[[nodiscard]] bool validate_audio(const AudioSettings& settings) noexcept;
[[nodiscard]] bool validate_input(const InputSettings& settings) noexcept;
[[nodiscard]] std::string to_string(AspectRatio value);
[[nodiscard]] std::string to_string(DisplayMode value);
[[nodiscard]] std::string to_string(HudSafeArea value);
[[nodiscard]] Result<AspectRatio> aspect_ratio_from_string(const std::string& value);
[[nodiscard]] Result<DisplayMode> display_mode_from_string(const std::string& value);
[[nodiscard]] Result<HudSafeArea> hud_safe_area_from_string(const std::string& value);
[[nodiscard]] Result<AppSettings> load_settings(const std::filesystem::path& path);
[[nodiscard]] Result<void> save_settings_atomic(const std::filesystem::path& path, const AppSettings& settings);

}
