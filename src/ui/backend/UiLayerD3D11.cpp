#include "ui/UiLayer.h"

#include "core/Log.h"
#include "gpu/d3d11/D3D11Device.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "platform/Window.h"
#include "platform/win32/Win32MessageHook.h"

// The Windows half of UiLayer. Layout and styling live in ui/UiLayer.cpp and
// are shared with the macOS build; only ImGui's platform and renderer backends
// differ.
//
// The static_casts are safe by construction: exactly one backend is compiled
// in, and its device factory is the only thing that can have produced the
// GraphicsDevice passed here.

namespace atemfx {

namespace {

bool forwardToImGui(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    return ImGui_ImplWin32_WndProcHandler(handle, message, wParam, lParam) != 0;
}

// main.cpp makes the process per-monitor DPI aware, so ImGui is already
// working in physical pixels here. Nothing to correct for density; the layout
// itself is what has to grow, and it has to grow again when the window is
// dragged to a monitor with a different scaling setting.
ui::DisplayScale displayScale(void* nativeHandle)
{
    ui::DisplayScale scale;
    scale.pixelDensity = 1.0f;
    scale.contentScale = 1.0f;
    if (HWND handle = static_cast<HWND>(nativeHandle))
    {
        const UINT dpi = ::GetDpiForWindow(handle);
        if (dpi > 0)
        {
            scale.contentScale = static_cast<float>(dpi) / 96.0f;
        }
    }
    return scale;
}

} // namespace

bool UiLayer::initialize(Window& window, GraphicsDevice& device)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    configure(displayScale(window.nativeHandle()));

    if (!ImGui_ImplWin32_Init(window.nativeHandle()))
    {
        ATEMFX_LOG_ERROR("ImGui_ImplWin32_Init failed");
        return false;
    }

    D3D11Device& d3d = static_cast<D3D11Device&>(device);
    if (!ImGui_ImplDX11_Init(d3d.device(), d3d.context()))
    {
        ATEMFX_LOG_ERROR("ImGui_ImplDX11_Init failed");
        return false;
    }

    setWin32MessageHook(&forwardToImGui);

    initialized_ = true;
    return true;
}

void UiLayer::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    setWin32MessageHook(nullptr);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void UiLayer::beginFrame(Window& window, GraphicsDevice& device)
{
    (void)device;

    // The atlas is locked between NewFrame and Render, so a rebuild has to
    // happen here, before the frame opens. InvalidateDeviceObjects drops the
    // font texture; NewFrame recreates it from the atlas below.
    if (updateScale(displayScale(window.nativeHandle())))
    {
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void UiLayer::render(GraphicsDevice& device)
{
    (void)device;

    // The device has already bound and cleared the back buffer in beginUi().
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

} // namespace atemfx
