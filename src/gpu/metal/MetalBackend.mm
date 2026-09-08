#include "gpu/metal/MetalDevice.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>

#include "core/Log.h"
#include "gpu/Backend.h"
#include "gpu/HalfFloat.h"
#include "gpu/ShaderPaths.h"
#include "platform/Window.h"

// The five Metal classes share a translation unit on purpose. Each is small
// and they are meaningless apart; splitting them would multiply the
// Objective-C++ header surface without making any of them easier to read.

namespace atemfx {

namespace {

constexpr const char* kBackendSubdirectory = "metal";
constexpr const char* kCommonSourceFile    = "common.metal";
constexpr const char* kVertexFunctionName  = "fullscreen_vertex";
constexpr const char* kFragmentFunctionName = "fragment_main";

std::string describe(NSError* error)
{
    if (!error)
    {
        return "unknown error";
    }
    return std::string([[error localizedDescription] UTF8String] ?: "unknown error");
}

} // namespace

// ---------------------------------------------------------------------------
// MetalTexture
// ---------------------------------------------------------------------------

bool MetalTexture::create(id<MTLDevice> device, uint32_t width, uint32_t height, MTLPixelFormat format)
{
    release();

    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    descriptor.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModePrivate;

    texture_ = [device newTextureWithDescriptor:descriptor];
    if (!texture_)
    {
        ATEMFX_LOG_ERROR("Failed to allocate a %ux%u Metal texture", width, height);
        return false;
    }

    width_  = width;
    height_ = height;
    return true;
}

bool MetalTexture::createUploadable(id<MTLDevice> device, uint32_t width, uint32_t height)
{
    release();

    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    descriptor.usage       = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;

    texture_ = [device newTextureWithDescriptor:descriptor];
    if (!texture_)
    {
        ATEMFX_LOG_ERROR("Failed to allocate a %ux%u upload texture", width, height);
        return false;
    }

    width_  = width;
    height_ = height;
    return true;
}

void MetalTexture::release()
{
    texture_ = nil;
    width_   = 0;
    height_  = 0;
}

// ---------------------------------------------------------------------------
// MetalShaderLibrary
// ---------------------------------------------------------------------------

bool MetalShaderLibrary::initialize(id<MTLDevice> device, MTLPixelFormat processingFormat, std::string& error)
{
    device_ = device;
    format_ = processingFormat;

    directory_ = resolveShaderDirectory(kBackendSubdirectory, kCommonSourceFile);
    if (directory_.empty())
    {
        error = "Could not locate the Metal shader directory. Set ATEMFX_SHADER_DIR.";
        return false;
    }

    if (!readTextFile(directory_ / kCommonSourceFile, commonSource_, error))
    {
        return false;
    }

    ATEMFX_LOG_INFO("Metal shader directory: %s", directory_.string().c_str());
    return true;
}

void MetalShaderLibrary::shutdown()
{
    shaders_.clear();
    device_ = nil;
}

bool MetalShaderLibrary::build(const std::string&                   name,
                               __strong id<MTLRenderPipelineState>& pipeline,
                               std::string&                         error)
{
    std::string fragmentSource;
    if (!readTextFile(directory_ / (name + ".metal"), fragmentSource, error))
    {
        return false;
    }

    // MSL compiled from source has no include search path, so the shared
    // declarations are prepended rather than #included.
    const std::string combined = commonSource_ + "\n" + fragmentSource;

    NSError*       compileError = nil;
    id<MTLLibrary> library      = [device_ newLibraryWithSource:@(combined.c_str())
                                                        options:nil
                                                          error:&compileError];
    if (!library)
    {
        error = name + ".metal: " + describe(compileError);
        return false;
    }

    id<MTLFunction> vertexFunction   = [library newFunctionWithName:@(kVertexFunctionName)];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@(kFragmentFunctionName)];
    if (!vertexFunction || !fragmentFunction)
    {
        error = name + ".metal: missing " + kVertexFunctionName + " or " + kFragmentFunctionName;
        return false;
    }

    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction               = vertexFunction;
    descriptor.fragmentFunction             = fragmentFunction;
    descriptor.colorAttachments[0].pixelFormat = format_;

    NSError* pipelineError = nil;
    pipeline = [device_ newRenderPipelineStateWithDescriptor:descriptor error:&pipelineError];
    if (!pipeline)
    {
        error = name + ".metal: " + describe(pipelineError);
        return false;
    }

    return true;
}

