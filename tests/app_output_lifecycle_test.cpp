#include "app/App.h"

#include <array>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "effects/BuiltinEffects.h"
#include "effects/EffectRegistry.h"
#include "gpu/Backend.h"

namespace {

using namespace atemfx;

int checks = 0;
int failures = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

// Drive the public App lifetime with deterministic platform events. The fake
// textures carry frame identities so the real chain and PROGRAM policy still
// have to deliver the correct image to the output consumers.
struct Scenario
{
    int frameCount = 4;
    int frame = -1;
    bool minimized = true;
    bool drawable = true;
    bool processing = false;
    bool displayConnected = true;
    bool closeDisplay = false;
    bool surfaceAlive = false;
    VirtualCameraState webcamState = VirtualCameraState::Sending;
    std::function<void(int)> beforeFrame;
    std::function<void(UiFrameState&)> uiAction;
    int previewBegins = 0;
    int previewEnds = 0;
    int uiFrames = 0;
    int processingBegins = 0;
    int sourceFrames = 0;
    int webcamCreates = 0;
    int webcamDestroys = 0;
    int webcamDestroyedAt = -1;
    int webcamStops = 0;
    int webcamErrors = 0;
    int displayOpens = 0;
    int displayCloses = 0;
    int displayEnumerations = 0;
    std::vector<uint64_t> webcamFrames;
    std::vector<uint64_t> displayFrames;
};

Scenario* scenario = nullptr;

class TestTexture final : public GpuTexture
{
public:
    TestTexture(uint32_t width = 1920, uint32_t height = 1080)
        : width_(width), height_(height) {}

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }
    bool valid() const override { return true; }
    void* nativeTexture() const override { return nullptr; }

    uint64_t frame = 0;

private:
    uint32_t width_;
    uint32_t height_;
};

class TestTargets final : public TargetPool
{
public:
    GpuTexture& scratch(std::size_t index) override { return scratch_[index % 2]; }
    GpuTexture& persistent(const std::string& key) override
    {
        return target(key, width(), height());
    }
    GpuTexture& uploadTarget(const std::string& key, uint32_t width, uint32_t height) override
    {
        return target(key, width, height);
    }
    bool upload(GpuTexture& texture, const void* pixels, std::size_t rowBytes) override
    {
        static_cast<TestTexture&>(texture).frame = 0;
        return pixels && rowBytes >= texture.width() * 4;
    }
    uint32_t width() const override { return 1920; }
    uint32_t height() const override { return 1080; }

private:
    GpuTexture& target(const std::string& key, uint32_t width, uint32_t height)
    {
        auto& texture = targets_[key];
        if (!texture) texture = std::make_unique<TestTexture>(width, height);
        return *texture;
    }

    std::array<TestTexture, 2> scratch_;
    std::map<std::string, std::unique_ptr<TestTexture>> targets_;
};

class TestShaders final : public ShaderLibrary
{
public:
    ShaderHandle shader(const std::string&, std::string*) override { return this; }
    bool reloadAll(std::string&) override { return true; }
    std::string directory() const override { return {}; }
};

class TestPass final : public FullscreenPass
{
public:
    void draw(GpuTexture& target, ShaderHandle shader, const GpuTexture* source,
              const EffectConstants&, SamplerFilter, const GpuTexture*) override
    {
        expect(scenario->processing, "PROGRAM copies stay inside the processing frame");
        expect(shader && source && source != &target, "PROGRAM copies distinct valid resources");
        if (source) static_cast<TestTexture&>(target).frame =
            static_cast<const TestTexture&>(*source).frame;
    }
};

class TestSurface final : public OutputSurface
{
public:
    TestSurface() { scenario->surfaceAlive = true; }
    ~TestSurface() override { scenario->surfaceAlive = false; }
    void present(GpuTexture& texture) override
    {
        expect(!scenario->processing, "display presents after processing is committed");
        scenario->displayFrames.push_back(static_cast<TestTexture&>(texture).frame);
    }
    void resize(uint32_t, uint32_t) override {}
    uint32_t width() const override { return 1920; }
    uint32_t height() const override { return 1080; }
};

