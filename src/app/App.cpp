#include "app/App.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

#include "core/Log.h"
#include "core/Version.h"
#include "effects/BuiltinEffects.h"
#include "effects/EffectRegistry.h"
#include "gpu/Backend.h"
#include "platform/display_routing.h"
#include "presets/boot_state.h"
#include "presets/preset_store.h"
#include "presets/scene_preset.h"
#include "tracking/source_mapping.h"
#include "ui/Inspector.h"

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

OverlayAspect overlayAspect(OverlayCanvasFormat format)
{
    return format == OverlayCanvasFormat::Portrait9x16
        ? OverlayAspect::Portrait9x16 : OverlayAspect::Landscape16x9;
}

OverlayCanvasFormat overlayCanvasFormat(OverlayAspect aspect)
{
    return aspect == OverlayAspect::Portrait9x16
        ? OverlayCanvasFormat::Portrait9x16 : OverlayCanvasFormat::Landscape16x9;
}

OverlayPlayback overlayPlayback(OverlayPanelPlayback playback)
{
    return playback == OverlayPanelPlayback::OneShot
        ? OverlayPlayback::OneShot : OverlayPlayback::Loop;
}

OverlayPanelPlayback overlayPanelPlayback(OverlayPlayback playback)
{
    return playback == OverlayPlayback::OneShot
        ? OverlayPanelPlayback::OneShot : OverlayPanelPlayback::Loop;
}

OverlayMediaType overlayMediaType(OverlayKind kind)
{
    return kind == OverlayKind::PngSequence
        ? OverlayMediaType::PngSequence : OverlayMediaType::StillPng;
}

bool lessCaseInsensitive(const OverlayAsset& left, const OverlayAsset& right)
{
    const std::size_t common = std::min(left.name.size(), right.name.size());
    for (std::size_t i = 0; i < common; ++i)
    {
        const unsigned char a = static_cast<unsigned char>(left.name[i]);
        const unsigned char b = static_cast<unsigned char>(right.name[i]);
        const char lowerA = static_cast<char>(std::tolower(a));
        const char lowerB = static_cast<char>(std::tolower(b));
        if (lowerA != lowerB) return lowerA < lowerB;
    }
    if (left.name.size() != right.name.size()) return left.name.size() < right.name.size();
    return left.id < right.id;
}

OverlayStackConfig overlayConfigFromPreset(const ScenePreset& preset)
{
    OverlayStackConfig config;
    config.layerCount = std::min(preset.overlays.size(), kMaxOverlayLayers);
    for (std::size_t i = 0; i < config.layerCount; ++i)
    {
        const SceneOverlayState& saved = preset.overlays[i];
        OverlayLayerConfig& layer = config.layers[i];
        layer.assetId = saved.assetId;
        layer.enabled = saved.enabled;
        layer.opacity = saved.opacity;
        layer.playback = saved.playback;
        layer.framesPerSecond = saved.framesPerSecond;
    }
    return config;
}