ShaderHandle MetalShaderLibrary::shader(const std::string& name, std::string* error)
{
    if (const auto it = shaders_.find(name); it != shaders_.end())
    {
        // A cached failure is a null pipeline. Returning early here is what
        // keeps a broken shader from hitting the disk every frame.
        return it->second->pipeline ? it->second.get() : nullptr;
    }

    auto entry  = std::make_unique<MetalShader>();
    entry->name = name;

    std::string localError;
    if (!build(name, entry->pipeline, localError))
    {
        ATEMFX_LOG_ERROR("%s", localError.c_str());
        if (error)
        {
            *error = localError;
        }
        shaders_.emplace(name, std::move(entry));
        return nullptr;
    }

    ATEMFX_LOG_INFO("Compiled %s.metal", name.c_str());
    return shaders_.emplace(name, std::move(entry)).first->second.get();
}

bool MetalShaderLibrary::reloadAll(std::string& error)
{
    std::string firstError;
    bool        allSucceeded = true;

    std::string reloadedCommon;
    std::string commonError;
    if (readTextFile(directory_ / kCommonSourceFile, reloadedCommon, commonError))
    {
        commonSource_ = reloadedCommon;
    }
    else
    {
        allSucceeded = false;
        firstError   = commonError;
    }

    for (auto& [name, entry] : shaders_)
    {
        __strong id<MTLRenderPipelineState> pipeline = nil;
        std::string                localError;
        if (!build(name, pipeline, localError))
        {
            allSucceeded = false;
            if (firstError.empty())
            {
                firstError = localError;
            }
            ATEMFX_LOG_ERROR("%s", localError.c_str());
            continue;  // keep the previous, working pipeline
        }
        entry->pipeline = pipeline;
    }

    error = firstError;
    if (allSucceeded)
    {
        ATEMFX_LOG_INFO("Reloaded %zu shaders", shaders_.size());
    }
    return allSucceeded;
}

// ---------------------------------------------------------------------------
// MetalTargetPool
// ---------------------------------------------------------------------------

bool MetalTargetPool::create(id<MTLDevice> device, uint32_t width, uint32_t height, MTLPixelFormat format)
{
    release();

    device_ = device;
    width_  = width;
    height_ = height;
    format_ = format;

    for (MetalTexture& target : scratch_)
    {
        if (!target.create(device, width, height, format))
        {
            release();
            return false;
        }
    }

    return true;
}

void MetalTargetPool::release()
{
    persistent_.clear();
    for (MetalTexture& target : scratch_)
    {
        target.release();
    }
    uploads_.clear();
    device_ = nil;
    width_  = 0;
    height_ = 0;
}

GpuTexture& MetalTargetPool::scratch(std::size_t index)
{
    return scratch_[index % kScratchCount];
}

GpuTexture& MetalTargetPool::persistent(const std::string& key)
{
    if (const auto it = persistent_.find(key); it != persistent_.end())
    {
        return *it->second;
    }

    auto target = std::make_unique<MetalTexture>();
    if (device_ && target->create(device_, width_, height_, format_))
    {
        ATEMFX_LOG_INFO("Allocated persistent target '%s' (%ux%u)", key.c_str(), width_, height_);
    }
    else
    {
        ATEMFX_LOG_ERROR("Failed to allocate persistent target '%s'", key.c_str());
    }

    return *persistent_.emplace(key, std::move(target)).first->second;
}

GpuTexture& MetalTargetPool::uploadTarget(const std::string& key, uint32_t width, uint32_t height)
{
    auto it = uploads_.find(key);
    if (it != uploads_.end() && it->second->width() == width && it->second->height() == height)
    {
        return *it->second;
    }

    // A camera can be swapped for one of another size, so the texture follows
    // the frame rather than the project.
    auto target = std::make_unique<MetalTexture>();
    if (device_ && width > 0 && height > 0 && target->createUploadable(device_, width, height))
    {
        ATEMFX_LOG_INFO("Allocated upload target '%s' (%ux%u)", key.c_str(), width, height);
    }

    if (it != uploads_.end())
    {
        it->second = std::move(target);
        return *it->second;
    }

    return *uploads_.emplace(key, std::move(target)).first->second;
}

bool MetalTargetPool::upload(GpuTexture& texture, const void* bgra8, std::size_t rowBytes)
{
    MetalTexture& destination = static_cast<MetalTexture&>(texture);
    if (!destination.valid() || !bgra8)
    {
        return false;
    }

    const MTLRegion region = MTLRegionMake2D(0, 0, destination.width(), destination.height());
    [destination.metal() replaceRegion:region
                           mipmapLevel:0
                             withBytes:bgra8
                           bytesPerRow:rowBytes];
    return true;
}

