#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gpu/Rhi.h"
#include "platform/Display.h"
#include "tracking/framing.h"
#include "tracking/TrackingSnapshot.h"
#include "video/VideoSource.h"
#include "video/program_output.h"
#include "video/source_health.h"
#include "video/virtual_camera.h"

namespace atemfx {

class EffectChain;
class FrameTiming;
class ParameterSet;
class VideoSource;
struct EffectContext;

// Everything the panels are allowed to touch this frame.
//
// The UI borrows the frame snapshot, edits operator controls and requests
// device changes. It never owns GPU resources or reaches into the renderer.
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
    SourceHealth                              sourceHealth        = {};
    bool                                      sourceDisconnected  = false;

    // Output selection, written the same way input selection is: the panel
    // asks, the application acts between frames. Opening a window on another
    // display is not a per-frame operation either.
    const std::vector<DisplayInfo>* displays             = nullptr;
    int                             selectedDisplay      = -1;
    int*                            requestedDisplay     = nullptr;
    bool*                           requestOutputClose   = nullptr;
    bool*                           requestDisplayRescan = nullptr;

    bool               outputActive    = false;
    uint32_t           outputWidth     = 0;
    uint32_t           outputHeight    = 0;
    const std::string*  outputStatus    = nullptr;
    ProgramMode*       programMode     = nullptr;

    bool                webcamSupported   = false;
    VirtualCameraStats  webcamStats       = {VirtualCameraState::Stopped, 0, 0};
    const std::string*   webcamStatus      = nullptr;
    bool*               requestWebcamStart = nullptr;
    bool*               requestWebcamStop  = nullptr;

    // Where the FX/Clean dissolve stands: 1 is the full chain, 0 Clean. The
    // panel shows it because an operator who pressed a button needs to see
    // that the machine is on its way, not stuck.
    float              programMix      = 1.0f;
    bool               programMixing   = false;
    bool*              operationLocked = nullptr;
    bool               inputHealthy    = false;

    const GpuTexture* sourcePreview = nullptr;
    const GpuTexture* preview       = nullptr;

    // The preview bus: the chain's own image, captured before program policy
    // can hold it, replace it with black or ramp the look out of it. This is
    // what FX would put on air right now, which is the one picture the
    // operator could not see while building a look behind Freeze or Black.
    // Its framing travels with it for the same reason: under Freeze the
    // program crop is the held one, and the chain has moved on.
    const GpuTexture* chainPreview       = nullptr;
    FramingRect       chainFraming       = {};
    bool              chainFramingActive = false;
    float             chainAspect        = 16.0f / 9.0f;

    uint32_t processingWidth  = 0;
    uint32_t processingHeight = 0;

    float gpuMilliseconds = 0.0f;
    bool  gpuTimingValid  = false;

    // Borrowed, not owned: the frame loop must not allocate.
    const char* adapterName = "";
    const char* backendName = "";

    // What the subject tracker is doing, or why it is not doing it.
    // Framing is steered from Auto Frame parameters; which subject to follow
    // is chosen here, because it is a capture concern, not an effect one.
    const std::string* trackingStatus = nullptr;
    bool               trackingAvailable = false;
    bool*              pickSubjectMode   = nullptr;
    const std::vector<TrackingCandidate>* trackingCandidates = nullptr;
    TrackingLockRequest*                  requestedLock      = nullptr;

    // Written by the UI, read by the application.
    bool*        vsync               = nullptr;
    bool*        requestShaderReload = nullptr;
    std::string* status              = nullptr;
};

void drawProgramPanel(UiFrameState& state);
void drawSourcePanel(UiFrameState& state);
void drawOutputPanel(UiFrameState& state);
void drawEffectsPanel(UiFrameState& state);
void drawPreviewPanel(UiFrameState& state);
void drawStatsPanel(UiFrameState& state);

// The wide panel under the preview. Two faces: the stats strip by default,
// and the parameters of the effect the operator is working on. See
// ui/Inspector.h for what decides which one is showing.
void drawInspectorPanel(UiFrameState& state);

// Content height of the PROGRAM panel: its caption plus the mode row. The
// layout pass sizes the window from it, and a second copy of the arithmetic
// would drift the first time the row changes.
float programControlsHeight();

// Renders any ParameterSet without knowing what the parameters mean. This is
// what lets an effect written tomorrow show up in today's UI.
void drawParameters(ParameterSet& parameters, const char* idScope, bool allowAutomation = false);

// The same rows dealt across `columns` equal columns. The inspector is wide
// and short where the sidebar was narrow and tall, and a single column there
// would hide most of an effect below the fold.
void drawParametersColumns(ParameterSet& parameters, const char* idScope, int columns,
                           bool allowAutomation = false);

// How many parameters one column receives when `count` are dealt across
// `columns`. Shared so the layout pass, which sizes the panel before it
// draws, cannot disagree with the renderer about which column a parameter
// lands in.
std::size_t parameterColumnSpan(std::size_t count, int columns);

} // namespace atemfx
