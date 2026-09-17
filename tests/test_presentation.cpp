#include "core/presentation.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)

bool near(double a, double b, double eps = 0.001) {
    return std::fabs(a - b) <= eps;
}

jojo::RendererCapabilities full_caps() {
    jojo::RendererCapabilities caps{};
    caps.exclusive_fullscreen = true;
    caps.texture_filters = {
        jojo::TextureFilter::off,
        jojo::TextureFilter::x2,
        jojo::TextureFilter::x4,
        jojo::TextureFilter::x8,
        jojo::TextureFilter::x16,
    };
    caps.msaa_modes = {
        jojo::Msaa::off,
        jojo::Msaa::x2,
        jojo::Msaa::x4,
        jojo::Msaa::x8,
    };
    return caps;
}

void test_simulation_resolution_is_independent_from_presentation() {
    jojo::GraphicsSettings graphics{};
    graphics.width = 3840;
    graphics.height = 2160;
    graphics.aspect_ratio = jojo::AspectRatio::ratio_16_9;

    jojo::PresentationInputs inputs{};
    inputs.simulation_resolution = {640, 480};
    inputs.desktop_resolution = {5120, 2880};
    inputs.dpi = 96;

    const auto plan = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(plan);
    if (!plan) return;
    CHECK(plan.value.simulation_resolution == jojo::Extent2D{640, 480});
    CHECK(plan.value.presentation_resolution == jojo::Extent2D{3840, 2160});
    CHECK(plan.value.viewport == jojo::RectI{0, 0, 3840, 2160});

    inputs.simulation_resolution = {320, 240};
    const auto second = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(second);
    if (!second) return;
    CHECK(second.value.simulation_resolution == jojo::Extent2D{320, 240});
    CHECK(second.value.presentation_resolution == plan.value.presentation_resolution);
    CHECK(second.value.viewport == plan.value.viewport);
}

void test_aspect_viewports_are_centered_without_non_uniform_stretch() {
    struct Case {
        jojo::AspectRatio aspect;
        jojo::Extent2D output;
        jojo::RectI expected;
    };
    const std::vector<Case> cases{
        {jojo::AspectRatio::ratio_4_3,   {1920, 1080}, {240, 0, 1440, 1080}},
        {jojo::AspectRatio::ratio_16_9,  {3440, 1440}, {440, 0, 2560, 1440}},
        {jojo::AspectRatio::ratio_16_10, {1920, 1200}, {0, 0, 1920, 1200}},
        {jojo::AspectRatio::ratio_21_9,  {3440, 1440}, {40, 0, 3360, 1440}},
        {jojo::AspectRatio::ratio_32_9,  {5120, 1440}, {0, 0, 5120, 1440}},
    };

    for (const auto& tc : cases) {
        jojo::GraphicsSettings graphics{};
        graphics.width = static_cast<int>(tc.output.width);
        graphics.height = static_cast<int>(tc.output.height);
        graphics.aspect_ratio = tc.aspect;
        jojo::PresentationInputs inputs{};
        inputs.desktop_resolution = tc.output;
        const auto plan = jojo::build_presentation_plan(graphics, inputs, full_caps());
        CHECK(plan);
        if (!plan) continue;
        CHECK(plan.value.viewport == tc.expected);
        const double viewport_aspect = static_cast<double>(plan.value.viewport.width) /
                                       static_cast<double>(plan.value.viewport.height);
        CHECK(near(viewport_aspect, jojo::aspect_ratio_value(tc.aspect), 0.002));
        CHECK(plan.value.uniform_presentation_scale);
    }
}

void test_camera_policy_preserves_vertical_extent_and_expands_horizontally() {
    struct Case { jojo::AspectRatio aspect; double expansion; };
    const std::vector<Case> cases{
        {jojo::AspectRatio::ratio_4_3, 1.0},
        {jojo::AspectRatio::ratio_16_9, 4.0 / 3.0},
        {jojo::AspectRatio::ratio_16_10, 1.2},
        {jojo::AspectRatio::ratio_21_9, 1.75},
        {jojo::AspectRatio::ratio_32_9, 8.0 / 3.0},
    };
    for (const auto& tc : cases) {
        const auto camera = jojo::camera_policy_for(tc.aspect);
        CHECK(camera.preserve_vertical_extent);
        CHECK(near(camera.vertical_extent_scale, 1.0));
        CHECK(near(camera.horizontal_extent_scale, tc.expansion));
        CHECK(near(camera.target_aspect, jojo::aspect_ratio_value(tc.aspect)));
    }
}