class TestDevice final : public GraphicsDevice
{
public:
    bool initialize(Window*, uint32_t, uint32_t) override { return true; }
    void shutdown() override {}
    bool beginFrame() override
    {
        ++scenario->previewBegins;
        return scenario->drawable;
    }
    void beginProcessing() override
    {
        expect(!scenario->processing, "processing frames do not overlap");
        scenario->processing = true;
        ++scenario->processingBegins;
    }
    void endProcessing() override { scenario->processing = false; }
    void beginUi() override {}
    void endFrame(bool) override { ++scenario->previewEnds; }
    void onWindowResized(uint32_t, uint32_t) override {}
    std::unique_ptr<OutputSurface> createOutputSurface(void*, uint32_t, uint32_t) override
    {
        return std::make_unique<TestSurface>();
    }
    ShaderLibrary& shaders() override { return shaders_; }
    FullscreenPass& fullscreenPass() override { return pass_; }
    TargetPool& targets() override { return targets_; }
    float lastGpuMilliseconds() const override { return 0; }
    bool hasGpuTiming() const override { return false; }
    const std::string& adapterName() const override { return name_; }
    bool readback(GpuTexture&, std::vector<uint8_t>&) override { return false; }

private:
    TestShaders shaders_;
    TestPass pass_;
    TestTargets targets_;
    std::string name_ = "Lifecycle test";
};

class TestWindow final : public Window
{
public:
    bool create(const std::string&, uint32_t, uint32_t) override { return true; }
    void destroy() override { closed_ = true; }
    void runFrameLoop(const FrameCallback& onFrame) override
    {
        for (int i = 0; i < scenario->frameCount && !closed_; ++i)
        {
            scenario->frame = i;
            if (scenario->beforeFrame) scenario->beforeFrame(i);
            onFrame();
        }
    }
    uint32_t width() const override { return 1600; }
    uint32_t height() const override { return 900; }
    bool minimized() const override { return scenario->minimized; }
    void* nativeHandle() const override { return nullptr; }

private:
    bool closed_ = false;
};

class TestOutputWindow final : public OutputWindow
{
public:
    bool open(const std::string&) override
    {
        ++scenario->displayOpens;
        open_ = true;
        return true;
    }
    void close() override
    {
        expect(!scenario->surfaceAlive, "output surface is released before its window closes");
        if (open_) ++scenario->displayCloses;
        open_ = false;
    }
    bool isOpen() const override { return open_; }
    void* nativeHandle() const override { return nullptr; }
    uint32_t width() const override { return 1920; }
    uint32_t height() const override { return 1080; }
    bool consumeCloseRequest() override { return std::exchange(scenario->closeDisplay, false); }

private:
    bool open_ = false;
};

class TestWebcam final : public VirtualCameraOutput
{
public:
    ~TestWebcam() override
    {
        ++scenario->webcamDestroys;
        scenario->webcamDestroyedAt = scenario->frame;
    }
    void submit(GpuTexture& texture) override
    {
        expect(scenario->processing, "webcam receives PROGRAM before processing is committed");
        scenario->webcamFrames.push_back(static_cast<TestTexture&>(texture).frame);
    }
    void requestStop() override
    {
        ++scenario->webcamStops;
        scenario->webcamState = VirtualCameraState::Stopping;
    }
    VirtualCameraStats stats() const override
    {
        return {scenario->webcamState, static_cast<uint64_t>(scenario->webcamFrames.size()), 0};
    }
    const std::string& error() const override
    {
        ++scenario->webcamErrors;
        return error_;
    }

private:
    std::string error_ = "Camera disconnected";
};

class TestSource final : public VideoSource
{
public:
    explicit TestSource(VideoSourceDescriptor descriptor) : VideoSource(std::move(descriptor)) {}
    bool initialize(EffectContext&, std::string&) override { return true; }
    void shutdown() override {}
    GpuTexture* render(EffectContext&) override
    {
        texture_.frame = static_cast<uint64_t>(++scenario->sourceFrames);
        return &texture_;
    }
    std::string status() const override { return "Generated frames"; }
    SourceHealth health() const override
    {
        SourceHealth health;
        health.signal = SourceSignal::Generated;
        return health;
    }

private:
    TestTexture texture_;
};

