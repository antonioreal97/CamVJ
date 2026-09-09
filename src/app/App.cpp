#include "app/App.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "core/Log.h"
#include "effects/BuiltinEffects.h"
#include "effects/EffectRegistry.h"
#include "gpu/Backend.h"
#include "tracking/source_mapping.h"

namespace atemfx {

namespace {

constexpr uint32_t kInitialWindowWidth  = 1600;
constexpr uint32_t kInitialWindowHeight = 900;

// Binary PPM: three lines of header and raw RGB. No encoder, no dependency,
// and every image tool reads it. This is a diagnostic, not a deliverable.
bool writePpm(const std::string& path, const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)
{
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
    {
        ATEMFX_LOG_ERROR("Could not open %s for writing", path.c_str());
        return false;
    }

    std::fprintf(file, "P6\n%u %u\n255\n", width, height);

    std::vector<uint8_t> row(static_cast<std::size_t>(width) * 3);
    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const std::size_t source = (static_cast<std::size_t>(y) * width + x) * 4;
            row[x * 3 + 0]           = rgba[source + 0];
            row[x * 3 + 1]           = rgba[source + 1];
            row[x * 3 + 2]           = rgba[source + 2];
        }
        std::fwrite(row.data(), 1, row.size(), file);
    }

    std::fclose(file);
    return true;
}

} // namespace

bool App::initialize(const AppOptions& options)
{
    options_ = options;
    programMode_ = options.programMode;

    // No dissolve into the state the show starts in: there is no previous
    // picture to come from, and --program black exists precisely so the first
    // frame is already safe.
    programTransition_.snapTo(programMode_);

    if (!options_.headless)
    {
        window_ = createPlatformWindow();
        if (!window_ || !window_->create("CamVJ", kInitialWindowWidth, kInitialWindowHeight))
        {
            return false;
        }
    }

    device_ = createGraphicsDevice();
    if (!device_ || !device_->initialize(window_.get(), kProcessingWidth, kProcessingHeight))
    {
        return false;
    }

    if (window_)
    {
        window_->setResizeCallback([this](uint32_t width, uint32_t height) {
            // Only the presentation surface follows the window. The processing
            // resolution is fixed by the project format.
            device_->onWindowResized(width, height);
        });
    }

    updateEffectContext();

    if (options_.checkShaders)
    {
        // Nothing else is needed: no input, no chain, no UI.
        return true;
    }

    availableSources_ = enumerateVideoSources();
    ATEMFX_LOG_INFO("Video inputs available: %zu", availableSources_.size());
    for (const VideoSourceDescriptor& source : availableSources_)
    {
        ATEMFX_LOG_INFO("  %-10s %s", source.category.c_str(), source.displayName.c_str());
    }

    startTracking();

    int requested = options_.sourceId.empty() ? 0 : -1;
    if (!options_.sourceId.empty())
    {
        for (int i = 0; i < static_cast<int>(availableSources_.size()); ++i)
        {
            if (availableSources_[i].id == options_.sourceId)
            {
                requested = i;
                break;
            }
        }
        if (requested < 0)
        {
            status_ = "Input unavailable: " + options_.sourceId + ". Select an input to recover.";
            sourceDisconnected_ = true;
            ATEMFX_LOG_WARN("Unknown input '%s'; holding black until an input is selected",
                            options_.sourceId.c_str());
        }
    }

    if (requested >= 0 && !selectSource(requested))
    {
        return false;
    }

    if (source_ && options_.testPattern >= 0)
    {
        if (Parameter* pattern = source_->parameters().find("pattern"))
        {
            pattern->setInt(options_.testPattern);
        }
    }

    registerBuiltinEffects(EffectRegistry::instance());
    if (!createDefaultChain())
    {
        return false;
    }
    chain_.prepare(effectContext_);

    std::string programError;
    if (!programOutput_.initialize(effectContext_, programError))
    {
        ATEMFX_LOG_ERROR("Program output: %s", programError.c_str());
        return false;
    }

    if (window_ && !ui_.initialize(*window_, *device_))
    {
        return false;
    }

    // Enumeration opens nothing, so it is safe headless. Opening an output is
    // not: it needs a window and a swap chain.
    displays_ = enumerateDisplays();
    ATEMFX_LOG_INFO("Displays available: %zu", displays_.size());
    for (const DisplayInfo& display : displays_)
    {
        ATEMFX_LOG_INFO("  %s %ux%u%s",
                        display.name.c_str(),
                        display.width,
                        display.height,
                        display.primary ? " (primary)" : "");
    }

    if (!options_.outputDisplayId.empty())
    {
        if (options_.headless)
        {
            ATEMFX_LOG_WARN("--output needs a window; ignored in headless mode");
        }
        else
        {
            int index = -1;
            for (int i = 0; i < static_cast<int>(displays_.size()); ++i)
            {
                if (displays_[i].id == options_.outputDisplayId)
                {
                    index = i;
                    break;
                }
            }

            if (index < 0)
            {
                ATEMFX_LOG_WARN("Unknown display '%s'; starting with no output",
                                options_.outputDisplayId.c_str());
            }
            else if (!openOutput(index))
            {
                // Not fatal. Losing the wall is bad; refusing to start because
                // of it is worse, because the operator then has nothing at all.
                ATEMFX_LOG_WARN("Could not open the output; starting with none");
            }
        }
    }

    webcamSupported_ = virtualCameraSupported();
    if (options_.webcam)
    {
        if (webcamSupported_)
        {
            // Started from the frame loop with every other device change, so
            // the command line takes exactly the path the operator's button
            // takes, including how it reports failing.
            requestWebcamStart_ = true;
        }
        else
        {
            webcamStatus_ = "Webcam output is not supported on this platform";
            webcamFailed_ = true;
            ATEMFX_LOG_WARN("%s", webcamStatus_.c_str());
        }
    }

    timing_.reset();

    ATEMFX_LOG_INFO("CamVJ ready: %s backend, processing %ux%u",
                    backendName(),
                    kProcessingWidth,
                    kProcessingHeight);
    return true;
}

