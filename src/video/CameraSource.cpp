#include "video/CameraSource.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "core/Log.h"
#include "effects/Effect.h"
#include "video/VideoDevices.h"

namespace atemfx {

namespace {

constexpr const char* kShaderName  = "source_blit";
constexpr const char* kTargetKey   = "source.frame";
constexpr const char* kUploadKey   = "source.camera";

// Parameter slots in source_blit. The first two are the operator's; the last
// two are the frame's own dimensions, which the shader needs and the user has
// no business editing.
constexpr std::size_t kUserParameterCount = 2;
constexpr std::size_t kSourceWidthSlot    = 2;
constexpr std::size_t kSourceHeightSlot   = 3;
constexpr std::size_t kFlipVerticalSlot   = 4;

double sourceClockSeconds()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

CameraSource::CameraSource(VideoSourceDescriptor descriptor)
    : VideoSource(std::move(descriptor))
{
}

CameraSource::~CameraSource()
{
    shutdown();
}

bool CameraSource::initialize(EffectContext& context, std::string& error)
{
    parameters_.add(Parameter::makeInt("fit", "Fit", 0, 0, 2));  // 0 fit, 1 fill, 2 stretch
    parameters_.add(Parameter::makeBool("mirror", "Mirror", false));

    target_ = &context.targets->persistent(kTargetKey);
    if (!target_->valid())
    {
        error = "Failed to allocate the source target";
        return false;
    }

    if (context.shaders->shader(kShaderName, &error) == nullptr)
    {
        return false;
    }

    capture_ = createCameraCapture();
    if (!capture_)
    {
        error = "No camera capture backend on this platform";
        return false;
    }

    const std::string deviceId = descriptor_.id.substr(std::strlen(kCameraSourceIdPrefix));
    if (!capture_->start(deviceId, [this](const CameraFrame& frame) { onFrame(frame); }, error))
    {
        // A camera that will not open is a reportable state, not a fatal one:
        // the operator can pick another input without restarting.
        ATEMFX_LOG_ERROR("Camera '%s': %s", descriptor_.displayName.c_str(), error.c_str());
        return false;
    }

    ATEMFX_LOG_INFO("Opened camera '%s'", descriptor_.displayName.c_str());
    return true;
}

void CameraSource::shutdown()
{
    // Stop capture first: after this returns no more frames are in flight, so
    // dropping the observer cannot race with a call already under way.
    if (capture_)
    {
        capture_->stop();
        capture_.reset();
    }
    frameObserver_ = nullptr;
    target_        = nullptr;
}

void CameraSource::setFrameObserver(FrameObserver observer)
{
    frameObserver_ = std::move(observer);
}

SourceMapping CameraSource::mapping() const
{
    SourceMapping mapping;
    mapping.mirrored = parameters_.valueOr("mirror", 0.0f) >= 0.5f;

    if (frameWidth_ == 0 || frameHeight_ == 0)
    {
        return mapping;
    }

    // The same arithmetic as source_blit, and it has to stay the same: this is
    // the inverse of what the shader did to the picture the tracker looked at.
    const int   mode         = static_cast<int>(parameters_.valueOr("fit", 0.0f) + 0.5f);
    const float sourceAspect = static_cast<float>(frameWidth_) / static_cast<float>(frameHeight_);

    if (mode == 0)
    {
        if (sourceAspect > canvasAspect_) mapping.scaleY = sourceAspect / canvasAspect_;
        else                              mapping.scaleX = canvasAspect_ / sourceAspect;
    }
    else if (mode == 1)
    {
        if (sourceAspect > canvasAspect_) mapping.scaleX = canvasAspect_ / sourceAspect;
        else                              mapping.scaleY = sourceAspect / canvasAspect_;
    }

    return mapping;
}

void CameraSource::onFrame(const CameraFrame& frame)
{
    if (!frame.pixels || frame.width == 0 || frame.height == 0)
    {
        return;
    }

    const double arrivalSeconds = sourceClockSeconds();

    // Before the copy below, so the tracker sees the frame even when the
    // render thread is behind. It copies only the frames it is ready for.
    if (frameObserver_)
    {
        frameObserver_(frame.pixels, frame.width, frame.height, frame.rowBytes, frame.bottomUp);
    }

    const std::size_t bytes = frame.rowBytes * frame.height;

    std::lock_guard<std::mutex> lock(mutex_);

    // resize() only reallocates when the camera changes format, so the steady
    // state is a memcpy under a lock held for the length of one frame copy.
    pending_.resize(bytes);
    std::memcpy(pending_.data(), frame.pixels, bytes);

    pendingWidth_    = frame.width;
    pendingHeight_   = frame.height;
    pendingRowBytes_ = frame.rowBytes;
    pendingBottomUp_ = frame.bottomUp;
    captureMonitor_.recordFrame(arrivalSeconds, hasPending_);
    hasPending_      = true;
}

GpuTexture* CameraSource::render(EffectContext& context)
{
    if (capture_)
    {
        capture_->poll();
    }

    bool consumedFrame = false;
    SourceCaptureHealth captureHealth;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (hasPending_)
        {
            // Swap rather than copy: the capture thread gets our old buffer to
            // fill next time, so neither side allocates in steady state.
            current_.swap(pending_);
            frameWidth_    = pendingWidth_;
            frameHeight_   = pendingHeight_;
            frameRowBytes_ = pendingRowBytes_;
            frameBottomUp_ = pendingBottomUp_;
            hasPending_    = false;
            consumedFrame  = true;
        }
        captureHealth = captureMonitor_.snapshot();
    }
    healthMonitor_.update(captureHealth, consumedFrame, sourceClockSeconds());

    if (frameWidth_ == 0 || frameHeight_ == 0 || !target_ || !target_->valid())
    {
        return nullptr;
    }

    const ShaderHandle shader = context.shaders->shader(kShaderName);
    if (!shader)
    {
        return nullptr;
    }

    GpuTexture& upload = context.targets->uploadTarget(kUploadKey, frameWidth_, frameHeight_);
    if (!upload.valid() || !context.targets->upload(upload, current_.data(), frameRowBytes_))
    {
        return nullptr;
    }

    canvasAspect_ = context.height > 0
        ? static_cast<float>(context.width) / static_cast<float>(context.height)
        : canvasAspect_;

    EffectConstants constants;
    setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);

    const std::vector<Parameter>& parameters = parameters_.all();
    const std::size_t             count      = std::min(parameters.size(), kUserParameterCount);
    for (std::size_t i = 0; i < count; ++i)
    {
        setParameterConstant(constants, i, parameters[i].value);
    }

    setParameterConstant(constants, kSourceWidthSlot, static_cast<float>(frameWidth_));
    setParameterConstant(constants, kSourceHeightSlot, static_cast<float>(frameHeight_));
    setParameterConstant(constants, kFlipVerticalSlot, frameBottomUp_ ? 1.0f : 0.0f);

    context.fullscreen->draw(*target_, shader, &upload, constants, SamplerFilter::Linear);
    return target_;
}

std::string CameraSource::status() const
{
    if (capture_)
    {
        const std::string problem = capture_->status();
        if (!problem.empty())
        {
            return problem;
        }
    }

    const SourceHealth snapshot = health();
    if (snapshot.signal == SourceSignal::Waiting)
    {
        return "waiting for the first frame";
    }

    return std::to_string(frameWidth_) + "x" + std::to_string(frameHeight_) + "  ·  " +
           std::to_string(snapshot.receivedFrames) + " frames";
}

} // namespace atemfx
