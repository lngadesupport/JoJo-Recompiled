#ifdef _WIN32
#define NOMINMAX
#include "app_win32/presentation_host.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
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

Result<ID3DBlob*> compile_d3d11_shader(const char* source, const char* target) {
    if (!source || !target) {
        return Result<ID3DBlob*>::failure(
            ErrorCode::invalid_argument,
            "D3D11 shader compilation requires source and target");
    }

    ID3DBlob* bytecode = nullptr;
    ID3DBlob* diagnostics = nullptr;
    const HRESULT hr = D3DCompile(
        source,
        std::strlen(source),
        nullptr,
        nullptr,
        nullptr,
        "main",
        target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0u,
        &bytecode,
        &diagnostics);
    if (FAILED(hr) || !bytecode) {
        std::string detail = "D3D11 presentation shader compilation failed";
        if (diagnostics && diagnostics->GetBufferPointer() && diagnostics->GetBufferSize() != 0u) {
            detail += ": ";
            detail.append(
                static_cast<const char*>(diagnostics->GetBufferPointer()),
                diagnostics->GetBufferSize());
        }
        if (diagnostics) diagnostics->Release();
        if (bytecode) bytecode->Release();
        return Result<ID3DBlob*>::failure(ErrorCode::backend_unavailable, std::move(detail));
    }
    if (diagnostics) diagnostics->Release();
    return Result<ID3DBlob*>::success(bytecode);
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

Result<D3d11FrameUploadPlan> make_d3d11_frame_upload_plan(
    const Ps1DisplayFrame& frame) {
    if (frame.width == 0u || frame.height == 0u) {
        return Result<D3d11FrameUploadPlan>::failure(
            ErrorCode::invalid_argument,
            "PS1 display frame dimensions must be non-zero");
    }

    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        return Result<D3d11FrameUploadPlan>::failure(
            ErrorCode::invalid_argument,
            "PS1 display frame dimensions overflow host storage");
    }
    const auto pixel_count = width * height;
    if (frame.rgba8.size() != pixel_count) {
        return Result<D3d11FrameUploadPlan>::failure(
            ErrorCode::invalid_argument,
            "PS1 display frame pixel storage does not match its dimensions");
    }
    if (frame.width > std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t)) {
        return Result<D3d11FrameUploadPlan>::failure(
            ErrorCode::invalid_argument,
            "PS1 display frame row pitch exceeds D3D11 limits");
    }

    D3d11FrameUploadPlan plan{};
    plan.width = frame.width;
    plan.height = frame.height;
    plan.row_pitch = frame.width * static_cast<std::uint32_t>(sizeof(std::uint32_t));
    plan.byte_size = frame.rgba8.size() * sizeof(std::uint32_t);
    plan.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    plan.pixels = frame.rgba8.data();
    return Result<D3d11FrameUploadPlan>::success(plan);
}

Result<void> upload_d3d11_ps1_frame(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const Ps1DisplayFrame& frame,
    ID3D11Texture2D** texture_out) {
    if (!device || !context || !texture_out) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "D3D11 frame upload requires device, context, and output texture");
    }
    *texture_out = nullptr;

    const auto plan = make_d3d11_frame_upload_plan(frame);
    if (!plan) {
        return Result<void>::failure(plan.error, plan.detail);
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = plan.value.width;
    desc.Height = plan.value.height;
    desc.MipLevels = 1u;
    desc.ArraySize = 1u;
    desc.Format = plan.value.format;
    desc.SampleDesc.Count = 1u;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = plan.value.pixels;
    initial.SysMemPitch = plan.value.row_pitch;

    ID3D11Texture2D* texture = nullptr;
    const HRESULT hr = device->CreateTexture2D(&desc, &initial, &texture);
    if (FAILED(hr) || !texture) {
        if (texture) texture->Release();
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "D3D11 failed to create the PS1 display texture");
    }

    *texture_out = texture;
    return Result<void>::success();
}