// ---------------------------------------------------------------------------
// MetalFullscreenPass
// ---------------------------------------------------------------------------

bool MetalFullscreenPass::initialize(MetalDevice& device, std::string& error)
{
    owner_ = &device;

    // Clamp addressing: effects displace UVs past the edge (RGB Split, Mirror)
    // and wrapping there produces obvious, wrong-looking artefacts.
    MTLSamplerDescriptor* descriptor = [[MTLSamplerDescriptor alloc] init];
    descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    descriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;

    descriptor.minFilter = MTLSamplerMinMagFilterNearest;
    descriptor.magFilter = MTLSamplerMinMagFilterNearest;
    pointSampler_        = [device.metal() newSamplerStateWithDescriptor:descriptor];

    descriptor.minFilter = MTLSamplerMinMagFilterLinear;
    descriptor.magFilter = MTLSamplerMinMagFilterLinear;
    linearSampler_       = [device.metal() newSamplerStateWithDescriptor:descriptor];

    if (!pointSampler_ || !linearSampler_)
    {
        error = "Failed to create the Metal samplers";
        return false;
    }

    return true;
}

void MetalFullscreenPass::shutdown()
{
    pointSampler_  = nil;
    linearSampler_ = nil;
    owner_         = nullptr;
}

void MetalFullscreenPass::draw(GpuTexture&            target,
                               ShaderHandle           shader,
                               const GpuTexture*      source,
                               const EffectConstants& constants,
                               SamplerFilter          filter,
                               const GpuTexture*      history)
{
    const MetalShader* program = static_cast<const MetalShader*>(shader);
    if (!owner_ || !program || !program->pipeline || !target.valid())
    {
        return;
    }

    id<MTLCommandBuffer> commands = owner_->processingCommandBuffer();
    if (!commands)
    {
        return;
    }

    MetalTexture& destination = static_cast<MetalTexture&>(target);

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    // Every pass writes every pixel, so there is nothing worth loading.
    pass.colorAttachments[0].texture     = destination.metal();
    pass.colorAttachments[0].loadAction  = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:program->pipeline];

    [encoder setVertexBytes:&constants length:sizeof(constants) atIndex:0];
    [encoder setFragmentBytes:&constants length:sizeof(constants) atIndex:0];

    if (source)
    {
        const MetalTexture& input = static_cast<const MetalTexture&>(*source);
        [encoder setFragmentTexture:input.metal() atIndex:0];
    }

    if (history)
    {
        const MetalTexture& previous = static_cast<const MetalTexture&>(*history);
        [encoder setFragmentTexture:previous.metal() atIndex:1];
    }

    [encoder setFragmentSamplerState:(filter == SamplerFilter::Point ? pointSampler_ : linearSampler_)
                             atIndex:0];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
}

// ---------------------------------------------------------------------------
// MetalOutputSurface
// ---------------------------------------------------------------------------

namespace {

// Self-contained: this pass is not an effect, it does not see EffectConstants,
// and it must not pick up whatever shaders/metal/common.metal happens to
// declare. Its only job is to put a finished frame on a display without
// distorting it.
constexpr const char* kOutputShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
};

vertex VSOutput output_vertex(uint vertexId [[vertex_id]])
{
    const float2 uv = float2(float((vertexId << 1) & 2u), float(vertexId & 2u));

    VSOutput output;
    output.uv       = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

// scale.xy applies to centred coordinates: greater than one shrinks the frame
// inside the display, which is what produces the letterbox bars.
fragment float4 output_fragment(VSOutput in [[stage_in]],
                                constant float4& scale [[buffer(0)]],
                                texture2d<float> source [[texture(0)]],
                                sampler samp [[sampler(0)]])
{
    const float2 uv = (in.uv - 0.5) * scale.xy + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    return float4(source.sample(samp, uv).rgb, 1.0);
}
)MSL";

} // namespace

