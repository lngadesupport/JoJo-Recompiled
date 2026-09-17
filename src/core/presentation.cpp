#include "core/presentation.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace jojo {
namespace {

struct Ratio {
    std::uint32_t numerator{};
    std::uint32_t denominator{};
};

constexpr Ratio ratio_for(AspectRatio aspect) noexcept {
    switch (aspect) {
        case AspectRatio::ratio_4_3: return {4u, 3u};
        case AspectRatio::ratio_16_9: return {16u, 9u};
        case AspectRatio::ratio_16_10: return {16u, 10u};
        case AspectRatio::ratio_21_9: return {21u, 9u};
        case AspectRatio::ratio_32_9: return {32u, 9u};
    }
    return {};
}

RectI fit_aspect(Extent2D output, Ratio ratio) noexcept {
    const std::uint64_t lhs = static_cast<std::uint64_t>(output.width) * ratio.denominator;
    const std::uint64_t rhs = static_cast<std::uint64_t>(output.height) * ratio.numerator;

    if (lhs >= rhs) {
        const auto width = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(output.height) * ratio.numerator) / ratio.denominator);
        return {
            static_cast<std::int32_t>((output.width - width) / 2u),
            0,
            width,
            output.height,
        };
    }

    const auto height = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(output.width) * ratio.denominator) / ratio.numerator);
    return {
        0,
        static_cast<std::int32_t>((output.height - height) / 2u),
        output.width,
        height,
    };
}

int filter_rank(TextureFilter filter) noexcept {
    switch (filter) {
        case TextureFilter::off: return 0;
        case TextureFilter::x2: return 2;
        case TextureFilter::x4: return 4;
        case TextureFilter::x8: return 8;
        case TextureFilter::x16: return 16;
    }
    return -1;
}

int msaa_rank(Msaa mode) noexcept {
    switch (mode) {
        case Msaa::off: return 0;
        case Msaa::x2: return 2;
        case Msaa::x4: return 4;
        case Msaa::x8: return 8;
    }
    return -1;
}

std::optional<TextureFilter> best_filter(TextureFilter requested,
                                         const std::vector<TextureFilter>& supported) noexcept {
    const int requested_rank = filter_rank(requested);
    int best_rank = -1;
    std::optional<TextureFilter> best;
    for (const auto candidate : supported) {
        const int rank = filter_rank(candidate);
        if (rank >= 0 && rank <= requested_rank && rank > best_rank) {
            best_rank = rank;
            best = candidate;
        }
    }
    return best;
}

std::optional<Msaa> best_msaa(Msaa requested,
                              const std::vector<Msaa>& supported) noexcept {
    const int requested_rank = msaa_rank(requested);
    int best_rank = -1;
    std::optional<Msaa> best;
    for (const auto candidate : supported) {
        const int rank = msaa_rank(candidate);
        if (rank >= 0 && rank <= requested_rank && rank > best_rank) {
            best_rank = rank;
            best = candidate;
        }
    }
    return best;
}

double ui_element_scale(UiScale setting, std::uint32_t dpi) noexcept {
    if (setting == UiScale::automatic) {
        return std::clamp(static_cast<double>(dpi) / 96.0, 0.75, 1.5);
    }
    return static_cast<double>(static_cast<int>(setting)) / 100.0;
}

UiLayout make_ui_layout(const GraphicsSettings& graphics,
                        std::uint32_t dpi,
                        const RectI& viewport) noexcept {
    const Ratio camera_ratio = ratio_for(graphics.aspect_ratio);
    const Ratio safe_16_9{16u, 9u};
    Ratio ui_ratio = camera_ratio;

    RectI safe = viewport;
    const bool wider_than_16_9 =
        static_cast<std::uint64_t>(camera_ratio.numerator) * safe_16_9.denominator >
        static_cast<std::uint64_t>(safe_16_9.numerator) * camera_ratio.denominator;

    if (graphics.hud_safe_area == HudSafeArea::safe_16_9 && wider_than_16_9) {
        const auto local = fit_aspect({viewport.width, viewport.height}, safe_16_9);
        safe = {
            viewport.x + local.x,
            viewport.y + local.y,
            local.width,
            local.height,
        };
        ui_ratio = safe_16_9;
    }

    constexpr std::uint32_t logical_height = 1080u;
    const auto logical_width = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(logical_height) * ui_ratio.numerator) / ui_ratio.denominator);

    return {
        safe,
        {logical_width, logical_height},
        static_cast<double>(safe.height) / static_cast<double>(logical_height),
        ui_element_scale(graphics.ui_scale, dpi),
    };
}

}