bool App::createDefaultChain()
{
    // Auto Frame starts enabled so a live camera puts the subject on the wall.
    struct Preset
    {
        const char* typeId;
        bool        enabled;
    };

    // Auto Frame first so the rest of the chain treats the LED picture, not
    // the wide shot. It starts enabled: a live camera is supposed to put the
    // subject on the wall. Other effects stay off until the operator adds them.
    const Preset presets[] = {
        {"auto_frame", true},
        {"passthrough", false},
        {"rgb_split", false},
        {"pixelate", false},
        {"fm_raster", false},
        {"subpixel", false},
        {"shutter", false},
        {"frame_delay", false},
        {"mirror", false},
        {"vhs", false},
        {"crt", false},
    };

    const bool overridden = !options_.enabledEffects.empty();

    for (const Preset& preset : presets)
    {
        std::string error;
        if (!chain_.addByType(preset.typeId, effectContext_, error))
        {
            ATEMFX_LOG_ERROR("Could not create '%s': %s", preset.typeId, error.c_str());
            return false;
        }

        bool enabled = preset.enabled;
        if (overridden)
        {
            enabled = std::find(options_.enabledEffects.begin(),
                                options_.enabledEffects.end(),
                                std::string(preset.typeId)) != options_.enabledEffects.end();
        }

        chain_.at(chain_.size() - 1).setEnabled(enabled);
    }

    return true;
}

bool App::checkShaders()
{
    // Compiles every shader the backend can see, not only the ones the current
    // chain happens to use. That is the point: an effect nobody has enabled
    // yet is exactly the one whose syntax error goes unnoticed.
    namespace fs = std::filesystem;

    const fs::path directory = device_->shaders().directory();

    std::vector<std::string> names;
    std::error_code          ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::string stem = entry.path().stem().string();

        // common holds shared declarations and is never a program of its own;
        // fullscreen is the vertex shader, compiled by the backend at start-up.
        if (stem == "common" || stem == "fullscreen")
        {
            continue;
        }

        names.push_back(stem);
    }

    if (ec || names.empty())
    {
        ATEMFX_LOG_ERROR("No shaders found in %s", directory.string().c_str());
        return false;
    }

    std::sort(names.begin(), names.end());

    std::size_t failed = 0;
    for (const std::string& name : names)
    {
        std::string error;
        if (device_->shaders().shader(name, &error))
        {
            ATEMFX_LOG_INFO("  ok    %s", name.c_str());
        }
        else
        {
            ++failed;
            ATEMFX_LOG_ERROR("  FAIL  %s", name.c_str());
        }
    }

    ATEMFX_LOG_INFO("%zu shaders, %zu failed", names.size(), failed);
    return failed == 0;
}