bool MetalOutputSurface::initialize(MetalDevice& device,
                                    void*        nativeView,
                                    uint32_t     width,
                                    uint32_t     height)
{
    owner_ = &device;

    NSView* view = (__bridge NSView*)nativeView;
    if (!view)
    {
        ATEMFX_LOG_ERROR("Output window has no native view");
        return false;
    }

    id<MTLDevice> metalDevice = device.metal();
    if (!metalDevice)
    {
        return false;
    }

    NSError*       error   = nil;
    id<MTLLibrary> library = [metalDevice newLibraryWithSource:@(kOutputShaderSource)
                                                       options:nil
                                                         error:&error];
    if (!library)
    {
        ATEMFX_LOG_ERROR("Output shader: %s",
                         [[error localizedDescription] UTF8String] ?: "unknown error");
        return false;
    }

    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction               = [library newFunctionWithName:@"output_vertex"];
    descriptor.fragmentFunction             = [library newFunctionWithName:@"output_fragment"];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    pipeline_ = [metalDevice newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (!pipeline_)
    {
        ATEMFX_LOG_ERROR("Output pipeline: %s",
                         [[error localizedDescription] UTF8String] ?: "unknown error");
        return false;
    }

    MTLSamplerDescriptor* sampler = [[MTLSamplerDescriptor alloc] init];
    sampler.minFilter             = MTLSamplerMinMagFilterLinear;
    sampler.magFilter             = MTLSamplerMinMagFilterLinear;
    sampler.sAddressMode          = MTLSamplerAddressModeClampToEdge;
    sampler.tAddressMode          = MTLSamplerAddressModeClampToEdge;
    sampler_                      = [metalDevice newSamplerStateWithDescriptor:sampler];

    layer_                 = [CAMetalLayer layer];
    layer_.device          = metalDevice;
    layer_.pixelFormat     = MTLPixelFormatBGRA8Unorm;
    layer_.framebufferOnly = YES;

    // This layer is the pacer. The preview window is told to stop waiting for
    // its own display while an output is live, so the cadence the audience
    // sees comes from here and from nowhere else.
    layer_.displaySyncEnabled = YES;

    // Three, which is also the default. Two was tried first, on the argument
    // that each drawable in flight is another frame of latency between the
    // camera and the wall.
    //
    // Measured on an M4 driving a 1920x1080 144 Hz panel, test pattern and one
    // effect, four runs of 600 frames each:
    //
    //   two drawables    106.3 / 107.8 / 108.5 / 110.9 fps
    //   three drawables  114.8 / 143.4 / 115.3 / 115.2 fps
    //
    // Three is faster and is the only one that ever locks to the panel's full
    // rate. A wall shows jitter; it does not show one frame of latency, so the
    // frame is spent. Re-measure before changing it, and take more than one
    // run of each: single runs put the two in the wrong order, which is how
    // the first attempt at this landed on two.
    layer_.maximumDrawableCount = 3;

    const CGFloat scale  = view.window ? view.window.backingScaleFactor : 1.0;
    layer_.contentsScale = scale;
    layer_.frame         = view.bounds;

    // Assign before wantsLayer, as in MetalDevice::initialize: the other order
    // gives you AppKit's layer and a black display.
    view.layer      = layer_;
    view.wantsLayer = YES;

    resize(width, height);

    ATEMFX_LOG_INFO("Output surface ready: %ux%u pixels", width_, height_);
    return true;
}

void MetalOutputSurface::shutdown()
{
    layer_    = nil;
    pipeline_ = nil;
    sampler_  = nil;
    owner_    = nullptr;
    width_    = 0;
    height_   = 0;
}

void MetalOutputSurface::resize(uint32_t width, uint32_t height)
{
    if (!layer_ || width == 0 || height == 0)
    {
        return;
    }

    const CGFloat scale = layer_.contentsScale > 0.0 ? layer_.contentsScale : 1.0;
    layer_.drawableSize = CGSizeMake(width * scale, height * scale);

    width_  = static_cast<uint32_t>(width * scale);
    height_ = static_cast<uint32_t>(height * scale);
}

void MetalOutputSurface::present(GpuTexture& frame)
{
    if (!layer_ || !pipeline_ || !owner_)
    {
        return;
    }

    MetalTexture& source = static_cast<MetalTexture&>(frame);
    if (!source.valid() || width_ == 0 || height_ == 0)
    {
        return;
    }

    @autoreleasepool
    {
        // Blocks on the output display's vsync. That is deliberate: this is
        // the clock the show runs on.
        id<CAMetalDrawable> drawable = [layer_ nextDrawable];
        if (!drawable)
        {
            return;
        }

        const float targetAspect = static_cast<float>(width_) / static_cast<float>(height_);
        const float sourceAspect =
            static_cast<float>(source.width()) / static_cast<float>(std::max(source.height(), 1u));

        // Fit, the same arithmetic as shaders/metal/source_blit.metal.
        float scale[4] = {1.0f, 1.0f, 0.0f, 0.0f};
        if (sourceAspect > targetAspect)
        {
            scale[1] = sourceAspect / targetAspect;
        }
        else
        {
            scale[0] = targetAspect / sourceAspect;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture     = drawable.texture;
        pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor  = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLCommandBuffer>        commands = [owner_->commandQueue() commandBuffer];
        id<MTLRenderCommandEncoder> encoder  = [commands renderCommandEncoderWithDescriptor:pass];

        [encoder setRenderPipelineState:pipeline_];
        [encoder setFragmentBytes:scale length:sizeof(scale) atIndex:0];
        [encoder setFragmentTexture:source.metal() atIndex:0];
        [encoder setFragmentSamplerState:sampler_ atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];

        [commands presentDrawable:drawable];
        [commands commit];
    }
}

// ---------------------------------------------------------------------------
// MetalDevice
// ---------------------------------------------------------------------------

bool MetalDevice::initialize(Window* window, uint32_t processingWidth, uint32_t processingHeight)
{
    device_ = MTLCreateSystemDefaultDevice();
    if (!device_)
    {
        ATEMFX_LOG_ERROR("No Metal device available");
        return false;
    }

    queue_ = [device_ newCommandQueue];
    if (!queue_)
    {
        ATEMFX_LOG_ERROR("Failed to create the Metal command queue");
        return false;
    }

    adapterName_ = std::string([[device_ name] UTF8String] ?: "unknown");
    headless_    = window == nullptr;

    if (!headless_)
    {
        NSView* view = (__bridge NSView*)window->nativeHandle();
        if (!view)
        {
            ATEMFX_LOG_ERROR("Window has no native view");
            return false;
        }

        layer_                 = [CAMetalLayer layer];
        layer_.device          = device_;
        layer_.pixelFormat     = MTLPixelFormatBGRA8Unorm;
        layer_.framebufferOnly = YES;

        // AppKit requires a custom layer to be assigned before the view is
        // told to become layer-backed; the other order gives you AppKit's own
        // layer and a black window.
        const CGFloat scale  = view.window ? view.window.backingScaleFactor : 1.0;
        layer_.contentsScale = scale;
        layer_.frame         = view.bounds;

        view.layer      = layer_;
        view.wantsLayer = YES;

        onWindowResized(window->width(), window->height());
    }

    std::string error;
    if (!shaders_.initialize(device_, MTLPixelFormatRGBA16Float, error))
    {
        ATEMFX_LOG_ERROR("Shader library: %s", error.c_str());
        return false;
    }

    if (!targets_.create(device_, processingWidth, processingHeight, MTLPixelFormatRGBA16Float))
    {
        return false;
    }

    if (!fullscreenPass_.initialize(*this, error))
    {
        ATEMFX_LOG_ERROR("Fullscreen pass: %s", error.c_str());
        return false;
    }

    ATEMFX_LOG_INFO("Metal device ready: %s (%s)",
                    adapterName_.c_str(),
                    headless_ ? "headless" : "windowed");
    return true;
}

void MetalDevice::shutdown()
{
    fullscreenPass_.shutdown();
    targets_.release();
    shaders_.shutdown();

    uiPassDescriptor_   = nil;
    uiCommands_         = nil;
    processingCommands_ = nil;
    drawable_           = nil;
    layer_              = nil;
    queue_              = nil;
    device_             = nil;
}

bool MetalDevice::beginFrame()
{
    if (headless_)
    {
        return true;
    }

    if (!layer_)
    {
        return false;
    }

    // Blocks until a drawable frees up, which is where vsync pacing happens.
    drawable_ = [layer_ nextDrawable];
    return drawable_ != nil;
}

void MetalDevice::beginProcessing()
{
    processingCommands_ = [queue_ commandBuffer];
}

void MetalDevice::endProcessing()
{
    if (!processingCommands_)
    {
        return;
    }

    // The completion handler runs on a background thread, so the result lands
    // in atomics rather than in members touched by the frame loop.
    std::shared_ptr<TimingState> timing = timing_;
    [processingCommands_ addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        const double milliseconds = (buffer.GPUEndTime - buffer.GPUStartTime) * 1000.0;
        if (milliseconds >= 0.0 && std::isfinite(milliseconds))
        {
            timing->milliseconds.store(static_cast<float>(milliseconds), std::memory_order_relaxed);
            timing->hasResult.store(true, std::memory_order_relaxed);
        }
    }];

    [processingCommands_ commit];
    processingCommands_ = nil;
}

void MetalDevice::beginUi()
{
    if (headless_ || !drawable_)
    {
        return;
    }

    uiCommands_ = [queue_ commandBuffer];

    uiPassDescriptor_                                = [MTLRenderPassDescriptor renderPassDescriptor];
    uiPassDescriptor_.colorAttachments[0].texture    = drawable_.texture;
    uiPassDescriptor_.colorAttachments[0].loadAction = MTLLoadActionClear;
    uiPassDescriptor_.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.04, 0.05, 1.0);
    uiPassDescriptor_.colorAttachments[0].storeAction = MTLStoreActionStore;
}