double aspect_ratio_value(AspectRatio aspect) noexcept {
    const auto ratio = ratio_for(aspect);
    if (ratio.denominator == 0u) return 0.0;
    return static_cast<double>(ratio.numerator) / static_cast<double>(ratio.denominator);
}

CameraAspectPolicy camera_policy_for(AspectRatio aspect) noexcept {
    const double target = aspect_ratio_value(aspect);
    constexpr double base = 4.0 / 3.0;
    if (target <= 0.0) return {};
    return {
        target,
        target / base,
        1.0,
        true,
    };
}

Result<PresentationPlan> build_presentation_plan(
    const GraphicsSettings& graphics,
    const PresentationInputs& inputs,
    const RendererCapabilities& capabilities) {
    if (!validate_graphics(graphics)) {
        return Result<PresentationPlan>::failure(
            ErrorCode::invalid_settings,
            "graphics settings are outside supported ranges");
    }
    if (inputs.simulation_resolution.width == 0u || inputs.simulation_resolution.height == 0u) {
        return Result<PresentationPlan>::failure(
            ErrorCode::invalid_argument,
            "simulation resolution must be non-zero");
    }
    if (inputs.desktop_resolution.width == 0u || inputs.desktop_resolution.height == 0u) {
        return Result<PresentationPlan>::failure(
            ErrorCode::invalid_argument,
            "desktop resolution must be non-zero");
    }
    if (inputs.dpi == 0u) {
        return Result<PresentationPlan>::failure(
            ErrorCode::invalid_argument,
            "display DPI must be non-zero");
    }

    const auto filter = best_filter(graphics.texture_filter, capabilities.texture_filters);
    if (!filter) {
        return Result<PresentationPlan>::failure(
            ErrorCode::invalid_argument,
            "renderer capabilities do not provide a usable texture-filter mode");
    }
    const auto msaa = best_msaa(graphics.msaa, capabilities.msaa_modes);
    if (!msaa) {
        return Result<PresentationPlan>::failure(
            ErrorCode::invalid_argument,
            "renderer capabilities do not provide a usable MSAA mode");
    }

    PresentationPlan plan{};
    plan.simulation_resolution = inputs.simulation_resolution;
    plan.requested_display_mode = graphics.display_mode;
    plan.applied_display_mode = graphics.display_mode;

    switch (graphics.display_mode) {
        case DisplayMode::windowed:
            plan.presentation_resolution = {
                static_cast<std::uint32_t>(graphics.width),
                static_cast<std::uint32_t>(graphics.height),
            };
            break;
        case DisplayMode::borderless:
            plan.presentation_resolution = inputs.desktop_resolution;
            break;
        case DisplayMode::fullscreen:
            if (capabilities.exclusive_fullscreen) {
                plan.presentation_resolution = {
                    static_cast<std::uint32_t>(graphics.width),
                    static_cast<std::uint32_t>(graphics.height),
                };
                plan.exclusive_fullscreen = true;
            } else {
                plan.applied_display_mode = DisplayMode::borderless;
                plan.presentation_resolution = inputs.desktop_resolution;
                plan.display_mode_fallback = true;
            }
            break;
    }

    plan.camera = camera_policy_for(graphics.aspect_ratio);
    plan.viewport = fit_aspect(plan.presentation_resolution, ratio_for(graphics.aspect_ratio));
    plan.ui = make_ui_layout(graphics, inputs.dpi, plan.viewport);

    plan.requested_texture_filter = graphics.texture_filter;
    plan.applied_texture_filter = *filter;
    plan.texture_filter_fallback = plan.applied_texture_filter != plan.requested_texture_filter;

    plan.requested_msaa = graphics.msaa;
    plan.applied_msaa = *msaa;
    plan.msaa_fallback = plan.applied_msaa != plan.requested_msaa;

    plan.uniform_presentation_scale = true;
    return Result<PresentationPlan>::success(std::move(plan));
}

}