bool App::selectSource(int index)
{
    if (index < 0 || index >= static_cast<int>(availableSources_.size()))
    {
        return false;
    }

    const VideoSourceDescriptor& descriptor = availableSources_[index];

    std::unique_ptr<VideoSource> candidate = createVideoSource(descriptor);
    std::string                  error;

    // Before initialize(), which is what starts capture: after that, frames
    // arrive on another thread and installing the tap would be a race. A
    // source with no frames in system memory ignores this and tracking
    // reports that it is seeing nothing.
    if (candidate && tracker_)
    {
        Tracker* tracker = tracker_.get();
        candidate->setFrameObserver([tracker](const uint8_t* bgra,
                                              uint32_t       width,
                                              uint32_t       height,
                                              std::size_t    rowBytes,
                                              bool           bottomUp) {
            tracker->submit(bgra, width, height, rowBytes, bottomUp);
        });
    }

    if (!candidate || !candidate->initialize(effectContext_, error))
    {
        status_ = descriptor.displayName + ": " + (error.empty() ? "could not open" : error);
        ATEMFX_LOG_ERROR("%s", status_.c_str());

        // Keep a working input on immediate failure. Before the first frame,
        // PROGRAM stays black; test patterns require an explicit selection.
        if (source_)
        {
            return true;
        }
        currentSource_ = index;
        return true;
    }

    if (source_)
    {
        source_->shutdown();
    }

    source_        = std::move(candidate);
    currentSource_ = index;
    sourceDisconnected_ = false;
    status_        = "Input: " + descriptor.displayName;

    // A lock is a box on this sensor. It is meaningless on the next one.
    if (tracker_)
    {
        tracker_->unlock();
        tracker_->setEnumerateCandidates(false);
    }
    pickSubjectMode_       = false;
    requestedLock_.pending = false;

    ATEMFX_LOG_INFO("Input selected: %s (%s)",
                    descriptor.displayName.c_str(),
                    descriptor.category.c_str());
    return true;
}

void App::startTracking()
{
    tracker_ = createSubjectTracker();
    if (!tracker_)
    {
        trackingStatus_ = "no subject tracking on this platform";
        ATEMFX_LOG_INFO("Subject tracking: unavailable on this platform");
        return;
    }

    std::string error;
    if (!tracker_->start(error))
    {
        // Tracking that will not start must not take the show down: the Auto
        // Frame effect still works from its manual controls.
        trackingStatus_ = error.empty() ? "tracking unavailable" : error;
        ATEMFX_LOG_ERROR("Subject tracking: %s", trackingStatus_.c_str());
        tracker_.reset();
    }
}

void App::rescanDevices()
{
    // No input running means no input selected. Falling back to the test
    // pattern id here would re-select it in the panel while PROGRAM is holding
    // black, and the one thing the operator must be able to trust is what the
    // panel says is live.
    const std::string currentId = source_ ? source_->descriptor().id :
        (currentSource_ >= 0 && currentSource_ < static_cast<int>(availableSources_.size()))
            ? availableSources_[currentSource_].id
            : std::string();

    const std::vector<VideoSourceDescriptor> previous = availableSources_;
    availableSources_                                 = enumerateVideoSources();

    currentSource_  = -1;
    bool stillThere = false;
    for (int i = 0; i < static_cast<int>(availableSources_.size()); ++i)
    {
        if (availableSources_[i].id == currentId)
        {
            currentSource_ = i;
            stillThere     = true;
            break;
        }
    }

    std::string appeared;
    for (const VideoSourceDescriptor& source : availableSources_)
    {
        bool known = false;
        for (const VideoSourceDescriptor& old : previous)
        {
            if (old.id == source.id)
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            ATEMFX_LOG_INFO("Camera connected: %s (%s)",
                            source.displayName.c_str(),
                            source.category.c_str());
            appeared = source.displayName;
        }
    }

    for (const VideoSourceDescriptor& old : previous)
    {
        bool still = false;
        for (const VideoSourceDescriptor& source : availableSources_)
        {
            if (source.id == old.id)
            {
                still = true;
                break;
            }
        }
        if (!still)
        {
            ATEMFX_LOG_INFO("Camera disconnected: %s", old.displayName.c_str());
        }
    }

    if (appeared.empty() && stillThere && availableSources_.size() == previous.size())
    {
        return;
    }

    if (!stillThere && source_)
    {
        sourceDisconnected_ = true;
        status_ = "Input disconnected. PROGRAM holds; select an input, then FX or Clean to resume.";
        ATEMFX_LOG_WARN("Input disappeared; holding PROGRAM until operator recovery");
        return;
    }

    if (sourceDisconnected_)
    {
        // A camera that comes back does not take itself live: recovery is an
        // operator action, so the guidance has to survive every rescan.
        status_ = appeared.empty()
            ? "No input running. PROGRAM holds; select an input, then FX or Clean to resume."
            : "Input available again. Select it, then FX or Clean to resume.";
        return;
    }

    status_ = appeared.empty() ? "Found " + std::to_string(availableSources_.size()) + " inputs"
                               : appeared + " connected";
    ATEMFX_LOG_INFO("%s", status_.c_str());
}