void test_ui_safe_area_and_logical_coordinates_scale_from_480p_to_8k() {
    jojo::GraphicsSettings graphics{};
    graphics.aspect_ratio = jojo::AspectRatio::ratio_21_9;
    graphics.hud_safe_area = jojo::HudSafeArea::safe_16_9;
    graphics.ui_scale = jojo::UiScale::percent_100;

    jojo::PresentationInputs inputs{};
    inputs.desktop_resolution = {3440, 1440};
    graphics.width = 3440;
    graphics.height = 1440;
    const auto ultrawide = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(ultrawide);
    if (ultrawide) {
        CHECK(ultrawide.value.viewport == jojo::RectI{40, 0, 3360, 1440});
        CHECK(ultrawide.value.ui.safe_rect == jojo::RectI{440, 0, 2560, 1440});
        CHECK(ultrawide.value.ui.logical_resolution == jojo::Extent2D{1920, 1080});
        CHECK(near(ultrawide.value.ui.logical_to_pixel_scale, 4.0 / 3.0));
        CHECK(near(ultrawide.value.ui.element_scale, 1.0));
    }

    graphics.aspect_ratio = jojo::AspectRatio::ratio_4_3;
    graphics.width = 640;
    graphics.height = 480;
    inputs.desktop_resolution = {640, 480};
    const auto p480 = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(p480);
    if (p480) {
        CHECK(p480.value.ui.safe_rect == jojo::RectI{0, 0, 640, 480});
        CHECK(p480.value.ui.logical_resolution == jojo::Extent2D{1440, 1080});
        CHECK(near(p480.value.ui.logical_to_pixel_scale, 480.0 / 1080.0));
    }

    graphics.aspect_ratio = jojo::AspectRatio::ratio_16_9;
    graphics.width = 7680;
    graphics.height = 4320;
    inputs.desktop_resolution = {7680, 4320};
    const auto p8k = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(p8k);
    if (p8k) {
        CHECK(p8k.value.ui.logical_resolution == jojo::Extent2D{1920, 1080});
        CHECK(near(p8k.value.ui.logical_to_pixel_scale, 4.0));
    }
}

void test_ui_auto_scale_is_dpi_aware_and_expanded_hud_uses_full_aspect() {
    jojo::GraphicsSettings graphics{};
    graphics.width = 3440;
    graphics.height = 1440;
    graphics.aspect_ratio = jojo::AspectRatio::ratio_21_9;
    graphics.hud_safe_area = jojo::HudSafeArea::expanded;
    graphics.ui_scale = jojo::UiScale::automatic;

    jojo::PresentationInputs inputs{};
    inputs.desktop_resolution = {3440, 1440};
    inputs.dpi = 144;
    const auto plan = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(plan);
    if (!plan) return;
    CHECK(plan.value.ui.safe_rect == plan.value.viewport);
    CHECK(plan.value.ui.logical_resolution == jojo::Extent2D{2520, 1080});
    CHECK(near(plan.value.ui.element_scale, 1.5));

    inputs.dpi = 192;
    const auto clamped = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(clamped);
    if (clamped) CHECK(near(clamped.value.ui.element_scale, 1.5));

    graphics.ui_scale = jojo::UiScale::percent_80;
    const auto explicit_scale = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(explicit_scale);
    if (explicit_scale) CHECK(near(explicit_scale.value.ui.element_scale, 0.8));
}

void test_quality_modes_use_requested_value_when_supported_and_fallback_downward() {
    jojo::GraphicsSettings graphics{};
    graphics.texture_filter = jojo::TextureFilter::x16;
    graphics.msaa = jojo::Msaa::x8;
    jojo::PresentationInputs inputs{};

    auto caps = full_caps();
    const auto exact = jojo::build_presentation_plan(graphics, inputs, caps);
    CHECK(exact);
    if (exact) {
        CHECK(exact.value.applied_texture_filter == jojo::TextureFilter::x16);
        CHECK(exact.value.applied_msaa == jojo::Msaa::x8);
        CHECK(!exact.value.texture_filter_fallback);
        CHECK(!exact.value.msaa_fallback);
    }

    caps.texture_filters = {jojo::TextureFilter::off, jojo::TextureFilter::x2, jojo::TextureFilter::x8};
    caps.msaa_modes = {jojo::Msaa::off, jojo::Msaa::x2, jojo::Msaa::x4};
    const auto fallback = jojo::build_presentation_plan(graphics, inputs, caps);
    CHECK(fallback);
    if (fallback) {
        CHECK(fallback.value.applied_texture_filter == jojo::TextureFilter::x8);
        CHECK(fallback.value.applied_msaa == jojo::Msaa::x4);
        CHECK(fallback.value.texture_filter_fallback);
        CHECK(fallback.value.msaa_fallback);
    }

    graphics.texture_filter = jojo::TextureFilter::x4;
    caps.texture_filters = {jojo::TextureFilter::off, jojo::TextureFilter::x2, jojo::TextureFilter::x8};
    const auto non_contiguous = jojo::build_presentation_plan(graphics, inputs, caps);
    CHECK(non_contiguous);
    if (non_contiguous) CHECK(non_contiguous.value.applied_texture_filter == jojo::TextureFilter::x2);
}

