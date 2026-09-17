#pragma once

#ifdef _WIN32
#define NOMINMAX
#include "core/presentation.h"

#include <cstdint>
#include <windows.h>

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

[[nodiscard]] Result<Win32WindowPlan> make_win32_window_plan(
    const PresentationPlan& presentation,
    RECT monitor_bounds,
    std::uint32_t dpi);

[[nodiscard]] Result<void> apply_win32_window_plan(
    HWND window,
    const Win32WindowPlan& plan);

[[nodiscard]] Result<RendererCapabilities> probe_d3d11_renderer_capabilities();

}
#endif