bool App::openOutput(int displayIndex)
{
    if (displayIndex < 0 || displayIndex >= static_cast<int>(displays_.size()))
    {
        return false;
    }

    closeOutput();

    const DisplayInfo& display = displays_[static_cast<std::size_t>(displayIndex)];

    if (display.primary)
    {
        // Allowed, because a single-display machine is how this gets tested,
        // but the operator has just covered their own user interface. Escape
        // is the way back, and it is worth saying so before they need it.
        ATEMFX_LOG_WARN("Output is on the primary display; press Escape to close it");
    }

    outputWindow_ = createOutputWindow();
    if (!outputWindow_ || !outputWindow_->open(display.id))
    {
        outputWindow_.reset();
        outputStatus_ = "Could not open a window on " + display.name;
        return false;
    }

    outputSurface_ = device_->createOutputSurface(outputWindow_->nativeHandle(),
                                                  outputWindow_->width(),
                                                  outputWindow_->height());
    if (!outputSurface_)
    {
        outputWindow_->close();
        outputWindow_.reset();
        outputStatus_ = "Could not create a surface on " + display.name;
        return false;
    }

    currentDisplay_ = displayIndex;
    outputStatus_   = "Live on " + display.name;
    ATEMFX_LOG_INFO("Output live on %s (%ux%u)",
                    display.name.c_str(),
                    outputSurface_->width(),
                    outputSurface_->height());
    return true;
}

void App::closeOutput()
{
    // Surface first: it renders into the window's layer, and the layer goes
    // away with the window.
    outputSurface_.reset();

    if (outputWindow_)
    {
        outputWindow_->close();
        outputWindow_.reset();
    }

    if (currentDisplay_ >= 0)
    {
        ATEMFX_LOG_INFO("Output closed");
        outputStatus_.clear();
    }
    currentDisplay_ = -1;
}

// Everything that opens, closes or re-enumerates a display. Called between
// frames for the same reason source selection is: none of it belongs inside
// the measured region.
void App::serviceOutput()
{
    if (outputWindow_ && outputWindow_->consumeCloseRequest())
    {
        requestOutputClose_ = true;
    }

    if (requestDisplayRescan_)
    {
        requestDisplayRescan_ = false;

        const std::string liveId =
            (currentDisplay_ >= 0 && currentDisplay_ < static_cast<int>(displays_.size()))
                ? displays_[static_cast<std::size_t>(currentDisplay_)].id
                : std::string();

        displays_ = enumerateDisplays();

        // The list is rebuilt, so the index into it is meaningless until the
        // live display is found again. Unplugged mid-show, the output goes.
        currentDisplay_ = -1;
        for (int i = 0; i < static_cast<int>(displays_.size()); ++i)
        {
            if (displays_[i].id == liveId)
            {
                currentDisplay_ = i;
                break;
            }
        }

        if (!liveId.empty() && currentDisplay_ < 0)
        {
            ATEMFX_LOG_WARN("The output display disconnected");
            outputStatus_ = "Output display disconnected";
            requestOutputClose_ = true;
        }
    }

    if (requestOutputClose_)
    {
        requestOutputClose_ = false;
        requestedDisplay_   = -1;
        closeOutput();
    }

    if (requestedDisplay_ >= 0)
    {
        const int requested = requestedDisplay_;
        requestedDisplay_   = -1;
        if (requested != currentDisplay_ && !openOutput(requested))
        {
            ATEMFX_LOG_WARN("%s", outputStatus_.c_str());
        }
    }
}

