#include "ui/UiLayer.h"

#import <Cocoa/Cocoa.h>

#include "core/Log.h"
#include "gpu/metal/MetalDevice.h"
#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "platform/Window.h"

// The macOS half of UiLayer. Layout and styling live in ui/UiLayer.cpp and are
// shared with the Direct3D 11 build; only ImGui's platform and renderer
// backends differ.
//
// The static_casts are safe by construction: exactly one backend is compiled
// in, and its device factory is the only thing that can have produced the
// GraphicsDevice passed here.

namespace atemfx {

namespace {

// AppKit hands ImGui a window measured in points and lets the Metal renderer
// multiply by the backing factor, so the layout stays at 1.0 and only the font
// atlas has to be built at the higher density. A window dragged between a
// Retina panel and an external 1x monitor changes this mid-run.
ui::DisplayScale displayScale(NSView* view)
{
    ui::DisplayScale scale;
    scale.pixelDensity = view && view.window ? (float)view.window.backingScaleFactor : 1.0f;
    scale.contentScale = 1.0f;
    return scale;
}

} // namespace

bool UiLayer::initialize(Window& window, GraphicsDevice& device)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    NSView* view = (__bridge NSView*)window.nativeHandle();
    if (!view)
    {
        ATEMFX_LOG_ERROR("UI: window has no native view");
        return false;
    }

    configure(displayScale(view));

    if (!ImGui_ImplOSX_Init(view))
    {
        ATEMFX_LOG_ERROR("ImGui_ImplOSX_Init failed");
        return false;
    }

    MetalDevice& metal = static_cast<MetalDevice&>(device);
    if (!ImGui_ImplMetal_Init(metal.metal()))
    {
        ATEMFX_LOG_ERROR("ImGui_ImplMetal_Init failed");
        return false;
    }

    initialized_ = true;
    return true;
}

void UiLayer::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void UiLayer::beginFrame(Window& window, GraphicsDevice& device)
{
    MetalDevice&             metal = static_cast<MetalDevice&>(device);
    MTLRenderPassDescriptor* pass  = metal.uiRenderPassDescriptor();
    if (!pass)
    {
        return;
    }

    NSView* view = (__bridge NSView*)window.nativeHandle();

    // The atlas is locked between NewFrame and Render, so a rebuild has to
    // happen here, before the frame opens.
    if (updateScale(displayScale(view)))
    {
        ImGui_ImplMetal_DestroyFontsTexture();
        ImGui_ImplMetal_CreateFontsTexture(metal.metal());
    }

    ImGui_ImplMetal_NewFrame(pass);
    ImGui_ImplOSX_NewFrame(view);
    ImGui::NewFrame();
}

void UiLayer::render(GraphicsDevice& device)
{
    MetalDevice&             metal    = static_cast<MetalDevice&>(device);
    id<MTLCommandBuffer>     commands = metal.uiCommandBuffer();
    MTLRenderPassDescriptor* pass     = metal.uiRenderPassDescriptor();
    if (!commands || !pass)
    {
        return;
    }

    ImGui::Render();

    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commands, encoder);
    [encoder endEncoding];
}

} // namespace atemfx
