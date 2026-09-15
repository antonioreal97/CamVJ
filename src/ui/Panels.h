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

// UI-only view of the overlay subsystem (FX-026).
//
// These types deliberately live at the UI seam rather than exposing the
// library or compositor. App owns and refreshes the vectors between frames;
// panels only borrow them and emit at most one command for App to consume
// after drawing. This keeps file picking, decoding, disk IO and GPU resource
// changes out of the ImGui call stack and, therefore, out of the video path.
enum class OverlayMediaType : uint8_t
{
    StillPng,
    PngSequence,
};

enum class OverlayCanvasFormat : uint8_t
{
    Landscape16x9,
    Portrait9x16,
};

enum class OverlayPanelPlayback : uint8_t
{
    Loop,
    OneShot,
};

struct OverlayVariantUiState
{
    bool              present    = false;
    uint32_t          width      = 0;
    uint32_t          height     = 0;
    uint32_t          frameCount = 0;
    float             fps        = 0.0f;
    const GpuTexture* thumbnail  = nullptr; // borrowed first frame; never owned by UI
    std::string       status;
};

struct OverlayAssetUiState
{
    std::string           id;
    std::string           name;
    OverlayMediaType      mediaType = OverlayMediaType::StillPng;
    OverlayVariantUiState landscape;
    OverlayVariantUiState portrait;
    bool                  inStack          = false;
    uint32_t              presetReferences = 0;
    std::string           status;
};

struct OverlayLayerUiState
{
    uint64_t         id = 0;
    std::string      assetId;
    std::string      name;
    OverlayMediaType mediaType = OverlayMediaType::StillPng;
    bool             enabled = true; // operator intent, including when a variant is absent
    float            opacity = 1.0f;
    bool             variantAvailable = true;
    bool             visible          = true;
    bool             transitioning    = false;
    OverlayPanelPlayback playback     = OverlayPanelPlayback::Loop;
    float            fps              = 30.0f;
    bool             paused           = false;
    uint32_t         displayedFrame   = 0;
    uint32_t         frameCount       = 0;
    uint64_t         underflows       = 0;
    std::string      status;
};

enum class OverlayImportPhase : uint8_t
{
    Idle,
    Picking,
    Validating,
    Copying,
    Preparing,
    Failed,
};

struct OverlayImportUiState
{
    OverlayImportPhase phase       = OverlayImportPhase::Idle;
    float              progress    = 0.0f;
    bool               cancellable = false;
    std::string        status;
};

struct OverlayPanelSnapshot
{
    // Assets must be kept in case-insensitive display-name order by the
    // producer. Sorting inside draw would allocate once per video frame.
    const std::vector<OverlayLayerUiState>* layers = nullptr; // front to back
    const std::vector<OverlayAssetUiState>* assets = nullptr;

    OverlayCanvasFormat format = OverlayCanvasFormat::Landscape16x9;
    OverlayImportUiState import;
    std::string          status;

    // Increment only after an import commits. The UI uses the edge to open
    // the library exactly once and select the imported logical asset.
    uint64_t    importSerial = 0;
    std::string lastImportedAssetId;
};

enum class OverlayUiCommandType : uint8_t
{
    None,
    SetLayerEnabled,
    SetLayerOpacity,
    SetLayerPlayback,
    SetLayerFps,
    SetLayerPaused,
    RestartLayer,
    MoveLayer,
    RemoveLayer,
    AddLayer,
    BeginImport,
    AddVariant,
    ReplaceVariant,
    RemoveAsset,
    CancelImport,
    DismissStatus,
};

struct OverlayUiCommand
{
    OverlayUiCommandType type = OverlayUiCommandType::None;
    uint64_t             layerId = 0;
    std::string          assetId;
    bool                 enabled = false;
    float                opacity = 1.0f;
    OverlayPanelPlayback playback = OverlayPanelPlayback::Loop;
    float                fps = 30.0f;
    bool                 paused = false;
    int                  moveDelta = 0;
    OverlayMediaType     mediaType = OverlayMediaType::StillPng;
    OverlayCanvasFormat  format = OverlayCanvasFormat::Landscape16x9;
};

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

    // How much of the look is in the chain: 1 the full chain, 0 Clean. The FX
    // preview bus reads it, because a ramped-out chain honestly shows no look
    // and the operator would otherwise think their effect is broken.
    float              programMix      = 1.0f;

    // How far PROGRAM has travelled toward the mode lit on the buttons, 0 to 1
    // — whichever of the two dissolves is running. The panel shows it because
    // an operator who pressed a button needs to see that the machine is on its
    // way, not stuck.
    float              programProgress = 1.0f;
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

    // Scene presets (FX-009). The panel writes a recall id or a save name;
    // App applies between frames so the chain is never rebuilt mid-draw.
    std::string* recallPresetId = nullptr;
    std::string* savePresetName = nullptr;
    bool*        requestPresetSave = nullptr;

    // Managed overlay library + active stack (FX-026). App clears the command
    // to None before draw, then consumes it between frames. Null keeps older
    // callers and headless/test harnesses source-compatible.
    const OverlayPanelSnapshot* overlays      = nullptr;
    OverlayUiCommand*        overlayCommand = nullptr;
};

void drawProgramPanel(UiFrameState& state);
void drawSourcePanel(UiFrameState& state);
void drawOutputPanel(UiFrameState& state);
void drawPresetsPanel(UiFrameState& state);
void drawOverlaysPanel(UiFrameState& state);
void drawEffectsPanel(UiFrameState& state);
void drawPreviewPanel(UiFrameState& state);
void drawStatsPanel(UiFrameState& state);

// The wide panel under the preview. Two faces: the stats strip by default,
// and the parameters of the effect the operator is working on. See
// ui/Inspector.h for what decides which one is showing.
void drawInspectorPanel(UiFrameState& state);

// Overlay-specific inspector face and sidebar height. Kept with its renderer
// so the layout pass and the controls cannot disagree about the four slots.
void  drawOverlayInspectorPanel(UiFrameState& state);
float overlaysPanelHeight();

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