// Camera transport is opened and closed here, between frames, for the same
// reason displays are: talking to a system extension is not a per-frame
// operation, and a webcam that fails to start must never cost a frame.
void App::serviceWebcam()
{
    if (webcam_)
    {
        webcamStats_ = webcam_->stats();

        if (webcamStats_.state == VirtualCameraState::Failed)
        {
            webcamStatus_ = webcam_->error();
            webcamFailed_ = true;
            ATEMFX_LOG_WARN("Webcam: %s", webcamStatus_.c_str());
            webcam_.reset();
            webcamStats_ = {VirtualCameraState::Stopped, 0, 0};
        }
        else if (webcamStats_.state == VirtualCameraState::Stopped)
        {
            // The worker has released the stream, so the output can go. Not
            // before: a frame may still be on its way to it.
            webcam_.reset();
            webcamStats_ = {VirtualCameraState::Stopped, 0, 0};
            webcamStatus_ = "Webcam stopped";
        }
    }

    if (requestWebcamStop_)
    {
        requestWebcamStop_ = false;
        if (webcam_)
        {
            webcamStatus_ = "Webcam stopping";
            webcam_->requestStop();
        }
    }

    if (requestWebcamStart_)
    {
        requestWebcamStart_ = false;
        if (!webcam_)
        {
            std::string error;
            webcam_ = createVirtualCameraOutput(*device_, error);
            if (webcam_)
            {
                webcamStats_  = {VirtualCameraState::Starting, 0, 0};
                webcamStatus_ = "Webcam starting";
            }
            else
            {
                webcamStatus_ = error;
                webcamFailed_ = true;
                ATEMFX_LOG_WARN("Webcam: %s", error.c_str());
            }
        }
    }
}

// Non-zero only when the run was asked for a webcam on the command line and
// never got one. An operator who started it from the panel gets the message
// in the panel; a script that passed --webcam gets an exit status.
int App::webcamResult() const
{
    return (options_.webcam && webcamFailed_) ? 1 : 0;
}

void App::updateEffectContext()
{
    effectContext_.shaders    = &device_->shaders();
    effectContext_.fullscreen = &device_->fullscreenPass();
    effectContext_.targets    = &device_->targets();
    effectContext_.width      = kProcessingWidth;
    effectContext_.height     = kProcessingHeight;
    effectContext_.time       = timing_.totalSeconds();
    effectContext_.deltaTime  = timing_.deltaSeconds();
    effectContext_.frameIndex = timing_.frameIndex();

    // The tracker looked at the captured image; the chain works on the canvas.
    // Fitting and mirroring happen between the two, so the observation is
    // mapped here rather than in the effect, which has no idea what the input
    // is. An SDI input at project resolution maps one to one. The same helper
    // maps a Pick click the other way, so the two cannot drift apart.
    TrackingSnapshot tracking;
    if (tracker_ && tracker_->latest(tracking) && source_)
    {
        const SourceMapping mapping = source_->mapping();
        mapSourceToCanvas(mapping, tracking.centerX, tracking.centerY, tracking.width,
                          tracking.height);

        // A subject fitted outside the canvas is not on the wall, so it is not
        // a subject. Letterbox bars are the only way this happens.
        if (tracking.centerX < 0.0f || tracking.centerX > 1.0f ||
            tracking.centerY < 0.0f || tracking.centerY > 1.0f)
        {
            tracking.valid = false;
        }
    }

    tracking.available = tracker_ != nullptr;
    effectContext_.tracking       = tracking;
    effectContext_.framing        = {};
    effectContext_.framingActive  = false;
    effectContext_.outputAspect   = 16.0f / 9.0f;
}