class TestEffect final : public Effect
{
public:
    explicit TestEffect(const char* id) : Effect({id, id, "Test", {}}) {}
    bool initialize(EffectContext&) override { return true; }
    bool process(EffectContext&, const GpuTexture& source, GpuTexture& destination) override
    {
        static_cast<TestTexture&>(destination).frame = static_cast<const TestTexture&>(source).frame;
        return true;
    }
    void shutdown() override {}
};

void run(Scenario& test, const AppOptions& options,
         const std::function<void(int)>& checkBeforeShutdown)
{
    scenario = &test;
    App app;
    const bool initialized = app.initialize(options);
    expect(initialized, "application initializes through its public factory seams");
    if (initialized)
    {
        const int result = app.run();
        expect(!test.processing, "run leaves no processing frame open");
        checkBeforeShutdown(result);
    }
    app.shutdown();
}

void checkHiddenWebcam()
{
    for (const bool minimized : {true, false})
    {
        Scenario test;
        test.minimized = minimized;
        test.drawable = false;
        AppOptions options;
        options.webcam = true;
        run(test, options, [&](int result) {
            expect(result == 0, "webcam run succeeds without a preview drawable");
            expect(test.webcamCreates == 1, "pending CLI webcam starts while preview is hidden");
            expect(test.webcamFrames == std::vector<uint64_t>({1, 2, 3, 4}),
                   "webcam receives every fresh PROGRAM frame without a preview");
            expect(test.processingBegins == 4, "webcam alone keeps the pipeline running");
            expect(test.previewBegins == (minimized ? 0 : 4),
                   "minimized preview never tries to acquire a drawable");
            expect(test.previewEnds == 0 && test.uiFrames == 0,
                   "missing drawable never reaches UI or preview presentation");
        });
    }
}

void checkQueuedWebcamStart()
{
    Scenario test;
    test.beforeFrame = [&](int frame) { test.minimized = frame != 0; };
    test.uiAction = [](UiFrameState& state) { *state.requestWebcamStart = true; };
    run(test, {}, [&](int result) {
        expect(result == 0 && test.uiFrames == 1, "operator queues webcam start before minimizing");
        expect(test.webcamCreates == 1, "queued operator webcam start survives minimization");
        expect(test.webcamFrames == std::vector<uint64_t>({2, 3, 4}),
               "operator webcam starts on the following frame and remains live");
    });
}

void checkWebcamTerminalStates()
{
    for (const auto terminal : {VirtualCameraState::Stopped, VirtualCameraState::Failed})
    {
        Scenario test;
        test.beforeFrame = [&](int frame) { if (frame == 1) test.webcamState = terminal; };
        AppOptions options;
        options.webcam = true;
        run(test, options, [&](int result) {
            expect(result == (terminal == VirtualCameraState::Failed ? 1 : 0),
                   "hidden webcam failure reaches the CLI exit result");
            expect(test.webcamDestroys == 1 && test.webcamDestroyedAt == 1,
                   "terminal webcam transport is released on the next hidden frame");
            expect(test.webcamErrors == (terminal == VirtualCameraState::Failed ? 1 : 0),
                   "failed transport exposes its diagnostic exactly once");
            expect(test.webcamFrames == std::vector<uint64_t>({1}),
                   "no frames are offered after the transport terminates");
            expect(test.processingBegins == 1, "pipeline sleeps when the last hidden output terminates");
        });
    }
}

void checkQueuedWebcamStop()
{
    Scenario test;
    test.beforeFrame = [&](int frame) {
        test.minimized = frame != 0;
        if (frame == 2) test.webcamState = VirtualCameraState::Stopped;
    };
    test.uiAction = [](UiFrameState& state) { *state.requestWebcamStop = true; };
    AppOptions options;
    options.webcam = true;
    run(test, options, [&](int result) {
        expect(result == 0, "operator stop is a successful run");
        expect(test.webcamStops == 1, "queued webcam stop is serviced while hidden");
        expect(test.webcamDestroys == 1 && test.webcamDestroyedAt == 2,
               "stopping webcam survives until its worker reports stopped");
    });
}

