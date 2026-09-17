#ifdef _WIN32
#define NOMINMAX
#include "app_win32/presentation_host.h"
#include "core/ps1_display_frame.h"

#include <algorithm>
#include <cstring>
#include <d3d11.h>
#include <dxgiformat.h>
#include <iostream>
#include <windows.h>

namespace {
int failures = 0;
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)

template <typename T>
bool contains(const std::vector<T>& values, T value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

jojo::PresentationPlan base_plan() {
    jojo::PresentationPlan plan{};
    plan.presentation_resolution = {2560, 1440};
    plan.applied_display_mode = jojo::DisplayMode::windowed;
    return plan;
}

void test_windowed_plan_is_decorated_and_uses_requested_client_size() {
    auto presentation = base_plan();
    const RECT monitor{0, 0, 3840, 2160};
    const auto plan = jojo::make_win32_window_plan(presentation, monitor, 144u);
    CHECK(plan);
    if (!plan) return;
    CHECK((plan.value.style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW);
    CHECK((plan.value.style & WS_POPUP) == 0u);
    CHECK(plan.value.client_resolution == jojo::Extent2D{2560, 1440});
    CHECK(!plan.value.cover_monitor);
    CHECK(!plan.value.switch_display_mode);
    CHECK(!plan.value.exclusive_fullscreen);
}

void test_borderless_plan_covers_monitor_without_switching_display_mode() {
    auto presentation = base_plan();
    presentation.applied_display_mode = jojo::DisplayMode::borderless;
    presentation.presentation_resolution = {3840, 2160};
    const RECT monitor{100, 50, 3940, 2210};
    const auto plan = jojo::make_win32_window_plan(presentation, monitor, 96u);
    CHECK(plan);
    if (!plan) return;
    CHECK((plan.value.style & WS_POPUP) != 0u);
    CHECK((plan.value.style & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW);
    CHECK(plan.value.cover_monitor);
    CHECK(!plan.value.switch_display_mode);
    CHECK(!plan.value.exclusive_fullscreen);
    CHECK(plan.value.window_rect.left == monitor.left);
    CHECK(plan.value.window_rect.top == monitor.top);
    CHECK(plan.value.window_rect.right == monitor.right);
    CHECK(plan.value.window_rect.bottom == monitor.bottom);
}

void test_exclusive_plan_requests_display_switch_and_popup_surface() {
    auto presentation = base_plan();
    presentation.applied_display_mode = jojo::DisplayMode::fullscreen;
    presentation.exclusive_fullscreen = true;
    const RECT monitor{0, 0, 3840, 2160};
    const auto plan = jojo::make_win32_window_plan(presentation, monitor, 96u);
    CHECK(plan);
    if (!plan) return;
    CHECK((plan.value.style & WS_POPUP) != 0u);
    CHECK(plan.value.switch_display_mode);
    CHECK(plan.value.exclusive_fullscreen);
    CHECK(plan.value.display_width == 2560u);
    CHECK(plan.value.display_height == 1440u);
}

void test_invalid_monitor_or_dpi_is_rejected() {
    auto presentation = base_plan();
    auto result = jojo::make_win32_window_plan(presentation, RECT{0, 0, 0, 1080}, 96u);
    CHECK(!result);
    CHECK(result.error == jojo::ErrorCode::invalid_argument);
    result = jojo::make_win32_window_plan(presentation, RECT{0, 0, 1920, 1080}, 0u);
    CHECK(!result);
    CHECK(result.error == jojo::ErrorCode::invalid_argument);
}

void test_d3d11_probe_reports_real_device_quality_capabilities() {
    const auto probed = jojo::probe_d3d11_renderer_capabilities();
    CHECK(probed);
    if (!probed) return;
    CHECK(probed.value.exclusive_fullscreen);
    CHECK(contains(probed.value.texture_filters, jojo::TextureFilter::off));
    CHECK(contains(probed.value.texture_filters, jojo::TextureFilter::x2));
    CHECK(contains(probed.value.texture_filters, jojo::TextureFilter::x4));
    CHECK(contains(probed.value.texture_filters, jojo::TextureFilter::x8));
    CHECK(contains(probed.value.texture_filters, jojo::TextureFilter::x16));
    CHECK(contains(probed.value.msaa_modes, jojo::Msaa::off));
    for (const auto mode : probed.value.msaa_modes) {
        CHECK(mode == jojo::Msaa::off || mode == jojo::Msaa::x2 ||
              mode == jojo::Msaa::x4 || mode == jojo::Msaa::x8);
    }
}

void test_d3d11_ps1_frame_upload_plan_is_tightly_packed_rgba8() {
    jojo::Ps1DisplayFrame frame{};
    frame.width = 2u;
    frame.height = 1u;
    frame.rgba8 = {0xFF0000FFu, 0xFF00FF00u};

    const auto plan = jojo::make_d3d11_frame_upload_plan(frame);
    CHECK(plan);
    if (!plan) return;
    CHECK(plan.value.width == 2u);
    CHECK(plan.value.height == 1u);
    CHECK(plan.value.row_pitch == 8u);
    CHECK(plan.value.byte_size == 8u);
    CHECK(plan.value.format == DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(plan.value.pixels == frame.rgba8.data());
}

void test_d3d11_ps1_frame_upload_plan_rejects_malformed_storage() {
    jojo::Ps1DisplayFrame frame{};
    frame.width = 2u;
    frame.height = 2u;
    frame.rgba8 = {0xFFFFFFFFu};
    const auto plan = jojo::make_d3d11_frame_upload_plan(frame);
    CHECK(!plan);
    CHECK(plan.error == jojo::ErrorCode::invalid_argument);
}

void test_d3d11_ps1_frame_upload_round_trips_pixels() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL feature_level{};
    const HRESULT create_hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0u,
        D3D11_SDK_VERSION,
        &device,
        &feature_level,
        &context);
    CHECK(SUCCEEDED(create_hr));
    CHECK(device != nullptr);
    CHECK(context != nullptr);
    if (FAILED(create_hr) || !device || !context) {
        if (context) context->Release();
        if (device) device->Release();
        return;
    }

    jojo::Ps1DisplayFrame frame{};
    frame.width = 2u;
    frame.height = 1u;
    frame.rgba8 = {0xFF0000FFu, 0xFF00FF00u};

    ID3D11Texture2D* texture = nullptr;
    const auto uploaded = jojo::upload_d3d11_ps1_frame(device, context, frame, &texture);
    CHECK(uploaded);
    CHECK(texture != nullptr);
    if (!uploaded || !texture) {
        if (texture) texture->Release();
        context->Release();
        device->Release();
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    CHECK(desc.Width == 2u);
    CHECK(desc.Height == 1u);
    CHECK(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(desc.MipLevels == 1u);
    CHECK(desc.ArraySize == 1u);
    CHECK(desc.SampleDesc.Count == 1u);
    CHECK((desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0u);

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0u;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0u;

    ID3D11Texture2D* staging = nullptr;
    const HRESULT staging_hr = device->CreateTexture2D(&staging_desc, nullptr, &staging);
    CHECK(SUCCEEDED(staging_hr));
    CHECK(staging != nullptr);
    if (SUCCEEDED(staging_hr) && staging) {
        context->CopyResource(staging, texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT map_hr = context->Map(staging, 0u, D3D11_MAP_READ, 0u, &mapped);
        CHECK(SUCCEEDED(map_hr));
        if (SUCCEEDED(map_hr)) {
            CHECK(mapped.RowPitch >= 8u);
            std::uint32_t pixels[2]{};
            std::memcpy(pixels, mapped.pData, sizeof(pixels));
            CHECK(pixels[0] == frame.rgba8[0]);
            CHECK(pixels[1] == frame.rgba8[1]);
            context->Unmap(staging, 0u);
        }
        staging->Release();
    }

    texture->Release();
    context->Release();
    device->Release();
}
}

int main() {
    test_windowed_plan_is_decorated_and_uses_requested_client_size();
    test_borderless_plan_covers_monitor_without_switching_display_mode();
    test_exclusive_plan_requests_display_switch_and_popup_surface();
    test_invalid_monitor_or_dpi_is_rejected();
    test_d3d11_probe_reports_real_device_quality_capabilities();
    test_d3d11_ps1_frame_upload_plan_is_tightly_packed_rgba8();
    test_d3d11_ps1_frame_upload_plan_rejects_malformed_storage();
    test_d3d11_ps1_frame_upload_round_trips_pixels();
    if (failures != 0) {
        std::cerr << failures << " Win32 presentation test(s) failed\n";
        return 1;
    }
    std::cout << "Win32 presentation tests passed\n";
    return 0;
}
#endif