Result<void> blit_d3d11_ps1_frame(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const Ps1DisplayFrame& frame,
    ID3D11RenderTargetView* render_target,
    std::uint32_t target_width,
    std::uint32_t target_height) {
    if (!device || !context || !render_target || target_width == 0u || target_height == 0u) {
        return Result<void>::failure(
            ErrorCode::invalid_argument,
            "D3D11 frame blit requires device, context, render target, and non-zero target dimensions");
    }

    ID3D11Texture2D* texture = nullptr;
    const auto uploaded = upload_d3d11_ps1_frame(device, context, frame, &texture);
    if (!uploaded) {
        return uploaded;
    }

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = device->CreateShaderResourceView(texture, nullptr, &srv);
    if (FAILED(hr) || !srv) {
        if (srv) srv->Release();
        texture->Release();
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "D3D11 failed to create the PS1 display shader resource view");
    }

    constexpr const char* kVertexShader = R"(
struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertex_id : SV_VertexID) {
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    VSOutput output;
    output.position = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    output.uv = uv;
    return output;
}
)";

    constexpr const char* kPixelShader = R"(
Texture2D frame_texture : register(t0);
SamplerState frame_sampler : register(s0);

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    return frame_texture.Sample(frame_sampler, input.uv);
}
)";

    const auto vertex_bytecode = compile_d3d11_shader(kVertexShader, "vs_4_0");
    if (!vertex_bytecode) {
        srv->Release();
        texture->Release();
        return Result<void>::failure(vertex_bytecode.error, vertex_bytecode.detail);
    }
    const auto pixel_bytecode = compile_d3d11_shader(kPixelShader, "ps_4_0");
    if (!pixel_bytecode) {
        vertex_bytecode.value->Release();
        srv->Release();
        texture->Release();
        return Result<void>::failure(pixel_bytecode.error, pixel_bytecode.detail);
    }

    ID3D11VertexShader* vertex_shader = nullptr;
    hr = device->CreateVertexShader(
        vertex_bytecode.value->GetBufferPointer(),
        vertex_bytecode.value->GetBufferSize(),
        nullptr,
        &vertex_shader);
    if (FAILED(hr) || !vertex_shader) {
        if (vertex_shader) vertex_shader->Release();
        pixel_bytecode.value->Release();
        vertex_bytecode.value->Release();
        srv->Release();
        texture->Release();
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "D3D11 failed to create the PS1 presentation vertex shader");
    }

    ID3D11PixelShader* pixel_shader = nullptr;
    hr = device->CreatePixelShader(
        pixel_bytecode.value->GetBufferPointer(),
        pixel_bytecode.value->GetBufferSize(),
        nullptr,
        &pixel_shader);
    pixel_bytecode.value->Release();
    vertex_bytecode.value->Release();
    if (FAILED(hr) || !pixel_shader) {
        if (pixel_shader) pixel_shader->Release();
        vertex_shader->Release();
        srv->Release();
        texture->Release();
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "D3D11 failed to create the PS1 presentation pixel shader");
    }

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

    ID3D11SamplerState* sampler = nullptr;
    hr = device->CreateSamplerState(&sampler_desc, &sampler);
    if (FAILED(hr) || !sampler) {
        if (sampler) sampler->Release();
        pixel_shader->Release();
        vertex_shader->Release();
        srv->Release();
        texture->Release();
        return Result<void>::failure(
            ErrorCode::backend_unavailable,
            "D3D11 failed to create the PS1 presentation point sampler");
    }

    const D3D11_VIEWPORT viewport{
        0.0f,
        0.0f,
        static_cast<float>(target_width),
        static_cast<float>(target_height),
        0.0f,
        1.0f,
    };
    context->OMSetRenderTargets(1u, &render_target, nullptr);
    context->RSSetViewports(1u, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader, nullptr, 0u);
    context->PSSetShader(pixel_shader, nullptr, 0u);
    context->PSSetShaderResources(0u, 1u, &srv);
    context->PSSetSamplers(0u, 1u, &sampler);
    context->Draw(3u, 0u);

    ID3D11ShaderResourceView* null_srv = nullptr;
    context->PSSetShaderResources(0u, 1u, &null_srv);

    sampler->Release();
    pixel_shader->Release();
    vertex_shader->Release();
    srv->Release();
    texture->Release();
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
