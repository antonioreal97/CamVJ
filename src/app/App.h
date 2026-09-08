#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "effects/Effect.h"
#include "effects/EffectChain.h"
#include "gpu/Rhi.h"
#include "platform/Display.h"
#include "platform/OutputWindow.h"
#include "platform/Window.h"
#include "tracking/Tracker.h"
#include "ui/UiLayer.h"
#include "video/FrameTiming.h"
#include "video/VideoDevices.h"
#include "video/VideoSource.h"

namespace atemfx {

struct AppOptions
{
    // Runs the whole pipeline with no window and no UI. The self-test path:
    // it exercises the device, the shaders and the chain, and it works over
    // SSH and in CI.
    bool headless = false;

    // 0 means run until the window is closed.
    int frames = 0;

    // Writes the final frame as a binary PPM when set.
    std::string dumpPath;

    bool vsync = true;

    // Type ids to start enabled. Empty means the built-in default chain.
    // Mainly for the self-test, which has no UI to click.
    std::vector<std::string> enabledEffects;

    // Video input id from enumerateVideoSources. Empty means the test pattern.
    std::string sourceId;

    // Display id from enumerateDisplays. Empty means no output: the engine
    // renders to its own window only. Set it and the processed frame goes
    // full screen on that display the moment the application starts.
    std::string outputDisplayId;

    // Compile every shader in the backend's directory and exit. Opens no
    // device, so it is safe in CI and safe on a machine with no camera
    // permission.
    bool checkShaders = false;
};

// Application lifetime and the frame loop. Owns every subsystem.
//
// M0 runs source, effects and UI on one thread. There is no external video
// clock to decouple from yet; see docs/ARCHITECTURE.md section 3 for where the
// processing thread gets split off in M1.
class App
{
public:
    bool initialize(const AppOptions& options);
    int  run();
    void shutdown();

private:
    bool checkShaders();
    bool createDefaultChain();
    bool selectSource(int index);
    bool openOutput(int displayIndex);
    void closeOutput();
    void serviceOutput();
    void startTracking();
    void rescanDevices();
    void updateEffectContext();
    void renderFrame();
    bool dumpLastFrame(const std::string& path);
    void reportTimings() const;

    // The project resolution. Processing happens here regardless of window
    // size, so M0's timings mean the same thing M1's will.
    static constexpr uint32_t kProcessingWidth  = 1920;
    static constexpr uint32_t kProcessingHeight = 1080;

    AppOptions options_;

    std::unique_ptr<Window>         window_;
    std::unique_ptr<GraphicsDevice> device_;

    std::unique_ptr<VideoSource>       source_;
    std::vector<VideoSourceDescriptor> availableSources_;
    int                                currentSource_   = -1;
    int                                requestedSource_ = -1;
    bool                               requestDeviceRescan_ = false;

    // Program output. Null until an operator picks a display; the window and
    // the surface live and die together, and the surface always goes first
    // because it renders into the window's layer.
    std::unique_ptr<OutputWindow>  outputWindow_;
    std::unique_ptr<OutputSurface> outputSurface_;
    std::vector<DisplayInfo>       displays_;
    int                            currentDisplay_      = -1;
    int                            requestedDisplay_    = -1;
    bool                           requestOutputClose_  = false;
    bool                           requestDisplayRescan_ = false;
    std::string                    outputStatus_;

    // Control plane. Runs beside the pipeline on its own thread, reads the
    // frames capture already produced, and can fail or stall without costing
    // a video frame. Null where the platform has no tracker.
    std::unique_ptr<Tracker> tracker_;
    std::string              trackingStatus_;

    EffectChain chain_;
    FrameTiming timing_;
    UiLayer     ui_;

    EffectContext effectContext_;
    GpuTexture*   lastOutput_ = nullptr;

    bool        requestShaderReload_ = false;
    std::string status_;
};

} // namespace atemfx