void MetalDevice::endFrame(bool vsync)
{
    if (headless_)
    {
        return;
    }

    if (vsync != vsync_ && layer_)
    {
        layer_.displaySyncEnabled = vsync ? YES : NO;
        vsync_                    = vsync;
    }

    if (uiCommands_ && drawable_)
    {
        [uiCommands_ presentDrawable:drawable_];
        [uiCommands_ commit];
    }

    uiCommands_       = nil;
    uiPassDescriptor_ = nil;
    drawable_         = nil;
}

void MetalDevice::onWindowResized(uint32_t width, uint32_t height)
{
    if (!layer_ || width == 0 || height == 0)
    {
        return;
    }

    const CGFloat scale = layer_.contentsScale > 0.0 ? layer_.contentsScale : 1.0;
    layer_.drawableSize = CGSizeMake(width * scale, height * scale);
}

std::unique_ptr<OutputSurface> MetalDevice::createOutputSurface(void*    nativeHandle,
                                                                uint32_t width,
                                                                uint32_t height)
{
    if (headless_)
    {
        ATEMFX_LOG_ERROR("A headless device has no display output");
        return nullptr;
    }

    auto surface = std::make_unique<MetalOutputSurface>();
    if (!surface->initialize(*this, nativeHandle, width, height))
    {
        return nullptr;
    }
    return surface;
}

