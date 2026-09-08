#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gpu/Rhi.h"
#include "platform/Display.h"
#include "video/VideoSource.h"

namespace atemfx {

class EffectChain;
class FrameTiming;
class ParameterSet;
class VideoSource;
struct EffectContext;

// Everything the panels are allowed to touch this frame.
//
// The UI reads and writes parameters and nothing else. It never owns GPU
// resources and never reaches into the renderer.
struct UiFrameState
{
    EffectChain*       chain         = nullptr;
    VideoSource*       source        = nullptr;
    const FrameTiming* timing        = nullptr;
    EffectContext*     effectContext = nullptr;  // needed to initialise effects added live

    // Input selection. The panel writes the request; the application acts on
    // it between frames, because opening a device is not a per-frame operation.
    const std::vector<VideoSourceDescriptor>* availableSources    = nullptr;
    int                                       selectedSource      = -1;
    int*                                      requestedSource     = nullptr;
    bool*                                     requestDeviceRescan = nullptr;

    // Output selection, written the same way input selection is: the panel
    // asks, the application acts between frames. Opening a window on another
    // display is not a per-frame operation either.
    const std::vector<DisplayInfo>* displays             = nullptr;
    int                             selectedDisplay      = -1;
    int*                            requestedDisplay     = nullptr;
    bool*                           requestOutputClose   = nullptr;
    bool*                           requestDisplayRescan = nullptr;

    bool               outputActive = false;
    uint32_t           outputWidth  = 0;
    uint32_t           outputHeight = 0;
    const std::string* outputStatus = nullptr;

    const GpuTexture* sourcePreview = nullptr;
    const GpuTexture* preview       = nullptr;

    uint32_t processingWidth  = 0;
    uint32_t processingHeight = 0;

    float gpuMilliseconds = 0.0f;
    bool  gpuTimingValid  = false;

    // Borrowed, not owned: the frame loop must not allocate.
    // Borrowed, not owned: the frame loop must not allocate.
    const char* adapterName = "";
    const char* backendName = "";

    // What the subject tracker is doing, or why it is not doing it. Read
    // only: tracking is steered from the Auto Frame effect's parameters like
    // any other effect, not from a panel of its own.
    const std::string* trackingStatus = nullptr;

    // Written by the UI, read by the application.
    bool*        vsync               = nullptr;
    bool*        requestShaderReload = nullptr;
    std::string* status              = nullptr;
};

void drawSourcePanel(UiFrameState& state);
void drawOutputPanel(UiFrameState& state);
void drawEffectsPanel(UiFrameState& state);
void drawPreviewPanel(UiFrameState& state);
void drawStatsPanel(UiFrameState& state);

// Renders any ParameterSet without knowing what the parameters mean. This is
// what lets an effect written tomorrow show up in today's UI.
void drawParameters(ParameterSet& parameters, const char* idScope, bool allowAutomation = false);

} // namespace atemfx