std::string overlayImportName(const std::filesystem::path& path, OverlayMediaType mediaType)
{
    std::string name = mediaType == OverlayMediaType::PngSequence
        ? path.filename().string() : path.stem().string();
    return name.empty() ? "Overlay" : name;
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

    // Venue boot restores the last source / display / portrait unless the
    // command line already picked them. CLI wins so a scripted demo stays
    // deterministic.
    BootState boot;
    std::string bootError;
    const bool haveBoot = loadBootState(boot, bootError);
    std::string effectiveSourceId = options_.sourceId;
    std::string effectiveOutputId = options_.outputDisplayId;
    if (haveBoot)
    {
        if (effectiveSourceId.empty() && !boot.sourceId.empty())
        {
            effectiveSourceId = boot.sourceId;
            ATEMFX_LOG_INFO("Venue boot: source %s", effectiveSourceId.c_str());
        }
        if (effectiveOutputId.empty() && !boot.outputDisplayId.empty())
        {
            effectiveOutputId = boot.outputDisplayId;
            ATEMFX_LOG_INFO("Venue boot: output %s", effectiveOutputId.c_str());
        }
    }

    int requested = effectiveSourceId.empty() ? 0 : -1;
    if (!effectiveSourceId.empty())
    {
        for (int i = 0; i < static_cast<int>(availableSources_.size()); ++i)
        {
            if (availableSources_[i].id == effectiveSourceId)
            {
                requested = i;
                break;
            }
        }
        if (requested < 0)
        {
            status_ = "Input unavailable: " + effectiveSourceId + ". Select an input to recover.";
            sourceDisconnected_ = true;
            ATEMFX_LOG_WARN("Unknown input '%s'; holding black until an input is selected",
                            effectiveSourceId.c_str());
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
    if (haveBoot)
    {
        for (std::size_t i = 0; i < chain_.size(); ++i)
        {
            if (chain_.at(i).descriptor().typeId != "auto_frame") continue;
            if (Parameter* portrait = chain_.at(i).parameters().find("portrait"))
            {
                portrait->setBool(boot.portrait);
            }
            break;
        }
    }
    chain_.prepare(effectContext_);

    overlayPicker_ = createOverlaySourcePicker();
    std::string overlayLibraryError;
    if (!overlayLibrary_.scan(overlayLibraryError))
    {
        // A read-only failure must not take the video engine down. The panel
        // reports it and remains available for a later successful import.
        overlayPanelSnapshot_.status = "Overlay library: " + overlayLibraryError;
        ATEMFX_LOG_WARN("Overlay library: %s", overlayLibraryError.c_str());
    }
    std::string overlayError;
    if (!overlaySystem_.initialize(effectContext_, overlayLibrary_, overlayError))
    {
        ATEMFX_LOG_ERROR("Overlay system: %s", overlayError.c_str());
        return false;
    }
    overlaysInitialized_ = true;
    rebuildOverlayAssetUi();
    refreshOverlayUi();

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

    if (!effectiveOutputId.empty())
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
                if (displays_[i].id == effectiveOutputId)
                {
                    index = i;
                    break;
                }
            }

            if (index < 0)
            {
                outputStatus_ = "Requested display unavailable. Select an output to send PROGRAM.";
                ATEMFX_LOG_WARN("Unknown display '%s'; starting with no output",
                                effectiveOutputId.c_str());
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

    // The version leads the banner: a log from a show is often the only
    // evidence of which build was on the machine that night.
    ATEMFX_LOG_INFO("CamVJ %s ready: %s backend, processing %ux%u",
                    kVersion,
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
    persistBootState();
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
    persistBootState();
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

    // Consume even on a manual rescan, so the same event does not trigger
    // another enumeration on the next frame.
    const bool displaysChanged = consumeDisplayChanges();
    DisplayRouteLoss routeLoss = DisplayRouteLoss::None;
    if (requestDisplayRescan_ || displaysChanged)
    {
        requestDisplayRescan_ = false;
        auto refreshed = enumerateDisplays();
        const auto routing = reconcileDisplayRouting(
            displays_, refreshed, currentDisplay_, requestedDisplay_);
        displays_ = std::move(refreshed);
        currentDisplay_ = routing.selected;
        requestedDisplay_ = routing.requested;
        routeLoss = routing.loss;

        if (routeLoss != DisplayRouteLoss::None)
        {
            requestOutputClose_ = true;
        }
    }

    if (requestOutputClose_)
    {
        requestOutputClose_ = false;
        requestedDisplay_   = -1;
        closeOutput();
        // Operator stopped the wall (or the route died): next venue boot must
        // not reopen a display that is no longer the show path.
        persistBootState();
    }

    if (routeLoss != DisplayRouteLoss::None)
    {
        outputStatus_ = routeLoss == DisplayRouteLoss::Disconnected
            ? "Output display disconnected. Select an output to resume sending."
            : "Output display configuration changed. Select it again to resume sending.";
        ATEMFX_LOG_WARN("%s", outputStatus_.c_str());
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

void App::persistBootState()
{
    // During initialize the chain is not ready yet; writing a partial boot
    // would wipe the portrait flag the venue file already holds.
    if (chain_.size() == 0)
    {
        return;
    }

    BootState state;
    if (currentSource_ >= 0 &&
        currentSource_ < static_cast<int>(availableSources_.size()))
    {
        state.sourceId = availableSources_[static_cast<std::size_t>(currentSource_)].id;
    }
    if (currentDisplay_ >= 0 &&
        currentDisplay_ < static_cast<int>(displays_.size()))
    {
        state.outputDisplayId = displays_[static_cast<std::size_t>(currentDisplay_)].id;
    }
    for (std::size_t i = 0; i < chain_.size(); ++i)
    {
        if (chain_.at(i).descriptor().typeId != "auto_frame") continue;
        if (const Parameter* portrait = chain_.at(i).parameters().find("portrait"))
        {
            state.portrait = portrait->asBool();
        }
        break;
    }

    std::string error;
    if (!saveBootState(state, error))
    {
        ATEMFX_LOG_WARN("Venue boot not saved: %s", error.c_str());
    }
}

bool App::recallPresetById(const std::string& id)
{
    ScenePreset preset;
    std::string error;
    if (!findFactoryPreset(id, preset) && !loadUserPreset(id, preset, error))
    {
        status_ = "Preset not found: " + id;
        ATEMFX_LOG_WARN("%s", status_.c_str());
        return false;
    }

    const OverlayStackConfig overlayConfig = overlayConfigFromPreset(preset);
    if (!overlaySystem_.validateConfig(overlayConfig, error))
    {
        status_ = "Recall failed: " + error;
        ATEMFX_LOG_ERROR("%s", status_.c_str());
        return false;
    }

    if (!applyScene(preset, chain_, effectContext_, programMode_, programTransition_, error))
    {
        status_ = "Recall failed: " + error;
        ATEMFX_LOG_ERROR("%s", status_.c_str());
        return false;
    }
    if (!overlaySystem_.applyConfig(overlayConfig, error))
    {
        // validateConfig above makes this reachable only for an unexpected
        // runtime failure. Report it loudly; silently recalling half a look
        // would be worse than leaving the operator with a clear fault.
        status_ = "Recall failed: " + error;
        ATEMFX_LOG_ERROR("%s", status_.c_str());
        return false;
    }

    ui::closeInspector();
    status_ = "Look: " + preset.name;
    ATEMFX_LOG_INFO("Recalled preset '%s'", preset.name.c_str());
    persistBootState();
    return true;
}

bool App::saveCurrentPreset(const std::string& name)
{
    const std::string id = presetIdFromName(name);
    ScenePreset       preset =
        captureScene(chain_, programMode_, overlaySystem_.config(),
                     id, name.empty() ? id : name);
    std::string error;
    if (!saveUserPreset(preset, error))
    {
        status_ = "Save failed: " + error;
        ATEMFX_LOG_ERROR("%s", status_.c_str());
        return false;
    }
    status_ = "Saved look: " + preset.name;
    rebuildOverlayAssetUi();
    return true;
}

void App::servicePresets()
{
    if (operationLocked_)
    {
        recallPresetId_.clear();
        requestPresetSave_ = false;
        savePresetName_.clear();
        return;
    }

    if (!recallPresetId_.empty())
    {
        const std::string id = recallPresetId_;
        recallPresetId_.clear();
        recallPresetById(id);
    }

    if (requestPresetSave_)
    {
        requestPresetSave_ = false;
        const std::string name = savePresetName_;
        savePresetName_.clear();
        saveCurrentPreset(name);
    }
}

void App::rebuildOverlayAssetUi()
{
    std::vector<OverlayAsset> assets = overlayLibrary_.assets();
    std::sort(assets.begin(), assets.end(), lessCaseInsensitive);

    std::vector<ScenePreset> userPresets;
    std::string presetError;
    for (const PresetListEntry& entry : listUserPresets(presetError))
    {
        ScenePreset preset;
        std::string loadError;
        if (loadUserPreset(entry.id, preset, loadError))
            userPresets.push_back(std::move(preset));
    }
    if (!presetError.empty())
        ATEMFX_LOG_WARN("Overlay preset reference scan: %s", presetError.c_str());

    overlayAssetUi_.clear();
    overlayAssetUi_.reserve(assets.size());
    const OverlayStackConfig& active = overlaySystem_.config();
    for (const OverlayAsset& asset : assets)
    {
        OverlayAssetUiState item;
        item.id = asset.id;
        item.name = asset.name;
        item.mediaType = overlayMediaType(asset.kind);

        auto fillVariant = [&](OverlayAspect aspect, OverlayVariantUiState& target) {
            const OverlayVariant* variant = asset.variant(aspect);
            if (!variant) return;
            target.present = true;
            target.width = variant->width;
            target.height = variant->height;
            target.frameCount = static_cast<std::uint32_t>(variant->frames.size());
            target.fps = asset.fpsDenominator == 0 ? 0.0f
                : static_cast<float>(asset.fpsNumerator) /
                  static_cast<float>(asset.fpsDenominator);
        };
        fillVariant(OverlayAspect::Landscape16x9, item.landscape);
        fillVariant(OverlayAspect::Portrait9x16, item.portrait);

        for (std::size_t i = 0; i < active.layerCount; ++i)
        {
            if (active.layers[i].assetId == asset.id)
            {
                item.inStack = true;
                break;
            }
        }
        for (const ScenePreset& preset : userPresets)
        {
            const bool referenced = std::any_of(
                preset.overlays.begin(), preset.overlays.end(),
                [&](const SceneOverlayState& layer) { return layer.assetId == asset.id; });
            if (referenced) ++item.presetReferences;
        }
        overlayAssetUi_.push_back(std::move(item));
    }
    overlayPanelSnapshot_.assets = &overlayAssetUi_;
}

void App::refreshOverlayUi()
{
    const OverlayUiSnapshot& source = overlaySystem_.snapshot();
    overlayPanelSnapshot_.format = overlayCanvasFormat(
        effectContext_.outputAspect < 1.0f ? OverlayAspect::Portrait9x16
                                           : OverlayAspect::Landscape16x9);

    bool rebuild = overlayLayerUi_.size() != source.layerCount;
    if (!rebuild)
    {
        for (std::size_t uiIndex = 0; uiIndex < source.layerCount; ++uiIndex)
        {
            const std::size_t sourceIndex = source.layerCount - 1U - uiIndex;
            if (overlayLayerUi_[uiIndex].id != source.layers[sourceIndex].config.layerId)
            {
                rebuild = true;
                break;
            }
        }
    }
    if (rebuild)
    {
        overlayLayerUi_.clear();
        overlayLayerUi_.resize(source.layerCount);
    }

    for (std::size_t uiIndex = 0; uiIndex < source.layerCount; ++uiIndex)
    {
        const std::size_t sourceIndex = source.layerCount - 1U - uiIndex;
        const OverlayLayerStatus& status = source.layers[sourceIndex];
        OverlayLayerUiState& target = overlayLayerUi_[uiIndex];
        const OverlayAsset* asset = overlayLibrary_.find(status.config.assetId);
        if (rebuild || target.assetId != status.config.assetId)
        {
            target.id = status.config.layerId;
            target.assetId = status.config.assetId;
            target.name = asset ? asset->name : status.config.assetId;
            target.mediaType = asset ? overlayMediaType(asset->kind)
                                     : OverlayMediaType::StillPng;
        }
        target.enabled = status.config.enabled;
        target.opacity = status.config.opacity;
        target.playback = overlayPanelPlayback(status.config.playback);
        target.fps = status.config.framesPerSecond;
        target.paused = status.paused;
        target.displayedFrame = status.displayedFrame;
        target.frameCount = status.frameCount;
        target.underflows = status.underflows;
        if (target.status != status.error) target.status = status.error;
        target.transitioning = status.phase == OverlayLayerPhase::Buffering ||
                               status.phase == OverlayLayerPhase::FadingIn ||
                               status.phase == OverlayLayerPhase::FadingOut ||
                               status.replacing;
        target.visible = status.phase == OverlayLayerPhase::FadingIn ||
                         status.phase == OverlayLayerPhase::Live ||
                         status.phase == OverlayLayerPhase::FadingOut;
        const OverlayAspect aspect = overlayPanelSnapshot_.format ==
                OverlayCanvasFormat::Portrait9x16
            ? OverlayAspect::Portrait9x16 : OverlayAspect::Landscape16x9;
        target.variantAvailable = asset && asset->variant(aspect);
    }

    for (OverlayAssetUiState& asset : overlayAssetUi_) asset.inStack = false;
    for (const OverlayLayerUiState& layer : overlayLayerUi_)
    {
        for (OverlayAssetUiState& asset : overlayAssetUi_)
        {
            if (asset.id == layer.assetId)
            {
                asset.inStack = true;
                break;
            }
        }
    }
    overlayPanelSnapshot_.layers = &overlayLayerUi_;
    overlayPanelSnapshot_.assets = &overlayAssetUi_;
    overlayPanelSnapshot_.importSerial = overlayImportSerial_;
}

bool App::beginOverlayPicker(const OverlayUiCommand& command)
{
    if (!overlayPicker_ || overlayImportFuture_.valid() ||
        overlayPicker_->state() == OverlayPickerState::Picking)
    {
        overlayPanelSnapshot_.status = "Finish or cancel the current import first";
        return false;
    }

    pendingOverlayImport_ = {};
    pendingOverlayImport_.mediaType = command.mediaType;
    pendingOverlayImport_.format = command.format;
    pendingOverlayImport_.assetId = command.assetId;
    if (command.type == OverlayUiCommandType::AddVariant)
        pendingOverlayImport_.action = OverlayImportAction::AddVariant;
    else if (command.type == OverlayUiCommandType::ReplaceVariant)
        pendingOverlayImport_.action = OverlayImportAction::ReplaceVariant;
    else
        pendingOverlayImport_.action = OverlayImportAction::NewAsset;

    const OverlayPickerMode mode = command.mediaType == OverlayMediaType::PngSequence
        ? OverlayPickerMode::SequenceDirectory : OverlayPickerMode::StillPng;
    std::string error;
    if (!overlayPicker_->begin(mode, window_ ? window_->nativeHandle() : nullptr, error))
    {
        overlayPanelSnapshot_.import.phase = OverlayImportPhase::Failed;
        overlayPanelSnapshot_.import.status = error;
        return false;
    }
    overlayPanelSnapshot_.status.clear();
    overlayPanelSnapshot_.import = {};
    overlayPanelSnapshot_.import.phase = OverlayImportPhase::Picking;
    overlayPanelSnapshot_.import.cancellable = true;
    overlayPanelSnapshot_.import.status = "Choose overlay source";
    return true;
}

void App::launchOverlayImport(const std::filesystem::path& source)
{
    const PendingOverlayImport pending = pendingOverlayImport_;
    const std::filesystem::path root = overlayLibrary_.rootDirectory();
    const std::string displayName = overlayImportName(source, pending.mediaType);
    overlayImportControl_ = std::make_shared<OverlayImportControl>();
    const std::shared_ptr<OverlayImportControl> control = overlayImportControl_;

    try
    {
        overlayImportFuture_ = std::async(
            std::launch::async,
            [root, source, displayName, pending, control]() mutable {
                OverlayImportJobResult result;
                try
                {
                    auto library = std::make_unique<OverlayLibrary>(root);
                    if (!library->scan(result.error)) return result;

                    OverlayImportSource selected;
                    selected.path = source;
                    selected.aspect = overlayAspect(pending.format);
                    selected.inferAspect = pending.action == OverlayImportAction::NewAsset;

                    OverlayAsset asset;
                    bool ok = false;
                    if (pending.action == OverlayImportAction::NewAsset)
                    {
                        OverlayImportRequest request;
                        request.name = displayName;
                        request.sources.push_back(std::move(selected));
                        ok = library->importAsset(request, asset, result.error, control.get());
                    }
                    else
                    {
                        ok = library->replaceVariant(pending.assetId, selected, asset,
                                                     result.error, control.get());
                    }
                    if (!ok)
                    {
                        result.cancelled = control->cancelRequested.load(
                            std::memory_order_acquire);
                        return result;
                    }
                    result.assetId = asset.id;
                    result.assetName = asset.name;
                    result.library = std::move(library);
                }
                catch (const std::exception& exception)
                {
                    result.error = exception.what();
                }
                return result;
            });
    }
    catch (const std::exception& exception)
    {
        overlayImportControl_.reset();
        overlayPanelSnapshot_.import.phase = OverlayImportPhase::Failed;
        overlayPanelSnapshot_.import.status = exception.what();
        return;
    }

    overlayPanelSnapshot_.import = {};
    overlayPanelSnapshot_.import.phase = OverlayImportPhase::Validating;
    overlayPanelSnapshot_.import.cancellable = true;
    overlayPanelSnapshot_.import.status = "Validating PNG";
}

void App::consumeOverlayCommand()
{
    if (overlayCommand_.type == OverlayUiCommandType::None) return;
    OverlayUiCommand command = std::move(overlayCommand_);
    overlayCommand_ = {};

    const bool structural = command.type == OverlayUiCommandType::MoveLayer ||
        command.type == OverlayUiCommandType::RemoveLayer ||
        command.type == OverlayUiCommandType::AddLayer ||
        command.type == OverlayUiCommandType::BeginImport ||
        command.type == OverlayUiCommandType::AddVariant ||
        command.type == OverlayUiCommandType::ReplaceVariant ||
        command.type == OverlayUiCommandType::RemoveAsset;
    if (structural && operationLocked_)
    {
        overlayPanelSnapshot_.status = "Unlock Operation to change overlay structure";
        return;
    }

    std::string error;
    switch (command.type)
    {
    case OverlayUiCommandType::SetLayerEnabled:
        if (!overlaySystem_.setLayerEnabled(command.layerId, command.enabled, error))
            overlayPanelSnapshot_.status = error.empty() ? "Overlay layer not found" : error;
        break;
    case OverlayUiCommandType::SetLayerOpacity:
        overlaySystem_.setLayerOpacity(command.layerId, command.opacity);
        break;
    case OverlayUiCommandType::SetLayerPlayback:
        overlaySystem_.setLayerPlayback(command.layerId, overlayPlayback(command.playback));
        break;
    case OverlayUiCommandType::SetLayerFps:
        overlaySystem_.setLayerFramesPerSecond(command.layerId, command.fps);
        break;
    case OverlayUiCommandType::SetLayerPaused:
        overlaySystem_.setLayerPaused(command.layerId, command.paused);
        break;
    case OverlayUiCommandType::RestartLayer:
        if (!overlaySystem_.triggerLayer(command.layerId, error))
            overlayPanelSnapshot_.status = error.empty() ? "Overlay layer not found" : error;
        break;
    case OverlayUiCommandType::MoveLayer:
        overlaySystem_.moveLayer(command.layerId, command.moveDelta);
        break;
    case OverlayUiCommandType::RemoveLayer:
        overlaySystem_.removeLayer(command.layerId);
        break;
    case OverlayUiCommandType::AddLayer:
    {
        const OverlayLayerId layer = overlaySystem_.addLayer(command.assetId, error);
        if (layer == 0 || !error.empty())
            overlayPanelSnapshot_.status = error.empty() ? "Could not add overlay layer" : error;
        break;
    }
    case OverlayUiCommandType::BeginImport:
    case OverlayUiCommandType::AddVariant:
    case OverlayUiCommandType::ReplaceVariant:
        beginOverlayPicker(command);
        break;
    case OverlayUiCommandType::RemoveAsset:
    {
        const auto found = std::find_if(
            overlayAssetUi_.begin(), overlayAssetUi_.end(),
            [&](const OverlayAssetUiState& asset) { return asset.id == command.assetId; });
        if (found == overlayAssetUi_.end() || found->inStack || found->presetReferences > 0)
        {
            overlayPanelSnapshot_.status = "Remove active and preset references first";
            break;
        }
        if (!overlayLibrary_.removeAsset(command.assetId, error))
        {
            overlayPanelSnapshot_.status = error;
            break;
        }
        overlaySystem_.setLibrary(overlayLibrary_);
        rebuildOverlayAssetUi();
        ui::inspectOverlayLibrary();
        overlayPanelSnapshot_.status = "Overlay moved to the library trash";
        break;
    }
    case OverlayUiCommandType::CancelImport:
        if (overlayPicker_) overlayPicker_->cancel();
        if (overlayImportControl_)
            overlayImportControl_->cancelRequested.store(true, std::memory_order_release);
        overlayPanelSnapshot_.import.status = "Cancelling import";
        break;
    case OverlayUiCommandType::DismissStatus:
        overlayPanelSnapshot_.status.clear();
        if (overlayPanelSnapshot_.import.phase == OverlayImportPhase::Failed)
            overlayPanelSnapshot_.import = {};
        break;
    case OverlayUiCommandType::None:
        break;
    }
}

void App::serviceOverlays()
{
    if (!overlaysInitialized_) return;
    overlaySystem_.commitControlPlane();
    consumeOverlayCommand();

    if (operationLocked_)
    {
        if (overlayPicker_ && overlayPicker_->state() == OverlayPickerState::Picking)
            overlayPicker_->cancel();
        if (overlayImportControl_)
            overlayImportControl_->cancelRequested.store(true, std::memory_order_release);
    }

    OverlayPickerResult picked;
    if (overlayPicker_ && overlayPicker_->poll(picked))
    {
        if (picked.state == OverlayPickerState::Selected)
        {
            launchOverlayImport(picked.path);
        }
        else if (picked.state == OverlayPickerState::Failed)
        {
            overlayPanelSnapshot_.import.phase = OverlayImportPhase::Failed;
            overlayPanelSnapshot_.import.status = picked.error;
        }
        else
        {
            overlayPanelSnapshot_.import = {};
        }
    }

    if (overlayImportFuture_.valid())
    {
        if (overlayImportControl_)
        {
            const float progress = overlayImportControl_->progress.load(std::memory_order_acquire);
            overlayPanelSnapshot_.import.progress = progress;
            overlayPanelSnapshot_.import.cancellable = true;
            overlayPanelSnapshot_.import.phase = progress < 0.08f
                ? OverlayImportPhase::Validating
                : (progress < 0.90f ? OverlayImportPhase::Copying
                                    : OverlayImportPhase::Preparing);
            overlayPanelSnapshot_.import.status = progress < 0.08f
                ? "Validating PNG" : (progress < 0.90f ? "Copying to library"
                                                       : "Publishing asset");
            if (overlayImportControl_->cancelRequested.load(std::memory_order_acquire))
                overlayPanelSnapshot_.import.status = "Cancelling import";
        }

        if (overlayImportFuture_.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready)
        {
            OverlayImportJobResult result = overlayImportFuture_.get();
            overlayImportControl_.reset();
            if (result.library)
            {
                overlayLibrary_ = std::move(*result.library);
                overlaySystem_.setLibrary(overlayLibrary_);
                rebuildOverlayAssetUi();
                ++overlayImportSerial_;
                overlayPanelSnapshot_.lastImportedAssetId = result.assetId;
                overlayPanelSnapshot_.status = "Imported: " + result.assetName;
                overlayPanelSnapshot_.import = {};
            }
            else if (result.cancelled)
            {
                overlayPanelSnapshot_.import = {};
            }
            else
            {
                overlayPanelSnapshot_.import.phase = OverlayImportPhase::Failed;
                overlayPanelSnapshot_.import.progress = 0.0f;
                overlayPanelSnapshot_.import.cancellable = false;
                overlayPanelSnapshot_.import.status = result.error.empty()
                    ? "Overlay import failed" : result.error;
            }
        }
    }
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
        const auto frameStarted = std::chrono::steady_clock::now();
        timing_.beginFrame();
        const bool havePreview = renderFrame();

        if (options_.frames > 0 && timing_.frameIndex() >= static_cast<uint64_t>(options_.frames))
        {
            reportTimings();
            window_->destroy();
        }
        else if (!havePreview && !outputSurface_)
        {
            // With neither display pacing the loop, a hidden webcam still
            // needs frames, but must not flood the GPU queue in a busy loop.
            // This M0 fallback yields outside processing; it is not genlock
            // and never waits for a GPU result or a camera consumer.
            std::this_thread::sleep_until(frameStarted +
                std::chrono::microseconds(16683));
        }
    });

    return webcamResult();
}

bool App::renderFrame()
{
    // Route loss, Escape and webcam startup/shutdown must be serviced even
    // when the operator's monitor cannot provide a preview drawable.
    serviceOutput();
    serviceWebcam();
    servicePresets();
    serviceOverlays();

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

    if (!havePreview && !outputSurface_ && !webcam_)
    {
        return false;
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
    overlaySystem_.service(effectContext_);
    if (frame)
    {
        frame = &overlaySystem_.composite(effectContext_, *frame, effectMix,
                                          source_->bypassEffects());
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
        refreshOverlayUi();

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
        // Two dissolves can be running at once — Freeze lifting off a picture
        // that is itself still fading from Clean to FX. The readout follows the
        // one the audience is watching, which is always the outer one. A chain
        // mix held mid-ramp behind Freeze or Black is not in progress at all,
        // and a percentage that never moves reads as a hung machine.
        const bool chainMixing = programTransition_.active() &&
            (programMode_ == ProgramMode::Effects || programMode_ == ProgramMode::Clean);
        state.programMixing       = programOutput_.transitioning() || chainMixing;
        state.programProgress     = programOutput_.transitioning()
            ? programOutput_.progress()
            : programTransition_.progress();
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
        state.recallPresetId      = &recallPresetId_;
        state.savePresetName      = &savePresetName_;
        state.requestPresetSave   = &requestPresetSave_;
        state.overlays            = &overlayPanelSnapshot_;
        state.overlayCommand      = &overlayCommand_;
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
    return havePreview;
}

void App::reportTimings() const
{
    ATEMFX_LOG_INFO("--- CamVJ %s timing -----------------------------", kVersion);
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
    // Capture venue routing while the chain and display selection still exist.
    persistBootState();

    if (overlayPicker_) overlayPicker_->cancel();
    if (overlayImportControl_)
        overlayImportControl_->cancelRequested.store(true, std::memory_order_release);
    if (overlayImportFuture_.valid())
    {
        try
        {
            static_cast<void>(overlayImportFuture_.get());
        }
        catch (const std::exception& exception)
        {
            ATEMFX_LOG_WARN("Overlay import shutdown: %s", exception.what());
        }
        catch (...)
        {
            ATEMFX_LOG_WARN("Overlay import shutdown: unknown failure");
        }
    }
    overlayImportControl_.reset();
    overlayPicker_.reset();

    ui_.shutdown();
    programOutput_.shutdown();
    overlaySystem_.shutdown();
    overlaysInitialized_ = false;
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
