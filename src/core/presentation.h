#pragma once

#include "core/result.h"
#include "core/settings.h"

#include <cstdint>
#include <vector>

namespace jojo {

struct Extent2D {
    std::uint32_t width{};
    std::uint32_t height{};
    friend bool operator==(const Extent2D&, const Extent2D&) = default;
};

struct RectI {
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
    friend bool operator==(const RectI&, const RectI&) = default;
};

struct RendererCapabilities {
    bool exclusive_fullscreen{};
    std::vector<TextureFilter> texture_filters{TextureFilter::off};
    std::vector<Msaa> msaa_modes{Msaa::off};
};

struct PresentationInputs {
    Extent2D simulation_resolution{640u, 480u};
    Extent2D desktop_resolution{1920u, 1080u};
    std::uint32_t dpi{96u};
};

struct CameraAspectPolicy {
    double target_aspect{};
    double horizontal_extent_scale{1.0};
    double vertical_extent_scale{1.0};
    bool preserve_vertical_extent{true};
};

struct UiLayout {
    RectI safe_rect{};
    Extent2D logical_resolution{};
    double logical_to_pixel_scale{1.0};
    double element_scale{1.0};
};

struct PresentationPlan {
    Extent2D simulation_resolution{};
    Extent2D presentation_resolution{};
    RectI viewport{};
    CameraAspectPolicy camera{};
    UiLayout ui{};

    DisplayMode requested_display_mode{DisplayMode::windowed};
    DisplayMode applied_display_mode{DisplayMode::windowed};
    bool exclusive_fullscreen{};
    bool display_mode_fallback{};

    TextureFilter requested_texture_filter{TextureFilter::off};
    TextureFilter applied_texture_filter{TextureFilter::off};
    bool texture_filter_fallback{};

    Msaa requested_msaa{Msaa::off};
    Msaa applied_msaa{Msaa::off};
    bool msaa_fallback{};

    bool uniform_presentation_scale{true};
};

[[nodiscard]] double aspect_ratio_value(AspectRatio aspect) noexcept;
[[nodiscard]] CameraAspectPolicy camera_policy_for(AspectRatio aspect) noexcept;

[[nodiscard]] Result<PresentationPlan> build_presentation_plan(
    const GraphicsSettings& graphics,
    const PresentationInputs& inputs,
    const RendererCapabilities& capabilities);

}