int App::run()
{
    if (options_.checkShaders)
    {
        return checkShaders() ? 0 : 1;
    }

    if (options_.headless)
    {
        const int frames = options_.frames > 0 ? options_.frames : 300;
        ATEMFX_LOG_INFO("Headless run: %d frames", frames);

        for (int i = 0; i < frames; ++i)
        {
            timing_.beginFrame();
            renderFrame();
        }

        reportTimings();

        if (!options_.dumpPath.empty() && !dumpLastFrame(options_.dumpPath))
        {
            return 1;
        }
        return webcamResult();
    }

    window_->runFrameLoop([this] {
        timing_.beginFrame();
        renderFrame();

        if (options_.frames > 0 && timing_.frameIndex() >= static_cast<uint64_t>(options_.frames))
        {
            reportTimings();
            window_->destroy();
        }
    });

    return webcamResult();
}

void App::renderFrame()
{
    // The preview can lose its drawable while the output must keep running:
    // minimised, or — the case that actually bites — completely covered by the
    // output window itself, because the operator's window happened to be on
    // the display they sent output to. macOS stops vending drawables to a
    // window nobody can see, and an engine that returns here would take the
    // wall down with the preview.
    //
    // beginFrame() is skipped rather than ignored when minimised: acquiring a
    // drawable for a window that has none can block, and nothing the operator
    // does to their own window is allowed to stall the output.
    const bool wantPreview  = !window_ || !window_->minimized();
    const bool havePreview  = wantPreview && device_->beginFrame();

    if (!havePreview && !outputSurface_)
    {
        return;
    }

    if (requestedLock_.pending)
    {
        requestedLock_.pending = false;
        pickSubjectMode_       = false;
        if (tracker_ && source_)
        {
            float centerX = requestedLock_.centerX;
            float centerY = requestedLock_.centerY;
            float width   = requestedLock_.width;
            float height  = requestedLock_.height;
            mapCanvasToSource(source_->mapping(), centerX, centerY, width, height);
            tracker_->lock(centerX, centerY, width, height);
        }
    }

    if (tracker_)
    {
        TrackingSnapshot latest;
        const bool       waiting = !tracker_->latest(latest) || !latest.locked;
        tracker_->setEnumerateCandidates(pickSubjectMode_ || waiting);
    }

    updateEffectContext();

    // Device work happens between frames, never inside the measured region:
    // opening a camera is not a per-frame operation.
    if (requestDeviceRescan_ || consumeVideoDeviceHotplug())
    {
        requestDeviceRescan_ = false;
        rescanDevices();
    }

    if (requestedSource_ >= 0)
    {
        const int requested = requestedSource_;
        requestedSource_    = -1;
        selectSource(requested);
    }

    serviceOutput();
    serviceWebcam();

    if (requestShaderReload_ && !operationLocked_)
    {
        requestShaderReload_ = false;
        std::string error;
        status_ = device_->shaders().reloadAll(error) ? "Shaders reloaded"
                                                      : ("Reload failed: " + error);
    }

    // --- Processing --------------------------------------------------------
    device_->beginProcessing();

    GpuTexture* sourceFrame = source_ ? source_->render(effectContext_) : nullptr;
    sourceHealth_ = source_ ? source_->health() : SourceHealth{};
    if (!source_)
    {
        sourceHealth_.signal = SourceSignal::Waiting;
    }
    const bool inputHealthy = !sourceDisconnected_ &&
        (sourceHealth_.signal == SourceSignal::Generated || sourceHealth_.signal == SourceSignal::Live);
    // Read before ProgramOutput can latch Freeze on input loss: the dissolve
    // follows the operator's mode, not the fault the output is covering.
    const float effectMix = programTransition_.update(programMode_, effectContext_.deltaTime);

    GpuTexture* frame = nullptr;
    if (sourceFrame && inputHealthy)
    {
        frame = &chain_.process(effectContext_, *sourceFrame, effectMix, source_->bypassEffects());
    }

    // The preview bus, read before program policy can hold this image or
    // replace it with black, and before ProgramOutput restores the framing
    // that belongs to the held frame rather than to this one. Under Freeze the
    // chain is still working on the next shot; this is the only place that
    // picture can be seen.
    GpuTexture* chainFrame          = frame;
    const FramingRect chainFraming  = effectContext_.framing;
    const bool  chainFramingActive  = effectContext_.framingActive;
    const float chainAspect         = effectContext_.outputAspect;

    frame = programOutput_.render(effectContext_, frame, inputHealthy, programMode_);
    lastOutput_ = frame;

    // The webcam sees PROGRAM, which is the point: Clean, Freeze and Black
    // reach a call the same way they reach the wall. It only offers the frame
    // and returns; a call application that stops reading costs dropped frames
    // in the stats, never a stalled chain.
    if (webcam_ && frame)
    {
        webcam_->submit(*frame);
    }

    device_->endProcessing();

    // --- Program output ----------------------------------------------------
    // Before the user interface, and on its own command buffer: what the
    // audience sees must not queue behind panels the operator is looking at.
    if (outputSurface_ && frame)
    {
        outputSurface_->present(*frame);
    }

    // --- Presentation ------------------------------------------------------
    if (window_ && havePreview)
    {
        device_->beginUi();

        UiFrameState state;
        state.chain               = &chain_;
        state.source              = source_.get();
        state.availableSources    = &availableSources_;
        state.selectedSource      = currentSource_;
        state.requestedSource     = &requestedSource_;
        state.requestDeviceRescan = &requestDeviceRescan_;
        state.displays            = &displays_;
        state.selectedDisplay     = currentDisplay_;
        state.requestedDisplay    = &requestedDisplay_;
        state.requestOutputClose  = &requestOutputClose_;
        state.requestDisplayRescan = &requestDisplayRescan_;
        state.outputActive        = outputSurface_ != nullptr;
        state.sourceHealth        = sourceHealth_;
        state.sourceDisconnected  = sourceDisconnected_;
        state.programMode         = &programMode_;
        state.programMix          = effectMix;
        // Held mid-ramp behind Freeze or Black is not a dissolve in progress,
        // and a percentage that never moves reads as a hung machine.
        state.programMixing       = programTransition_.active() &&
            (programMode_ == ProgramMode::Effects || programMode_ == ProgramMode::Clean);
        state.operationLocked     = &operationLocked_;
        state.inputHealthy        = inputHealthy;
        state.outputWidth         = outputSurface_ ? outputSurface_->width() : 0;
        state.outputHeight        = outputSurface_ ? outputSurface_->height() : 0;
        state.outputStatus        = &outputStatus_;
        state.webcamSupported     = webcamSupported_;
        state.webcamStats         = webcamStats_;
        state.webcamStatus        = &webcamStatus_;
        state.requestWebcamStart  = &requestWebcamStart_;
        state.requestWebcamStop   = &requestWebcamStop_;
        state.timing              = &timing_;
        state.effectContext       = &effectContext_;
        state.sourcePreview       = sourceFrame;
        state.preview             = frame;
        state.chainPreview        = chainFrame;
        state.chainFraming        = chainFraming;
        state.chainFramingActive  = chainFramingActive;
        state.chainAspect         = chainAspect;
        state.processingWidth     = kProcessingWidth;
        state.processingHeight    = kProcessingHeight;
        state.gpuMilliseconds     = device_->lastGpuMilliseconds();
        state.gpuTimingValid      = device_->hasGpuTiming();
        state.adapterName         = device_->adapterName().c_str();
        state.backendName         = backendName();
        state.vsync               = &options_.vsync;
        state.requestShaderReload = &requestShaderReload_;
        state.status              = &status_;
        trackingStatus_           = tracker_ ? tracker_->status() : trackingStatus_;
        state.trackingStatus      = &trackingStatus_;
        state.trackingAvailable   = tracker_ != nullptr;
        state.pickSubjectMode     = &pickSubjectMode_;
        state.requestedLock       = &requestedLock_;

        trackingCandidates_.clear();
        const bool showCandidates =
            pickSubjectMode_ || !effectContext_.tracking.locked;
        if (tracker_ && showCandidates)
        {
            tracker_->candidates(trackingCandidates_);
            if (source_)
            {
                const SourceMapping mapping = source_->mapping();
                std::size_t         write   = 0;
                for (std::size_t i = 0; i < trackingCandidates_.size(); ++i)
                {
                    TrackingCandidate mapped = trackingCandidates_[i];
                    mapSourceToCanvas(mapping, mapped.centerX, mapped.centerY, mapped.width,
                                      mapped.height);
                    if (mapped.centerX < 0.0f || mapped.centerX > 1.0f ||
                        mapped.centerY < 0.0f || mapped.centerY > 1.0f)
                    {
                        continue;
                    }
                    trackingCandidates_[write++] = mapped;
                }
                trackingCandidates_.resize(write);
            }
            else
            {
                trackingCandidates_.clear();
            }
        }
        state.trackingCandidates = &trackingCandidates_;

        ui_.beginFrame(*window_, *device_);
        ui_.draw(state);
        ui_.render(*device_);
    }

    // With an output live, the output display is the clock. Waiting for the
    // preview's display as well means waiting on two unsynchronised vsyncs,
    // and two 60 Hz displays that never agree on phase produce 30 fps.
    if (havePreview)
    {
        device_->endFrame(options_.vsync && !outputSurface_);
    }
}

