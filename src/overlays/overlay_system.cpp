#include "overlays/overlay_system.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include "overlays/overlay_compositor.h"
#include "overlays/overlay_library.h"
#include "overlays/overlay_platform.h"
#include "overlays/overlay_playback.h"

namespace atemfx {
namespace {

constexpr std::size_t kAspectCount       = 2;
constexpr std::size_t kStaticUploadCount = 2;
constexpr std::size_t kSequenceUploadCount = 4;
// Four layers can each own one pending decode for the active aspect. Eight
// fixed buffers leave another full set for the worker without retaining
// sixteen 8 MiB rasters after steady state.
constexpr std::size_t kDecodeResultCount = 8;

std::size_t aspectIndex(OverlayAspect aspect)
{
    return aspect == OverlayAspect::Portrait9x16 ? 1U : 0U;
}

OverlayAspect aspectFor(const EffectContext& context)
{
    return context.outputAspect < 1.0f ? OverlayAspect::Portrait9x16
                                       : OverlayAspect::Landscape16x9;
}

bool sameAssetContent(const OverlayAsset& left, const OverlayAsset& right)
{
    if (left.id != right.id || left.kind != right.kind ||
        left.fpsNumerator != right.fpsNumerator ||
        left.fpsDenominator != right.fpsDenominator)
        return false;
    for (std::size_t i = 0; i < left.variants.size(); ++i)
    {
        if (left.variants[i].has_value() != right.variants[i].has_value()) return false;
        if (!left.variants[i]) continue;
        const OverlayVariant& a = *left.variants[i];
        const OverlayVariant& b = *right.variants[i];
        if (a.aspect != b.aspect || a.width != b.width || a.height != b.height ||
            a.frames != b.frames)
            return false;
    }
    return true;
}

bool sameVariantContent(const OverlayAsset& left, const OverlayAsset& right, std::size_t index)
{
    if (left.variants[index].has_value() != right.variants[index].has_value()) return false;
    if (!left.variants[index]) return true;
    const OverlayVariant& a = *left.variants[index];
    const OverlayVariant& b = *right.variants[index];
    return a.aspect == b.aspect && a.width == b.width && a.height == b.height &&
           a.frames == b.frames;
}

float easedMix(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

template <typename T, std::size_t Capacity>
class SpscQueue
{
public:
    bool push(T value)
    {
        const std::size_t write = write_.load(std::memory_order_relaxed);
        const std::size_t next = (write + 1U) % Capacity;
        if (next == read_.load(std::memory_order_acquire)) return false;
        entries_[write] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value)
    {
        const std::size_t read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        value = entries_[read];
        read_.store((read + 1U) % Capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> entries_{};
    std::atomic<std::size_t> read_{0};
    std::atomic<std::size_t> write_{0};
};

struct DecodeControl
{
    OverlayLayerId layerId = 0;
    std::uint64_t  generation = 0;
    OverlayAsset   asset;
    std::atomic<bool> active{false};
    std::atomic<std::uint32_t> desiredFrame{0};
    std::atomic<int> desiredAspect{0};

    // Worker-only cache. These fields are never read by the render thread.
    bool workerHaveRequest = false;
    std::uint32_t workerFrame = 0;
    int workerAspect = 0;
};

struct DecodeResult
{
    OverlayLayerId layerId = 0;
    std::uint64_t  generation = 0;
    OverlayAspect  aspect = OverlayAspect::Landscape16x9;
    std::uint32_t  frame = 0;
    bool            ok = false;
    DecodedOverlayImage image;
    std::string     error;
    bool            published = false; // worker-only, released through retired queue
};

class OverlayDecodeWorker
{
public:
    void start()
    {
        if (running_.exchange(true)) return;
        worker_ = std::thread([this] { run(); });
    }

    void stop()
    {
        running_.store(false, std::memory_order_release);
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(controlsMutex_);
        controls_ = {};
    }

    void attach(const std::shared_ptr<DecodeControl>& control)
    {
        std::lock_guard<std::mutex> lock(controlsMutex_);
        for (auto& slot : controls_)
        {
            if (!slot)
            {
                slot = control;
                wake_.notify_one();
                return;
            }
        }
    }

    void detach(const std::shared_ptr<DecodeControl>& control)
    {
        if (!control) return;
        control->active.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lock(controlsMutex_);
        for (auto& slot : controls_)
        {
            if (slot == control) slot.reset();
        }
    }

    bool pop(DecodeResult*& result) { return ready_.pop(result); }

    void retire(DecodeResult* result)
    {
        // The reverse queue is larger than the ready queue's usable capacity,
        // so a consumer that retires each pop cannot fill it.
        // Never wait from the render thread. With one retirement per consumed
        // result this queue has enough capacity; a failed push is therefore a
        // defensive drop that merely delays reuse of one decoder slot.
        static_cast<void>(retired_.push(result));
    }

private:
    DecodeResult* freeResult()
    {
        for (DecodeResult& result : results_)
        {
            if (!result.published) return &result;
        }
        return nullptr;
    }

    void releaseRetired()
    {
        DecodeResult* result = nullptr;
        while (retired_.pop(result))
        {
            if (result) result->published = false;
        }
    }

    void run()
    {
        while (running_.load(std::memory_order_acquire))
        {
            releaseRetired();
            std::array<std::shared_ptr<DecodeControl>, kMaxOverlayLayers> controls;
            {
                std::lock_guard<std::mutex> lock(controlsMutex_);
                controls = controls_;
            }

            bool didWork = false;
            for (const auto& control : controls)
            {
                if (!control || !control->active.load(std::memory_order_acquire)) continue;
                const int aspectValue = control->desiredAspect.load(std::memory_order_acquire);
                const auto aspect = aspectValue == 1 ? OverlayAspect::Portrait9x16
                                                     : OverlayAspect::Landscape16x9;
                const std::uint32_t desired = control->desiredFrame.load(std::memory_order_acquire);
                if (control->workerHaveRequest && control->workerAspect == aspectValue &&
                    control->workerFrame == desired)
                    continue;

                const OverlayVariant* variant = control->asset.variant(aspect);
                if (!variant || variant->frames.empty())
                {
                    control->workerHaveRequest = true;
                    control->workerAspect = aspectValue;
                    control->workerFrame = desired;
                    continue;
                }

                DecodeResult* result = freeResult();
                if (!result) break;
                const std::size_t frame = std::min<std::size_t>(desired,
                                                                variant->frames.size() - 1U);
                result->layerId = control->layerId;
                result->generation = control->generation;
                result->aspect = aspect;
                result->frame = static_cast<std::uint32_t>(frame);
                result->error.clear();
                result->ok = decodeOverlayPng(variant->frames[frame], result->image,
                                              result->error);
                result->published = true;
                if (!ready_.push(result))
                {
                    result->published = false;
                    break;
                }
                // Mark the request complete only after the result is visible
                // to the render side. If every reusable buffer or the ready
                // queue was busy, leaving it uncached makes the worker retry;
                // otherwise a static/paused frame could be skipped forever.
                control->workerHaveRequest = true;
                control->workerAspect = aspectValue;
                control->workerFrame = desired;
                didWork = true;
            }

            if (!didWork)
            {
                std::unique_lock<std::mutex> lock(waitMutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(2));
            }
        }
        releaseRetired();
    }

    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex controlsMutex_;
    std::array<std::shared_ptr<DecodeControl>, kMaxOverlayLayers> controls_{};
    std::mutex waitMutex_;
    std::condition_variable wake_;
    std::array<DecodeResult, kDecodeResultCount> results_{};
    SpscQueue<DecodeResult*, kDecodeResultCount + 1U> ready_;
    SpscQueue<DecodeResult*, kDecodeResultCount + 1U> retired_;
};

struct UploadSlot
{
    GpuTexture* texture = nullptr;
};

} // namespace

struct OverlaySystem::Impl
{
    struct Layer
    {
        OverlayLayerConfig config;
        OverlayAsset asset;
        OverlayPlaybackState playback;
        std::shared_ptr<DecodeControl> decode;
        std::array<GpuTexture*, kAspectCount> currentTexture{};
        std::array<GpuTexture*, kAspectCount> previousTexture{};
        std::array<std::uint32_t, kAspectCount> displayedFrame{};
        std::array<DecodeResult*, kAspectCount> pending{};
        std::array<float, kAspectCount> replacementLinear{};
        std::array<bool, kAspectCount> awaitingReplacement{};
        std::array<bool, kAspectCount> replacementActive{};
        std::size_t resourceSlot = 0;
        std::uint64_t generation = 0;
        std::uint64_t underflows = 0;
        std::uint64_t skippedFrames = 0;
        std::string error;
        bool pendingRemoval = false;
    };

    const OverlayLibrary* library = nullptr;
    OverlayCompositor compositor;
    OverlayDecodeWorker decoder;
    std::array<Layer, kMaxOverlayLayers> layers{};
    std::size_t layerCount = 0;
    OverlayStackConfig config;
    OverlayUiSnapshot snapshot;
    OverlayLayerId nextLayerId = 1;
    std::uint64_t nextGeneration = 1;
    std::array<bool, kMaxOverlayLayers> resourceUsed{};

    std::array<std::array<std::array<UploadSlot, kStaticUploadCount>, kAspectCount>,
               kMaxOverlayLayers> staticUploads{};
    std::array<std::array<UploadSlot, kSequenceUploadCount>, kAspectCount> sequenceUploads{};

    Layer* findLayer(OverlayLayerId id)
    {
        for (std::size_t i = 0; i < layerCount; ++i)
            if (layers[i].config.layerId == id) return &layers[i];
        return nullptr;
    }

    const Layer* findLayer(OverlayLayerId id) const
    {
        for (std::size_t i = 0; i < layerCount; ++i)
            if (layers[i].config.layerId == id) return &layers[i];
        return nullptr;
    }

    bool animationBusy(OverlayLayerId except = 0) const
    {
        for (std::size_t i = 0; i < layerCount; ++i)
        {
            const Layer& layer = layers[i];
            if (layer.config.layerId == except || layer.asset.kind != OverlayKind::PngSequence)
                continue;
            if (layer.config.enabled || layer.playback.phase() == OverlayLayerPhase::FadingOut)
                return true;
        }
        return false;
    }

    std::size_t acquireResourceSlot()
    {
        for (std::size_t i = 0; i < resourceUsed.size(); ++i)
        {
            if (!resourceUsed[i])
            {
                resourceUsed[i] = true;
                return i;
            }
        }
        return 0;
    }

    void releasePending(Layer& layer)
    {
        for (DecodeResult*& result : layer.pending)
        {
            if (result)
            {
                decoder.retire(result);
                result = nullptr;
            }
        }
    }

    void detach(Layer& layer)
    {
        releasePending(layer);
        decoder.detach(layer.decode);
        layer.decode.reset();
        if (layer.resourceSlot < resourceUsed.size()) resourceUsed[layer.resourceSlot] = false;
    }

    std::uint32_t assetFrameCount(const OverlayAsset& asset) const
    {
        for (const auto& variant : asset.variants)
            if (variant) return static_cast<std::uint32_t>(variant->frames.size());
        return 0;
    }

    void attachDecode(Layer& layer)
    {
        layer.generation = nextGeneration++;
        layer.decode = std::make_shared<DecodeControl>();
        layer.decode->layerId = layer.config.layerId;
        layer.decode->generation = layer.generation;
        layer.decode->asset = layer.asset;
        decoder.attach(layer.decode);
    }

    bool configureLayer(Layer& layer, const OverlayLayerConfig& requested,
                        const OverlayAsset& asset, std::string& error)
    {
        if (requested.opacity < 0.0f || requested.opacity > 1.0f ||
            !std::isfinite(requested.opacity))
        {
            error = "Overlay opacity must be between 0 and 1";
            return false;
        }
        if (requested.framesPerSecond < 1.0f ||
            requested.framesPerSecond > kOverlaySequenceFps ||
            !std::isfinite(requested.framesPerSecond))
        {
            error = "Overlay FPS must be between 1 and 30";
            return false;
        }
        layer = {};
        layer.config = requested;
        if (layer.config.layerId == 0) layer.config.layerId = nextLayerId++;
        else nextLayerId = std::max(nextLayerId, layer.config.layerId + 1U);
        layer.asset = asset;
        layer.resourceSlot = acquireResourceSlot();
        const std::uint32_t frames = assetFrameCount(asset);
        layer.playback.configure(asset.kind, frames, layer.config.playback,
                                 layer.config.framesPerSecond);
        layer.playback.setEnabled(layer.config.enabled);
        attachDecode(layer);
        return true;
    }

    void syncConfigAndSnapshot()
    {
        config = {};
        snapshot = {};
        config.layerCount = layerCount;
        snapshot.layerCount = layerCount;
        snapshot.animationSlotAvailable = !animationBusy();
        for (std::size_t i = 0; i < layerCount; ++i)
        {
            config.layers[i] = layers[i].config;
            OverlayLayerStatus& status = snapshot.layers[i];
            status.config = layers[i].config;
            status.phase = layers[i].playback.phase();
            status.displayedFrame = layers[i].displayedFrame[0];
            status.frameCount = assetFrameCount(layers[i].asset);
            status.skippedFrames = layers[i].skippedFrames;
            status.underflows = layers[i].underflows;
            status.animated = layers[i].asset.kind == OverlayKind::PngSequence;
            status.paused = layers[i].playback.paused();
            status.replacing = layers[i].replacementActive[0] ||
                               layers[i].replacementActive[1];
            status.error = layers[i].error;
        }
    }

    void updateSnapshotRuntime(OverlayAspect aspect)
    {
        snapshot.animationSlotAvailable = !animationBusy();
        const std::size_t activeAspect = aspectIndex(aspect);
        for (std::size_t i = 0; i < layerCount; ++i)
        {
            OverlayLayerStatus& status = snapshot.layers[i];
            const Layer& layer = layers[i];
            status.config.enabled = layer.config.enabled;
            status.config.opacity = layer.config.opacity;
            status.config.playback = layer.config.playback;
            status.config.framesPerSecond = layer.config.framesPerSecond;
            status.phase = layer.playback.phase();
            status.displayedFrame = layer.displayedFrame[activeAspect];
            status.frameCount = assetFrameCount(layer.asset);
            status.skippedFrames = layer.skippedFrames;
            status.underflows = layer.underflows;
            status.paused = layer.playback.paused();
            status.replacing = layer.replacementActive[activeAspect];
            if (status.error != layer.error) status.error = layer.error;
        }
    }

    bool decodedRasterValid(const DecodeResult& result, std::string& error) const
    {
        const std::uint32_t expectedWidth =
            result.aspect == OverlayAspect::Portrait9x16 ? 1080U : 1920U;
        const std::uint32_t expectedHeight =
            result.aspect == OverlayAspect::Portrait9x16 ? 1920U : 1080U;
        const std::size_t minimumRow = static_cast<std::size_t>(expectedWidth) * 4U;
        if (result.image.width != expectedWidth || result.image.height != expectedHeight ||
            result.image.rowBytes < minimumRow ||
            result.image.rowBytes > result.image.bgra8.size() ||
            result.image.bgra8.size() / result.image.rowBytes < expectedHeight)
        {
            error = "Decoded overlay frame has an invalid raster or buffer";
            return false;
        }
        return true;
    }

    UploadSlot* uploadDecoded(EffectContext& context, Layer& layer, DecodeResult& result)
    {
        auto trySlots = [&](auto& slots) -> UploadSlot* {
            for (UploadSlot& slot : slots)
            {
                if (!slot.texture || !slot.texture->valid() ||
                    slot.texture->width() != result.image.width ||
                    slot.texture->height() != result.image.height)
                    continue;
                // Metal returns false while a fixed-ring slot is still being
                // sampled by an unfinished command buffer; D3D11's DISCARD
                // path normally succeeds immediately. Neither path waits.
                if (context.targets->upload(*slot.texture, result.image.bgra8.data(),
                                            result.image.rowBytes))
                    return &slot;
            }
            return nullptr;
        };
        const std::size_t index = aspectIndex(result.aspect);
        if (layer.asset.kind == OverlayKind::PngSequence)
            return trySlots(sequenceUploads[index]);
        return trySlots(staticUploads[layer.resourceSlot][index]);
    }

    void acceptDecoded(EffectContext& context, DecodeResult* result)
    {
        Layer* layer = findLayer(result->layerId);
        if (!layer || result->generation != layer->generation)
        {
            decoder.retire(result);
            return;
        }
        const std::size_t index = aspectIndex(result->aspect);
        if (layer->pending[index])
        {
            ++layer->skippedFrames;
            decoder.retire(layer->pending[index]);
        }
        layer->pending[index] = result;

        if (!result->ok)
        {
            layer->error = result->error.empty() ? "Overlay frame decode failed" : result->error;
            layer->playback.setReady(false);
            decoder.retire(result);
            layer->pending[index] = nullptr;
            return;
        }

        if (!decodedRasterValid(*result, layer->error))
        {
            layer->playback.setReady(false);
            decoder.retire(result);
            layer->pending[index] = nullptr;
            return;
        }

        UploadSlot* slot = uploadDecoded(context, *layer, *result);
        if (!slot) return;
        if (layer->awaitingReplacement[index] && layer->currentTexture[index] &&
            layer->currentTexture[index] != slot->texture)
        {
            layer->previousTexture[index] = layer->currentTexture[index];
            layer->replacementLinear[index] = 0.0f;
            layer->replacementActive[index] = true;
        }
        layer->awaitingReplacement[index] = false;
        layer->currentTexture[index] = slot->texture;
        layer->displayedFrame[index] = result->frame;
        layer->error.clear();
        if (aspectIndex(aspectFor(context)) == index) layer->playback.setReady(true);
        decoder.retire(result);
        layer->pending[index] = nullptr;
    }

    void retryPending(EffectContext& context)
    {
        for (std::size_t i = 0; i < layerCount; ++i)
        {
            Layer& layer = layers[i];
            for (std::size_t a = 0; a < kAspectCount; ++a)
            {
                DecodeResult* result = layer.pending[a];
                if (!result || !result->ok) continue;
                UploadSlot* slot = uploadDecoded(context, layer, *result);
                if (!slot)
                {
                    ++layer.underflows;
                    continue;
                }
                if (layer.awaitingReplacement[a] && layer.currentTexture[a] &&
                    layer.currentTexture[a] != slot->texture)
                {
                    layer.previousTexture[a] = layer.currentTexture[a];
                    layer.replacementLinear[a] = 0.0f;
                    layer.replacementActive[a] = true;
                }
                layer.awaitingReplacement[a] = false;
                layer.currentTexture[a] = slot->texture;
                layer.displayedFrame[a] = result->frame;
                layer.error.clear();
                if (aspectIndex(aspectFor(context)) == a) layer.playback.setReady(true);
                decoder.retire(result);
                layer.pending[a] = nullptr;
            }
        }
    }

    void eraseLayer(std::size_t index)
    {
        detach(layers[index]);
        for (std::size_t i = index; i + 1U < layerCount; ++i)
            layers[i] = std::move(layers[i + 1U]);
        if (layerCount > 0)
        {
            --layerCount;
            layers[layerCount] = {};
        }
        syncConfigAndSnapshot();
    }

    bool validateConfig(const OverlayStackConfig& requested,
                        std::array<OverlayAsset, kMaxOverlayLayers>* assets,
                        std::string& error) const
    {
        if (!library || requested.layerCount > kMaxOverlayLayers)
        {
            error = "Invalid overlay stack";
            return false;
        }
        std::set<std::string> ids;
        std::size_t enabledSequences = 0;
        for (std::size_t i = 0; i < requested.layerCount; ++i)
        {
            const OverlayLayerConfig& layer = requested.layers[i];
            OverlayAsset asset;
            if (layer.assetId.empty() || !ids.insert(layer.assetId).second ||
                !library->copyAsset(layer.assetId, asset))
            {
                error = "Overlay preset references a missing or duplicate asset";
                return false;
            }
            if (layer.enabled && asset.kind == OverlayKind::PngSequence)
                ++enabledSequences;
            if (!std::isfinite(layer.opacity) || layer.opacity < 0.0f || layer.opacity > 1.0f ||
                !std::isfinite(layer.framesPerSecond) || layer.framesPerSecond < 1.0f ||
                layer.framesPerSecond > kOverlaySequenceFps)
            {
                error = "Overlay preset has invalid opacity or FPS";
                return false;
            }
            if (assets) (*assets)[i] = std::move(asset);
        }
        if (enabledSequences > 1)
        {
            error = "Only one PNG sequence can be active";
            return false;
        }
        return true;
    }
};

OverlaySystem::OverlaySystem() : impl_(std::make_unique<Impl>()) {}
OverlaySystem::~OverlaySystem() { shutdown(); }

bool OverlaySystem::initialize(EffectContext& context, const OverlayLibrary& library,
                               std::string& error)
{
    shutdown();
    impl_->library = &library;
    if (!impl_->compositor.initialize(context, error)) return false;

    for (std::size_t slot = 0; slot < kMaxOverlayLayers; ++slot)
    {
        for (std::size_t aspect = 0; aspect < kAspectCount; ++aspect)
        {
            const std::uint32_t width = aspect == 0 ? 1920U : 1080U;
            const std::uint32_t height = aspect == 0 ? 1080U : 1920U;
            for (std::size_t copy = 0; copy < kStaticUploadCount; ++copy)
            {
                const std::string key = "overlay.static." + std::to_string(slot) + "." +
                                        std::to_string(aspect) + "." + std::to_string(copy);
                auto& upload = context.targets->uploadTarget(key, width, height);
                if (!upload.valid())
                {
                    error = "Failed to reserve overlay upload textures";
                    shutdown();
                    return false;
                }
                impl_->staticUploads[slot][aspect][copy].texture = &upload;
            }
        }
    }
    for (std::size_t aspect = 0; aspect < kAspectCount; ++aspect)
    {
        const std::uint32_t width = aspect == 0 ? 1920U : 1080U;
        const std::uint32_t height = aspect == 0 ? 1080U : 1920U;
        for (std::size_t copy = 0; copy < kSequenceUploadCount; ++copy)
        {
            const std::string key = "overlay.sequence." + std::to_string(aspect) + "." +
                                    std::to_string(copy);
            auto& upload = context.targets->uploadTarget(key, width, height);
            if (!upload.valid())
            {
                error = "Failed to reserve overlay sequence textures";
                shutdown();
                return false;
            }
            impl_->sequenceUploads[aspect][copy].texture = &upload;
        }
    }
    impl_->decoder.start();
    impl_->syncConfigAndSnapshot();
    return true;
}

void OverlaySystem::setLibrary(const OverlayLibrary& library)
{
    impl_->library = &library;
    for (std::size_t i = 0; i < impl_->layerCount; ++i)
    {
        Impl::Layer& layer = impl_->layers[i];
        OverlayAsset refreshed;
        if (!library.copyAsset(layer.config.assetId, refreshed))
        {
            layer.error = "Overlay asset is missing";
            layer.playback.setReady(false);
            continue;
        }
        if (sameAssetContent(layer.asset, refreshed)) continue;
        for (std::size_t aspect = 0; aspect < kAspectCount; ++aspect)
        {
            if (!sameVariantContent(layer.asset, refreshed, aspect) &&
                layer.currentTexture[aspect])
                layer.awaitingReplacement[aspect] = true;
        }
        impl_->decoder.detach(layer.decode);
        impl_->releasePending(layer);
        layer.asset = std::move(refreshed);
        // Keep the last-good texture live while the replacement/additional
        // variant is decoded and uploaded. Importing an unrelated asset does
        // not touch this layer at all.
        layer.error.clear();
        impl_->attachDecode(layer);
    }
    impl_->syncConfigAndSnapshot();
}

void OverlaySystem::shutdown()
{
    if (!impl_) return;
    for (std::size_t i = 0; i < impl_->layerCount; ++i)
    {
        impl_->releasePending(impl_->layers[i]);
        if (impl_->layers[i].decode)
            impl_->layers[i].decode->active.store(false, std::memory_order_release);
    }
    impl_->decoder.stop();
    impl_->compositor.shutdown();
    impl_->layers = {};
    impl_->layerCount = 0;
    impl_->config = {};
    impl_->snapshot = {};
    impl_->resourceUsed = {};
    impl_->library = nullptr;
}

OverlayLayerId OverlaySystem::addLayer(std::string_view assetId, std::string& error)
{
    if (!impl_->library)
    {
        error = "Overlay library is unavailable";
        return 0;
    }
    if (impl_->layerCount >= kMaxOverlayLayers)
    {
        error = "The overlay stack already has four layers";
        return 0;
    }
    for (std::size_t i = 0; i < impl_->layerCount; ++i)
    {
        if (impl_->layers[i].config.assetId == assetId)
        {
            error = "This overlay is already in the stack";
            return 0;
        }
    }
    OverlayAsset asset;
    if (!impl_->library->copyAsset(assetId, asset))
    {
        error = "Overlay asset not found";
        return 0;
    }
    OverlayLayerConfig config;
    config.layerId = impl_->nextLayerId++;
    config.assetId = std::string(assetId);
    // Explicit Add layer is an on-air gesture. The graphic buffers, then
    // fades in; import by itself never calls this method.
    config.enabled = true;
    if (asset.kind == OverlayKind::PngSequence && impl_->animationBusy())
    {
        config.enabled = false;
        error = "Added disabled: another PNG sequence is active";
    }
    Impl::Layer layer;
    if (!impl_->configureLayer(layer, config, asset, error)) return 0;
    impl_->layers[impl_->layerCount++] = std::move(layer);
    impl_->syncConfigAndSnapshot();
    return config.layerId;
}

bool OverlaySystem::removeLayer(OverlayLayerId layerId)
{
    Impl::Layer* layer = impl_->findLayer(layerId);
    if (!layer) return false;
    layer->pendingRemoval = true;
    layer->config.enabled = false;
    layer->playback.setEnabled(false);
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::moveLayer(OverlayLayerId layerId, int delta)
{
    std::size_t index = impl_->layerCount;
    for (std::size_t i = 0; i < impl_->layerCount; ++i)
        if (impl_->layers[i].config.layerId == layerId) { index = i; break; }
    if (index >= impl_->layerCount || delta == 0) return false;

    // The UI presents front-to-back and sends -1 toward front. Internal and
    // preset order is back-to-front, so front is the increasing index.
    const int targetValue = static_cast<int>(index) - delta;
    if (targetValue < 0 || targetValue >= static_cast<int>(impl_->layerCount)) return false;
    const std::size_t target = static_cast<std::size_t>(targetValue);
    std::swap(impl_->layers[index], impl_->layers[target]);
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::setLayerAsset(OverlayLayerId layerId, std::string_view assetId,
                                  std::string& error)
{
    Impl::Layer* layer = impl_->findLayer(layerId);
    if (!layer || !impl_->library) return false;
    OverlayAsset asset;
    if (!impl_->library->copyAsset(assetId, asset))
    {
        error = "Overlay asset not found";
        return false;
    }
    if (asset.kind == OverlayKind::PngSequence && layer->config.enabled &&
        impl_->animationBusy(layerId))
    {
        error = "Another PNG sequence is active";
        return false;
    }
    const std::size_t resourceSlot = layer->resourceSlot;
    impl_->decoder.detach(layer->decode);
    impl_->releasePending(*layer);
    impl_->resourceUsed[resourceSlot] = false;
    OverlayLayerConfig next = layer->config;
    next.assetId = std::string(assetId);
    if (!impl_->configureLayer(*layer, next, asset, error))
    {
        impl_->resourceUsed[resourceSlot] = true;
        return false;
    }
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::setLayerEnabled(OverlayLayerId layerId, bool enabled, std::string& error)
{
    Impl::Layer* layer = impl_->findLayer(layerId);
    if (!layer) return false;
    if (enabled && layer->asset.kind == OverlayKind::PngSequence &&
        impl_->animationBusy(layerId))
    {
        error = "Another PNG sequence is active or fading out";
        return false;
    }
    layer->pendingRemoval = false;
    layer->config.enabled = enabled;
    if (enabled && layer->asset.kind == OverlayKind::PngSequence &&
        layer->config.playback == OverlayPlayback::OneShot)
        layer->playback.restart();
    else
        layer->playback.setEnabled(enabled);
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::setLayerOpacity(OverlayLayerId layerId, float opacity)
{
    Impl::Layer* layer = impl_->findLayer(layerId);
    if (!layer || !std::isfinite(opacity)) return false;
    layer->config.opacity = std::clamp(opacity, 0.0f, 1.0f);
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::setLayerPlayback(OverlayLayerId layerId, OverlayPlayback playback)
{
    Impl::Layer* layer = impl_->findLayer(layerId);
    if (!layer) return false;
    layer->config.playback = playback;
    layer->playback.setPlayback(playback);
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::setLayerFramesPerSecond(OverlayLayerId layerId, float fps)
{
    Impl::Layer* layer = impl_->findLayer(layerId);
    if (!layer || !std::isfinite(fps)) return false;
    layer->config.framesPerSecond = std::clamp(fps, 1.0f, kOverlaySequenceFps);
    layer->playback.setFramesPerSecond(layer->config.framesPerSecond);
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::setLayerPaused(OverlayLayerId layerId, bool paused)
{
    Impl::Layer* layer = impl_->findLayer(layerId);
    if (!layer) return false;
    layer->playback.setPaused(paused);
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::triggerLayer(OverlayLayerId layerId, std::string& error)
{
    Impl::Layer* layer = impl_->findLayer(layerId);
    if (!layer) return false;
    if (layer->asset.kind == OverlayKind::PngSequence && impl_->animationBusy(layerId))
    {
        error = "Another PNG sequence is active or fading out";
        return false;
    }
    layer->config.enabled = true;
    layer->pendingRemoval = false;
    layer->playback.restart();
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::applyConfig(const OverlayStackConfig& requested, std::string& error)
{
    std::array<OverlayAsset, kMaxOverlayLayers> assets;
    if (!impl_->validateConfig(requested, &assets, error)) return false;

    for (std::size_t i = 0; i < impl_->layerCount; ++i) impl_->detach(impl_->layers[i]);
    impl_->layers = {};
    impl_->layerCount = 0;
    impl_->resourceUsed = {};
    for (std::size_t i = 0; i < requested.layerCount; ++i)
    {
        OverlayLayerConfig config = requested.layers[i];
        config.layerId = 0; // runtime identity is deliberately not persisted
        Impl::Layer layer;
        if (!impl_->configureLayer(layer, config, assets[i], error)) return false;
        impl_->layers[impl_->layerCount++] = std::move(layer);
    }
    impl_->syncConfigAndSnapshot();
    return true;
}

bool OverlaySystem::validateConfig(const OverlayStackConfig& requested,
                                   std::string& error) const
{
    return impl_->validateConfig(requested, nullptr, error);
}

const OverlayStackConfig& OverlaySystem::config() const { return impl_->config; }
const OverlayUiSnapshot& OverlaySystem::snapshot() const { return impl_->snapshot; }

void OverlaySystem::commitControlPlane()
{
    for (std::size_t i = impl_->layerCount; i-- > 0;)
    {
        Impl::Layer& layer = impl_->layers[i];
        if (layer.pendingRemoval && layer.playback.phase() == OverlayLayerPhase::Disabled)
            impl_->eraseLayer(i);
    }
}

void OverlaySystem::service(EffectContext& context)
{
    impl_->retryPending(context);
    DecodeResult* result = nullptr;
    std::size_t drained = 0;
    while (drained < kDecodeResultCount && impl_->decoder.pop(result))
    {
        impl_->acceptDecoded(context, result);
        ++drained;
    }

    const OverlayAspect aspect = aspectFor(context);
    const std::size_t aspectSlot = aspectIndex(aspect);
    for (std::size_t i = 0; i < impl_->layerCount; ++i)
    {
        Impl::Layer& layer = impl_->layers[i];
        const OverlayVariant* variant = layer.asset.variant(aspect);
        const bool haveTexture = layer.currentTexture[aspectSlot] != nullptr;
        layer.playback.setReady(variant && haveTexture && layer.error.empty());
        const bool autoDisabled = layer.playback.advance(context.deltaTime);
        if (autoDisabled) layer.config.enabled = false;

        if (layer.replacementActive[aspectSlot])
        {
            const float step = std::max(context.deltaTime, 0.0f) / kOverlayFadeSeconds;
            layer.replacementLinear[aspectSlot] =
                std::min(1.0f, layer.replacementLinear[aspectSlot] + step);
            if (layer.replacementLinear[aspectSlot] >= 1.0f)
            {
                layer.replacementActive[aspectSlot] = false;
                layer.previousTexture[aspectSlot] = nullptr;
            }
        }

        if (layer.decode)
        {
            layer.decode->desiredAspect.store(static_cast<int>(aspectSlot),
                                               std::memory_order_release);
            layer.decode->desiredFrame.store(layer.playback.desiredFrame(),
                                              std::memory_order_release);
            const bool wantsFrame = variant &&
                (layer.config.enabled || layer.playback.phase() == OverlayLayerPhase::FadingOut ||
                 layer.playback.phase() == OverlayLayerPhase::Buffering);
            layer.decode->active.store(wantsFrame, std::memory_order_release);
        }
    }

    impl_->config.layerCount = impl_->layerCount;
    for (std::size_t i = 0; i < impl_->layerCount; ++i)
    {
        // The ids only change on control operations, where the full snapshot
        // is synchronised. Keep steady-state service free of string copies.
        impl_->config.layers[i].enabled = impl_->layers[i].config.enabled;
        impl_->config.layers[i].opacity = impl_->layers[i].config.opacity;
        impl_->config.layers[i].playback = impl_->layers[i].config.playback;
        impl_->config.layers[i].framesPerSecond =
            impl_->layers[i].config.framesPerSecond;
    }
    impl_->updateSnapshotRuntime(aspect);
}

GpuTexture& OverlaySystem::composite(EffectContext& context, GpuTexture& input,
                                     float effectMix, bool bypass)
{
    std::array<OverlayCompositeLayer, kMaxOverlayLayers * 2U> renderLayers{};
    std::size_t count = 0;
    const OverlayAspect aspect = aspectFor(context);
    const std::size_t index = aspectIndex(aspect);
    for (std::size_t i = 0; i < impl_->layerCount; ++i)
    {
        Impl::Layer& layer = impl_->layers[i];
        GpuTexture* texture = layer.currentTexture[index];
        const float opacity = layer.config.opacity * layer.playback.fadeAmount();
        if (!texture || opacity <= 0.0f) continue;
        if (layer.replacementActive[index] && layer.previousTexture[index])
        {
            const float mix = easedMix(layer.replacementLinear[index]);
            renderLayers[count++] = {layer.previousTexture[index], aspect, opacity * (1.0f - mix)};
            renderLayers[count++] = {texture, aspect, opacity * mix};
        }
        else
        {
            renderLayers[count++] = {texture, aspect, opacity};
        }
    }
    return impl_->compositor.composite(context, input,
                                       std::span<const OverlayCompositeLayer>(renderLayers.data(), count),
                                       effectMix, bypass);
}

} // namespace atemfx