void checkDisplayLifecycle()
{
    Scenario opening;
    opening.beforeFrame = [&](int frame) { opening.minimized = frame != 0; };
    opening.uiAction = [](UiFrameState& state) { *state.requestedDisplay = 0; };
    run(opening, {}, [&](int result) {
        expect(result == 0 && opening.displayOpens == 1,
               "queued display selection opens while the preview is minimized");
        expect(opening.displayFrames == std::vector<uint64_t>({2, 3, 4}),
               "display alone receives every PROGRAM frame while preview is hidden");
    });

    Scenario closing;
    closing.closeDisplay = true;
    AppOptions options;
    options.outputDisplayId = "wall";
    run(closing, options, [&](int result) {
        expect(result == 0 && closing.displayCloses == 1,
               "output Escape closes the display while the preview is minimized");
        expect(closing.processingBegins == 0 && closing.displayFrames.empty(),
               "closing the last hidden output takes effect before processing");
    });

    Scenario disconnected;
    disconnected.beforeFrame = [&](int frame) {
        disconnected.minimized = frame != 0;
        disconnected.displayConnected = frame == 0;
    };
    disconnected.uiAction = [](UiFrameState& state) { *state.requestDisplayRescan = true; };
    run(disconnected, options, [&](int result) {
        expect(result == 0 && disconnected.displayEnumerations == 2,
               "queued display rescan is serviced while the preview is minimized");
        expect(disconnected.displayCloses == 1 && disconnected.processingBegins == 1,
               "unplugged display is released before deciding whether processing is needed");
    });
}

void checkNoConsumers()
{
    Scenario test;
    run(test, {}, [&](int result) {
        expect(result == 0 && test.processingBegins == 0 && test.previewBegins == 0,
               "hidden application without output avoids drawable acquisition and processing");
    });
}

} // namespace

namespace atemfx {

std::unique_ptr<Window> createPlatformWindow() { return std::make_unique<TestWindow>(); }
std::unique_ptr<GraphicsDevice> createGraphicsDevice() { return std::make_unique<TestDevice>(); }
const char* backendName() { return "Test"; }
std::unique_ptr<OutputWindow> createOutputWindow() { return std::make_unique<TestOutputWindow>(); }
std::unique_ptr<Tracker> createSubjectTracker() { return nullptr; }
bool virtualCameraSupported() { return true; }
std::unique_ptr<VirtualCameraOutput> createVirtualCameraOutput(GraphicsDevice&, std::string&)
{
    ++scenario->webcamCreates;
    return std::make_unique<TestWebcam>();
}
std::vector<DisplayInfo> enumerateDisplays()
{
    ++scenario->displayEnumerations;
    return scenario->displayConnected ? std::vector<DisplayInfo>{{"wall", "Wall", 1920, 1080, 60, false}}
                                      : std::vector<DisplayInfo>{};
}
std::vector<VideoSourceDescriptor> enumerateVideoSources()
{
    return {{"test", "Test", "Internal"}};
}
bool consumeVideoDeviceHotplug() { return false; }
std::unique_ptr<VideoSource> createVideoSource(const VideoSourceDescriptor& descriptor)
{
    return std::make_unique<TestSource>(descriptor);
}
void registerBuiltinEffects(EffectRegistry& registry)
{
    for (const char* id : {"auto_frame", "passthrough", "rgb_split", "pixelate", "fm_raster",
                          "subpixel", "shutter", "frame_delay", "mirror", "vhs", "crt"})
    {
        if (!registry.find(id)) registry.add([id] { return std::make_unique<TestEffect>(id); });
    }
}
bool UiLayer::initialize(Window&, GraphicsDevice&) { return true; }
void UiLayer::shutdown() {}
void UiLayer::beginFrame(Window&, GraphicsDevice&) {}
void UiLayer::draw(UiFrameState& state)
{
    ++scenario->uiFrames;
    if (scenario->uiAction) scenario->uiAction(state);
}
void UiLayer::render(GraphicsDevice&) {}

} // namespace atemfx

int main()
{
    checkHiddenWebcam();
    checkQueuedWebcamStart();
    checkWebcamTerminalStates();
    checkQueuedWebcamStop();
    checkDisplayLifecycle();
    checkNoConsumers();
    std::printf("App output lifecycle: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