void App::reportTimings() const
{
    ATEMFX_LOG_INFO("--- CamVJ timing ---------------------------------");
    ATEMFX_LOG_INFO("backend      %s (%s)", backendName(), device_->adapterName().c_str());
    ATEMFX_LOG_INFO("resolution   %ux%u", kProcessingWidth, kProcessingHeight);
    if (source_)
    {
        ATEMFX_LOG_INFO("input        %s  (%s)",
                        source_->descriptor().displayName.c_str(),
                        source_->status().c_str());
    }
    ATEMFX_LOG_INFO("tracking     %s",
                    tracker_ ? tracker_->status().c_str() : trackingStatus_.c_str());
    ATEMFX_LOG_INFO("frames       %llu", static_cast<unsigned long long>(timing_.frameIndex()));
    ATEMFX_LOG_INFO("fps          %.1f", timing_.fps());
    ATEMFX_LOG_INFO("cpu frame    %.3f ms avg, %.3f ms peak",
                    timing_.averageFrameMs(),
                    timing_.maxFrameMs());
    if (device_->hasGpuTiming())
    {
        ATEMFX_LOG_INFO("gpu process  %.3f ms", device_->lastGpuMilliseconds());
    }
    ATEMFX_LOG_INFO("effects      %zu enabled of %zu", chain_.enabledCount(), chain_.size());
    if (options_.webcam)
    {
        // The one number that says whether a call actually received the show.
        // Dropped frames are the only symptom that path produces.
        const VirtualCameraStats webcam = webcam_ ? webcam_->stats() : webcamStats_;
        ATEMFX_LOG_INFO("webcam       %llu sent, %llu dropped%s%s",
                        static_cast<unsigned long long>(webcam.sent),
                        static_cast<unsigned long long>(webcam.skipped),
                        webcamStatus_.empty() ? "" : "  ",
                        webcamStatus_.c_str());
    }
    ATEMFX_LOG_INFO("budget       16.68 ms per frame at 59.94 fps");
    ATEMFX_LOG_INFO("--------------------------------------------------");
}