void test_display_modes_cover_windowed_borderless_and_exclusive_fallback() {
    jojo::GraphicsSettings graphics{};
    graphics.width = 2560;
    graphics.height = 1440;
    jojo::PresentationInputs inputs{};
    inputs.desktop_resolution = {3840, 2160};

    auto caps = full_caps();
    graphics.display_mode = jojo::DisplayMode::windowed;
    auto plan = jojo::build_presentation_plan(graphics, inputs, caps);
    CHECK(plan);
    if (plan) {
        CHECK(plan.value.applied_display_mode == jojo::DisplayMode::windowed);
        CHECK(plan.value.presentation_resolution == jojo::Extent2D{2560, 1440});
        CHECK(!plan.value.exclusive_fullscreen);
        CHECK(!plan.value.display_mode_fallback);
    }

    graphics.display_mode = jojo::DisplayMode::borderless;
    plan = jojo::build_presentation_plan(graphics, inputs, caps);
    CHECK(plan);
    if (plan) {
        CHECK(plan.value.applied_display_mode == jojo::DisplayMode::borderless);
        CHECK(plan.value.presentation_resolution == inputs.desktop_resolution);
        CHECK(!plan.value.exclusive_fullscreen);
    }

    graphics.display_mode = jojo::DisplayMode::fullscreen;
    plan = jojo::build_presentation_plan(graphics, inputs, caps);
    CHECK(plan);
    if (plan) {
        CHECK(plan.value.applied_display_mode == jojo::DisplayMode::fullscreen);
        CHECK(plan.value.presentation_resolution == jojo::Extent2D{2560, 1440});
        CHECK(plan.value.exclusive_fullscreen);
        CHECK(!plan.value.display_mode_fallback);
    }

    caps.exclusive_fullscreen = false;
    plan = jojo::build_presentation_plan(graphics, inputs, caps);
    CHECK(plan);
    if (plan) {
        CHECK(plan.value.applied_display_mode == jojo::DisplayMode::borderless);
        CHECK(plan.value.presentation_resolution == inputs.desktop_resolution);
        CHECK(!plan.value.exclusive_fullscreen);
        CHECK(plan.value.display_mode_fallback);
    }
}

void test_invalid_presentation_inputs_are_rejected() {
    jojo::GraphicsSettings graphics{};
    jojo::PresentationInputs inputs{};
    inputs.simulation_resolution = {0, 480};
    auto result = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(!result);
    CHECK(result.error == jojo::ErrorCode::invalid_argument);

    inputs.simulation_resolution = {640, 480};
    inputs.desktop_resolution = {0, 1080};
    result = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(!result);
    CHECK(result.error == jojo::ErrorCode::invalid_argument);

    inputs.desktop_resolution = {1920, 1080};
    inputs.dpi = 0;
    result = jojo::build_presentation_plan(graphics, inputs, full_caps());
    CHECK(!result);
    CHECK(result.error == jojo::ErrorCode::invalid_argument);
}
}

int main() {
    test_simulation_resolution_is_independent_from_presentation();
    test_aspect_viewports_are_centered_without_non_uniform_stretch();
    test_camera_policy_preserves_vertical_extent_and_expands_horizontally();
    test_ui_safe_area_and_logical_coordinates_scale_from_480p_to_8k();
    test_ui_auto_scale_is_dpi_aware_and_expanded_hud_uses_full_aspect();
    test_quality_modes_use_requested_value_when_supported_and_fallback_downward();
    test_display_modes_cover_windowed_borderless_and_exclusive_fallback();
    test_invalid_presentation_inputs_are_rejected();
    if (failures != 0) {
        std::cerr << failures << " presentation test(s) failed\n";
        return 1;
    }
    std::cout << "presentation tests passed\n";
    return 0;
}
