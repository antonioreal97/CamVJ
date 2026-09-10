#pragma once

// Objective-C++ only. Included by the Metal backend and by the Metal UI layer;
// never by portable code, which sees this backend solely through gpu/Rhi.h.

#if !defined(__OBJC__)
#error "MetalDevice.h requires Objective-C++"
#endif

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gpu/Rhi.h"

@class NSView;

namespace atemfx {

class MetalTexture final : public GpuTexture
{
public:
    bool create(id<MTLDevice> device, uint32_t width, uint32_t height, MTLPixelFormat format);

    // CPU-writable and sample-only: BGRA8 in shared storage, for capture
    // frames arriving in system memory.
    bool createUploadable(id<MTLDevice> device, uint32_t width, uint32_t height);

    void release();

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }
    bool     valid() const override { return texture_ != nil; }
    void*    nativeTexture() const override { return (__bridge void*)texture_; }

    id<MTLTexture> metal() const { return texture_; }

private:
    id<MTLTexture> texture_ = nil;
    uint32_t       width_   = 0;
    uint32_t       height_  = 0;
};

// A compiled effect: one Metal library holding the shared fullscreen vertex
// function and the effect's fragment function, plus the pipeline state for the
// processing pixel format.
struct MetalShader
{
    std::string                name;
    id<MTLRenderPipelineState> pipeline = nil;
};

class MetalShaderLibrary final : public ShaderLibrary
{
public:
    bool initialize(id<MTLDevice> device, MTLPixelFormat processingFormat, std::string& error);
    void shutdown();

    ShaderHandle shader(const std::string& name, std::string* error = nullptr) override;
    bool         reloadAll(std::string& error) override;
    std::string  directory() const override { return directory_.string(); }

private:
    // __strong: a reference parameter to an Objective-C pointer defaults to
    // __autoreleasing under ARC, which does not bind to a strong member.
    bool build(const std::string& name, __strong id<MTLRenderPipelineState>& pipeline, std::string& error);

    id<MTLDevice>  device_ = nil;
    MTLPixelFormat format_ = MTLPixelFormatRGBA16Float;

    std::filesystem::path directory_;
    std::string           commonSource_;

    // unique_ptr so a rehash never invalidates a handle already handed out.
    std::unordered_map<std::string, std::unique_ptr<MetalShader>> shaders_;
};

class MetalTargetPool final : public TargetPool
{
public:
    bool create(id<MTLDevice> device, uint32_t width, uint32_t height, MTLPixelFormat format);

    // CPU-writable and sample-only: BGRA8 in shared storage, for capture
    // frames arriving in system memory.
    bool createUploadable(id<MTLDevice> device, uint32_t width, uint32_t height);

    void release();

    GpuTexture& scratch(std::size_t index) override;
    GpuTexture& persistent(const std::string& key) override;
    GpuTexture& uploadTarget(const std::string& key, uint32_t width, uint32_t height) override;
    bool        upload(GpuTexture& texture, const void* bgra8, std::size_t rowBytes) override;

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }

private:
    static constexpr std::size_t kScratchCount = 2;

    id<MTLDevice>  device_ = nil;
    MTLPixelFormat format_ = MTLPixelFormatRGBA16Float;
    uint32_t       width_  = 0;
    uint32_t       height_ = 0;

    MetalTexture                                                   scratch_[kScratchCount];
    std::unordered_map<std::string, std::unique_ptr<MetalTexture>> persistent_;
    std::unordered_map<std::string, std::unique_ptr<MetalTexture>> uploads_;
};

class MetalDevice;

class MetalFullscreenPass final : public FullscreenPass
{
public:
    bool initialize(MetalDevice& device, std::string& error);
    void shutdown();

    void draw(GpuTexture&            target,
              ShaderHandle           shader,
              const GpuTexture*      source,
              const EffectConstants& constants,
              SamplerFilter          filter,
              const GpuTexture*      history) override;

private:
    MetalDevice*        owner_         = nullptr;
    id<MTLSamplerState> pointSampler_  = nil;
    id<MTLSamplerState> linearSampler_ = nil;
};

// The program feed on a second display: its own CAMetalLayer, its own
// pipeline — the drawable is BGRA8 while everything the engine processes is
// RGBA16Float, so the shader library, which is compiled for the processing
// format, cannot draw here.
class MetalOutputSurface final : public OutputSurface
{
public:
    ~MetalOutputSurface() override { shutdown(); }

    bool initialize(MetalDevice& device, void* nativeView, uint32_t width, uint32_t height);
    void shutdown();

    void present(GpuTexture& frame) override;
    void resize(uint32_t width, uint32_t height) override;

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }

private:
    MetalDevice*               owner_    = nullptr;
    CAMetalLayer*              layer_    = nil;
    id<MTLRenderPipelineState> pipeline_ = nil;
    id<MTLSamplerState>        sampler_  = nil;

    uint32_t width_  = 0;
    uint32_t height_ = 0;
};

class MetalDevice final : public GraphicsDevice
{
public:
    bool initialize(Window* window, uint32_t processingWidth, uint32_t processingHeight) override;
    void shutdown() override;

    bool beginFrame() override;
    void beginProcessing() override;
    void endProcessing() override;
    void beginUi() override;
    void endFrame(bool vsync) override;

    void onWindowResized(uint32_t width, uint32_t height) override;

    std::unique_ptr<OutputSurface> createOutputSurface(void*    nativeHandle,
                                                       uint32_t width,
                                                       uint32_t height) override;

    ShaderLibrary&  shaders() override { return shaders_; }
    FullscreenPass& fullscreenPass() override { return fullscreenPass_; }
    TargetPool&     targets() override { return targets_; }

    float lastGpuMilliseconds() const override;
    bool  hasGpuTiming() const override;

    const std::string& adapterName() const override { return adapterName_; }

    bool readback(GpuTexture& texture, std::vector<uint8_t>& rgba) override;

    // Used by MetalFullscreenPass and the Metal UI layer.
    id<MTLDevice>            metal() const { return device_; }
    id<MTLCommandQueue>      commandQueue() const { return queue_; }
    id<MTLCommandBuffer>     processingCommandBuffer() const { return processingCommands_; }
    id<MTLCommandBuffer>     uiCommandBuffer() const { return uiCommands_; }
    MTLRenderPassDescriptor* uiRenderPassDescriptor() const { return uiPassDescriptor_; }

private:
    // Written from a command-buffer completion handler on a background thread.
    struct TimingState
    {
        std::atomic<float> milliseconds{0.0f};
        std::atomic<bool>  hasResult{false};
    };

    id<MTLDevice>       device_ = nil;
    id<MTLCommandQueue> queue_  = nil;

    CAMetalLayer*            layer_            = nil;   // nil when headless
    __weak NSView*           previewView_      = nil;
    id<CAMetalDrawable>      drawable_         = nil;
    id<MTLCommandBuffer>     processingCommands_ = nil;
    id<MTLCommandBuffer>     uiCommands_       = nil;
    MTLRenderPassDescriptor* uiPassDescriptor_ = nil;

    MetalShaderLibrary   shaders_;
    MetalTargetPool      targets_;
    MetalFullscreenPass  fullscreenPass_;

    std::shared_ptr<TimingState> timing_ = std::make_shared<TimingState>();

    std::string adapterName_ = "unknown";
    bool        headless_    = false;
    bool        vsync_       = true;
};

} // namespace atemfx