bool App::dumpLastFrame(const std::string& path)
{
    if (!lastOutput_)
    {
        ATEMFX_LOG_ERROR("Nothing to dump: the pipeline produced no frame");
        return false;
    }

    std::vector<uint8_t> rgba;
    if (!device_->readback(*lastOutput_, rgba))
    {
        ATEMFX_LOG_ERROR("Frame readback failed");
        return false;
    }

    if (!writePpm(path, rgba, lastOutput_->width(), lastOutput_->height()))
    {
        return false;
    }

    ATEMFX_LOG_INFO("Wrote %s (%ux%u)", path.c_str(), lastOutput_->width(), lastOutput_->height());
    return true;
}

void App::shutdown()
{
    ui_.shutdown();
    programOutput_.shutdown();
    chain_.shutdown();

    if (source_)
    {
        source_->shutdown();
        source_.reset();
    }

    // After the source: capture is stopped by then, so nothing can be inside
    // submit() when the tracker goes away.
    if (tracker_)
    {
        tracker_->stop();
        tracker_.reset();
    }

    // Before the device: the surface holds a layer and a pipeline built by it.
    closeOutput();

    // Same reason, plus one of its own: the destructor waits for the worker to
    // release the camera extension, so the device outlives the last frame the
    // GPU was still copying into it.
    webcam_.reset();

    if (device_)
    {
        device_->shutdown();
    }
    if (window_)
    {
        window_->destroy();
    }
}

} // namespace atemfx
