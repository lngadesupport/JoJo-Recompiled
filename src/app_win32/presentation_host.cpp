#ifdef _WIN32
#define NOMINMAX
#include "app_win32/presentation_host.h"

#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <limits>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace jojo {
namespace {

bool valid_monitor_rect(const RECT& rect) noexcept {
    return rect.right > rect.left && rect.bottom > rect.top;
}

Result<void> make_windowed_rect(Win32WindowPlan& plan, RECT monitor_bounds) {
    RECT rect{
        0,
        0,
        static_cast<LONG>(plan.client_resolution.width),
        static_cast<LONG>(plan.client_resolution.height),
    };
    if (!AdjustWindowRectExForDpi(&rect, plan.style, FALSE, plan.ex_style, plan.dpi)) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "AdjustWindowRectExForDpi failed for requested presentation size");
    }
    const LONG width = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;
    const LONG monitor_width = monitor_bounds.right - monitor_bounds.left;
    const LONG monitor_height = monitor_bounds.bottom - monitor_bounds.top;
    const LONG x = monitor_bounds.left + std::max<LONG>(0, (monitor_width - width) / 2);
    const LONG y = monitor_bounds.top + std::max<LONG>(0, (monitor_height - height) / 2);
    plan.window_rect = {x, y, x + width, y + height};
    return Result<void>::success();
}

HRESULT create_probe_device(ID3D11Device** device, ID3D11DeviceContext** context) noexcept {
    constexpr std::array<D3D_FEATURE_LEVEL, 4> levels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL selected{};
    auto create = [&](D3D_DRIVER_TYPE type, const D3D_FEATURE_LEVEL* begin, UINT count) {
        return D3D11CreateDevice(nullptr,
                                 type,
                                 nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 begin,
                                 count,
                                 D3D11_SDK_VERSION,
                                 device,
                                 &selected,
                                 context);
    };

    HRESULT hr = create(D3D_DRIVER_TYPE_HARDWARE, levels.data(), static_cast<UINT>(levels.size()));
    if (hr == E_INVALIDARG) {
        hr = create(D3D_DRIVER_TYPE_HARDWARE, levels.data() + 1, static_cast<UINT>(levels.size() - 1));
    }
    if (SUCCEEDED(hr)) return hr;

    *device = nullptr;
    *context = nullptr;
    hr = create(D3D_DRIVER_TYPE_WARP, levels.data(), static_cast<UINT>(levels.size()));
    if (hr == E_INVALIDARG) {
        hr = create(D3D_DRIVER_TYPE_WARP, levels.data() + 1, static_cast<UINT>(levels.size() - 1));
    }
    return hr;
}

}

Result<Win32WindowPlan> make_win32_window_plan(
    const PresentationPlan& presentation,
    RECT monitor_bounds,
    std::uint32_t dpi) {
    if (!valid_monitor_rect(monitor_bounds)) {
        return Result<Win32WindowPlan>::failure(ErrorCode::invalid_argument,
                                                "monitor bounds must have positive dimensions");
    }
    if (dpi == 0u) {
        return Result<Win32WindowPlan>::failure(ErrorCode::invalid_argument,
                                                "window DPI must be non-zero");
    }
    if (presentation.presentation_resolution.width == 0u ||
        presentation.presentation_resolution.height == 0u ||
        presentation.presentation_resolution.width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()) ||
        presentation.presentation_resolution.height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max())) {
        return Result<Win32WindowPlan>::failure(ErrorCode::invalid_argument,
                                                "presentation resolution is invalid for a Win32 window");
    }

    Win32WindowPlan plan{};
    plan.client_resolution = presentation.presentation_resolution;
    plan.dpi = dpi;
    plan.display_width = presentation.presentation_resolution.width;
    plan.display_height = presentation.presentation_resolution.height;

    switch (presentation.applied_display_mode) {
        case DisplayMode::windowed: {
            plan.style = WS_OVERLAPPEDWINDOW;
            plan.ex_style = WS_EX_APPWINDOW;
            auto rect = make_windowed_rect(plan, monitor_bounds);
            if (!rect) {
                return Result<Win32WindowPlan>::failure(rect.error, rect.detail);
            }
            break;
        }
        case DisplayMode::borderless:
            plan.style = WS_POPUP;
            plan.ex_style = WS_EX_APPWINDOW;
            plan.window_rect = monitor_bounds;
            plan.cover_monitor = true;
            break;
        case DisplayMode::fullscreen:
            if (!presentation.exclusive_fullscreen) {
                return Result<Win32WindowPlan>::failure(
                    ErrorCode::invalid_argument,
                    "fullscreen presentation plan must explicitly request exclusive fullscreen");
            }
            plan.style = WS_POPUP;
            plan.ex_style = WS_EX_APPWINDOW;
            plan.window_rect = {
                monitor_bounds.left,
                monitor_bounds.top,
                monitor_bounds.left + static_cast<LONG>(plan.display_width),
                monitor_bounds.top + static_cast<LONG>(plan.display_height),
            };
            plan.switch_display_mode = true;
            plan.exclusive_fullscreen = true;
            break;
    }

    return Result<Win32WindowPlan>::success(plan);
}

