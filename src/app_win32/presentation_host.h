#pragma once

#ifdef _WIN32
#define NOMINMAX
#include "core/presentation.h"
#include "core/ps1_display_frame.h"

#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgiformat.h>
#include <windows.h>
#include <wrl/client.h>

namespace jojo {

struct Win32WindowPlan {
    DWORD style{};
    DWORD ex_style{};
    RECT window_rect{};
    Extent2D client_resolution{};
    bool cover_monitor{};
    bool switch_display_mode{};
    bool exclusive_fullscreen{};
    std::uint32_t display_width{};
    std::uint32_t display_height{};
    std::uint32_t dpi{96u};
};

struct D3d11FrameUploadPlan {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t row_pitch{};
    std::size_t byte_size{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    const void* pixels{};
};

[[nodiscard]] Result<Win32WindowPlan> make_win32_window_plan(
    const PresentationPlan& presentation,
    RECT monitor_bounds,
    std::uint32_t dpi);

[[nodiscard]] Result<void> apply_win32_window_plan(
    HWND window,
    const Win32WindowPlan& plan);

[[nodiscard]] Result<D3d11FrameUploadPlan> make_d3d11_frame_upload_plan(
    const Ps1DisplayFrame& frame);

[[nodiscard]] Result<void> upload_d3d11_ps1_frame(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const Ps1DisplayFrame& frame,
    ID3D11Texture2D** texture_out);

[[nodiscard]] Result<void> blit_d3d11_ps1_frame(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const Ps1DisplayFrame& frame,
    ID3D11RenderTargetView* render_target,
    std::uint32_t target_width,
    std::uint32_t target_height);

class D3d11Ps1Presenter {
public:
    D3d11Ps1Presenter() = default;

    [[nodiscard]] static Result<D3d11Ps1Presenter> create(HWND window);
    [[nodiscard]] Result<void> present(const Ps1DisplayFrame& frame);

    [[nodiscard]] std::uint32_t back_buffer_width() const noexcept;
    [[nodiscard]] std::uint32_t back_buffer_height() const noexcept;

private:
    [[nodiscard]] Result<void> recreate_render_target();
    [[nodiscard]] Result<void> resize_to_client();

    HWND window_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_{};
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_{};
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_{};
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_{};
    std::uint32_t back_buffer_width_{};
    std::uint32_t back_buffer_height_{};
};

[[nodiscard]] Result<RendererCapabilities> probe_d3d11_renderer_capabilities();

}
#endif
