#include "video/DeckLinkSource.h"

#include <cstring>
#include <utility>

#include "core/Log.h"
#include "effects/Effect.h"
#include "gpu/EffectConstants.h"

namespace atemfx {
namespace {

constexpr const char* kShaderName = "source_blit";
constexpr const char* kTargetKey  = "source.frame";
constexpr const char* kUploadKey  = "source.decklink";

constexpr std::size_t kSourceWidthSlot  = 2;
constexpr std::size_t kSourceHeightSlot = 3;
constexpr std::size_t kFlipVerticalSlot = 4;

} // namespace

DeckLinkSource::DeckLinkSource(VideoSourceDescriptor descriptor)
    : VideoSource(std::move(descriptor))
{
}

DeckLinkSource::~DeckLinkSource()
{
    shutdown();
}

bool DeckLinkSource::initialize(EffectContext& context, std::string& error)
{
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

    capture_ = createDeckLinkCapture();
    if (!capture_)
    {
        error = "DeckLink capture unavailable in this build";
        ATEMFX_LOG_ERROR("DeckLink source '%s': %s", descriptor_.displayName.c_str(), error.c_str());
        return false;
    }

    const std::string deviceId = descriptor_.id.substr(std::strlen(kDeckLinkSourceIdPrefix));
    if (!capture_->start(deviceId, [this](const DeckLinkCaptureFrame& frame) { onFrame(frame); }, error))
    {
        ATEMFX_LOG_ERROR("DeckLink source '%s': %s", descriptor_.displayName.c_str(), error.c_str());
        return false;
    }

    ATEMFX_LOG_INFO("Opened DeckLink source '%s'", descriptor_.displayName.c_str());
    return true;
}

void DeckLinkSource::shutdown()
{
    if (capture_)
    {
        capture_->stop();
        capture_.reset();
    }
    frameObserver_ = nullptr;
    target_        = nullptr;
}

void DeckLinkSource::setFrameObserver(FrameObserver observer)
{
    frameObserver_ = std::move(observer);
}

void DeckLinkSource::onFrame(const DeckLinkCaptureFrame& frame)
{
    if (!frame.pixels || frame.width == 0 || frame.height == 0 || frame.rowBytes == 0)
    {
        return;
    }

    if (frameObserver_)
    {
        frameObserver_(frame.pixels, frame.width, frame.height, frame.rowBytes, frame.bottomUp);
    }

    const std::size_t bytes = frame.rowBytes * frame.height;

    std::lock_guard<std::mutex> lock(mutex_);
    pending_.resize(bytes);
    std::memcpy(pending_.data(), frame.pixels, bytes);

    pendingWidth_    = frame.width;
    pendingHeight_   = frame.height;
    pendingRowBytes_ = frame.rowBytes;
    pendingBottomUp_ = frame.bottomUp;
    hasPending_      = true;
    ++framesReceived_;
}

GpuTexture* DeckLinkSource::render(EffectContext& context)
{
    if (capture_)
    {
        capture_->poll();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (hasPending_)
        {
            current_.swap(pending_);
            frameWidth_     = pendingWidth_;
            frameHeight_    = pendingHeight_;
            frameRowBytes_  = pendingRowBytes_;
            frameBottomUp_  = pendingBottomUp_;
            hasPending_     = false;
        }
    }

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

    EffectConstants constants;
    setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);
    setParameterConstant(constants, kSourceWidthSlot, static_cast<float>(frameWidth_));
    setParameterConstant(constants, kSourceHeightSlot, static_cast<float>(frameHeight_));
    setParameterConstant(constants, kFlipVerticalSlot, frameBottomUp_ ? 1.0f : 0.0f);

    context.fullscreen->draw(*target_, shader, &upload, constants, SamplerFilter::Linear);
    return target_;
}

std::string DeckLinkSource::status() const
{
    if (capture_)
    {
        const std::string problem = capture_->status();
        if (!problem.empty())
        {
            return problem;
        }
    }
    else
    {
        return "DeckLink capture unavailable in this build";
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (framesReceived_ == 0)
    {
        return "waiting for the first SDI frame";
    }

    return std::to_string(pendingWidth_ != 0 ? pendingWidth_ : frameWidth_) + "x" +
           std::to_string(pendingHeight_ != 0 ? pendingHeight_ : frameHeight_) + "  ·  " +
           std::to_string(framesReceived_) + " SDI frames";
}

} // namespace atemfx