Result<void> apply_win32_window_plan(HWND window, const Win32WindowPlan& plan) {
    if (!window) {
        return Result<void>::failure(ErrorCode::invalid_argument, "window handle is null");
    }

    if (plan.switch_display_mode) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        mode.dmPelsWidth = plan.display_width;
        mode.dmPelsHeight = plan.display_height;
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        if (ChangeDisplaySettingsW(&mode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) {
            return Result<void>::failure(ErrorCode::invalid_settings,
                                         "exclusive fullscreen display mode is unavailable");
        }
    } else {
        ChangeDisplaySettingsW(nullptr, 0);
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous_style = SetWindowLongPtrW(window, GWL_STYLE, static_cast<LONG_PTR>(plan.style));
    if (previous_style == 0 && GetLastError() != ERROR_SUCCESS) {
        return Result<void>::failure(ErrorCode::invalid_argument, "failed to apply Win32 window style");
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous_ex_style = SetWindowLongPtrW(window, GWL_EXSTYLE, static_cast<LONG_PTR>(plan.ex_style));
    if (previous_ex_style == 0 && GetLastError() != ERROR_SUCCESS) {
        return Result<void>::failure(ErrorCode::invalid_argument, "failed to apply Win32 extended window style");
    }

    const int width = plan.window_rect.right - plan.window_rect.left;
    const int height = plan.window_rect.bottom - plan.window_rect.top;
    if (!SetWindowPos(window,
                      nullptr,
                      plan.window_rect.left,
                      plan.window_rect.top,
                      width,
                      height,
                      SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "failed to position Win32 presentation window");
    }
    return Result<void>::success();
}

Result<RendererCapabilities> probe_d3d11_renderer_capabilities() {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const HRESULT hr = create_probe_device(&device, &context);
    if (FAILED(hr) || !device) {
        if (context) context->Release();
        if (device) device->Release();
        return Result<RendererCapabilities>::failure(
            ErrorCode::backend_unavailable,
            "unable to create a D3D11 hardware or WARP device for renderer capability probing");
    }

    RendererCapabilities caps{};
    caps.exclusive_fullscreen = true;
    caps.texture_filters = {
        TextureFilter::off,
        TextureFilter::x2,
        TextureFilter::x4,
        TextureFilter::x8,
        TextureFilter::x16,
    };
    caps.msaa_modes = {Msaa::off};

    struct SampleMode { UINT samples; Msaa mode; };
    constexpr std::array<SampleMode, 3> samples{{
        {2u, Msaa::x2},
        {4u, Msaa::x4},
        {8u, Msaa::x8},
    }};
    for (const auto& sample : samples) {
        UINT quality_levels = 0u;
        if (SUCCEEDED(device->CheckMultisampleQualityLevels(
                DXGI_FORMAT_R8G8B8A8_UNORM,
                sample.samples,
                &quality_levels)) && quality_levels > 0u) {
            caps.msaa_modes.push_back(sample.mode);
        }
    }

    if (context) context->Release();
    device->Release();
    return Result<RendererCapabilities>::success(std::move(caps));
}

}
#endif