float MetalDevice::lastGpuMilliseconds() const
{
    return timing_->milliseconds.load(std::memory_order_relaxed);
}

bool MetalDevice::hasGpuTiming() const
{
    return timing_->hasResult.load(std::memory_order_relaxed);
}

bool MetalDevice::readback(GpuTexture& texture, std::vector<uint8_t>& rgba)
{
    MetalTexture& source = static_cast<MetalTexture&>(texture);
    if (!source.valid())
    {
        return false;
    }

    // The half-float unpacking below is specific to RGBA16Float, which is the
    // only format the pool hands out. Fail loudly rather than silently produce
    // garbage if that ever changes.
    if (source.metal().pixelFormat != MTLPixelFormatRGBA16Float)
    {
        ATEMFX_LOG_ERROR("Readback only supports RGBA16Float");
        return false;
    }

    const NSUInteger width       = source.width();
    const NSUInteger height      = source.height();
    const NSUInteger bytesPerRow = width * 8;  // RGBA16Float

    id<MTLBuffer> staging = [device_ newBufferWithLength:bytesPerRow * height
                                                 options:MTLResourceStorageModeShared];
    if (!staging)
    {
        return false;
    }

    id<MTLCommandBuffer>      commands = [queue_ commandBuffer];
    id<MTLBlitCommandEncoder> blit     = [commands blitCommandEncoder];
    [blit copyFromTexture:source.metal()
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(width, height, 1)
                     toBuffer:staging
            destinationOffset:0
       destinationBytesPerRow:bytesPerRow
     destinationBytesPerImage:bytesPerRow * height];
    [blit endEncoding];
    [commands commit];
    [commands waitUntilCompleted];

    const uint16_t* halves = static_cast<const uint16_t*>([staging contents]);
    rgba.resize(static_cast<std::size_t>(width) * height * 4);

    for (std::size_t i = 0; i < rgba.size(); ++i)
    {
        const float value = std::clamp(halfToFloat(halves[i]), 0.0f, 1.0f);
        rgba[i]           = static_cast<uint8_t>(value * 255.0f + 0.5f);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Backend factory
// ---------------------------------------------------------------------------

std::unique_ptr<GraphicsDevice> createGraphicsDevice()
{
    return std::make_unique<MetalDevice>();
}

const char* backendName()
{
    return "Metal";
}

} // namespace atemfx
