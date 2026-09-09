// PROGRAM as a webcam on macOS.
//
// macOS 12.3 deprecated CoreMediaIO DAL plug-ins and macOS 13 replaced them
// with Camera Extensions: a signed system extension the user approves once,
// after which every call application sees the device. Shipping our own means
// a Developer ID signature, notarisation and an approved system extension —
// none of which an unsigned build has. See docs/VIRTUAL_CAMERA.md.
//
// So this does not install anything. It connects, as an ordinary client, to
// the *sink* stream of a camera extension that is already on the machine —
// the one OBS Studio installs. A camera extension device carries two streams:
// the source that call applications read, and a sink any process may push
// frames into. That is how OBS itself feeds it, and its extension authorises
// every client, so there is nothing OBS-specific about the transport beyond
// which device we look for.
//
// The consequence to keep in mind: the device is named after the extension
// that owns it, so Zoom and Meet list "OBS Virtual Camera", not CamVJ.

#include "video/virtual_camera.h"

#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreMediaIO/CMIOHardware.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Log.h"
#include "gpu/metal/MetalDevice.h"

namespace atemfx {

namespace {

// The stream format the OBS extension declares, and the only one it declares.
// It happens to be the engine canvas, so PROGRAM arrives without a resample.
constexpr uint32_t kFrameWidth  = 1920;
constexpr uint32_t kFrameHeight = 1080;

// Frames in flight between the render thread and the extension. Small on
// purpose: a consumer that stops draining must cost dropped frames, never a
// growing queue and never a stalled PROGRAM.
constexpr int kMaxPixelBuffers = 4;

constexpr const char* kObsBundleId     = "com.obsproject.obs-studio";
constexpr const char* kObsPlugInPath   = "Contents/PlugIns/mac-virtualcam.plugin";
constexpr const char* kObsDeviceUuidKey = "OBSCameraDeviceUUID";

// OBS 32 ships this device id. It is only the fallback: the id is read from
// the installed plug-in first, so an OBS release that reissues it still works.
constexpr const char* kObsDeviceUuidFallback = "7626645E-4425-469E-9D8B-97E0FA59AC75";

// Program frame (RGBA16Float) into the extension's BGRA8 buffer. The same
// fit arithmetic the display output uses: a fixed raster downstream cannot
// correct a stretched picture, so letterbox rather than distort.
constexpr const char* kWebcamShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
};

vertex VSOutput webcam_vertex(uint vertexId [[vertex_id]])
{
    const float2 uv = float2(float((vertexId << 1) & 2u), float(vertexId & 2u));

    VSOutput output;
    output.uv       = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

fragment float4 webcam_fragment(VSOutput in [[stage_in]],
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

NSString* stringProperty(CMIOObjectID object, CMIOObjectPropertySelector selector)
{
    CMIOObjectPropertyAddress address{selector, kCMIOObjectPropertyScopeGlobal,
                                      kCMIOObjectPropertyElementMain};
    UInt32 size = 0;
    if (CMIOObjectGetPropertyDataSize(object, &address, 0, nullptr, &size) != kCMIOHardwareNoError)
    {
        return nil;
    }

    CFStringRef value = nullptr;
    UInt32      used  = 0;
    if (CMIOObjectGetPropertyData(object, &address, 0, nullptr, size, &used, &value) !=
            kCMIOHardwareNoError ||
        !value)
    {
        return nil;
    }
    return (__bridge_transfer NSString*)value;
}

// The device id OBS gave its extension, read from the installed plug-in.
NSString* obsDeviceUuid()
{
    NSURL* application = [[NSWorkspace sharedWorkspace]
        URLForApplicationWithBundleIdentifier:@(kObsBundleId)];
    if (application)
    {
        NSURL* plist = [[application URLByAppendingPathComponent:@(kObsPlugInPath)]
            URLByAppendingPathComponent:@"Contents/Info.plist"];
        NSDictionary* info = [NSDictionary dictionaryWithContentsOfURL:plist];
        NSString*     uuid = info[@(kObsDeviceUuidKey)];
        if ([uuid isKindOfClass:[NSString class]] && uuid.length > 0)
        {
            return uuid;
        }
    }
    return @(kObsDeviceUuidFallback);
}

// Everything the completion handler touches, so a frame still in the GPU
// queue when the operator stops the webcam cannot reach a released stream.
// Shared with the block by value, which is why it outlives the output.
struct Transport
{
    std::mutex       mutex;
    CMSimpleQueueRef queue  = nullptr;   // null once the stream is closed
    CMIODeviceID     device = 0;
    CMIOStreamID     stream = 0;

    CMFormatDescriptionRef format = nullptr;

    std::atomic<uint64_t> sent{0};
    std::atomic<uint64_t> skipped{0};

    ~Transport()
    {
        if (format)
        {
            CFRelease(format);
        }
    }

    // Off the render thread: a Metal completion handler, never the frame loop.
    void enqueue(CVPixelBufferRef pixels)
    {
        CMSampleTimingInfo timing{};
        timing.presentationTimeStamp = CMClockGetTime(CMClockGetHostTimeClock());
        timing.duration              = kCMTimeInvalid;
        timing.decodeTimeStamp       = kCMTimeInvalid;

        std::lock_guard<std::mutex> lock(mutex);
        if (!queue)
        {
            skipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        CMSampleBufferRef sample = nullptr;
        const OSStatus    status = CMSampleBufferCreateForImageBuffer(
            kCFAllocatorDefault, pixels, true, nullptr, nullptr, format, &timing, &sample);
        if (status != noErr || !sample)
        {
            skipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Full means the extension is not draining as fast as we produce.
        // Dropping here is the contract: the queue is the bound.
        if (CMSimpleQueueEnqueue(queue, sample) != noErr)
        {
            CFRelease(sample);
            skipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        sent.fetch_add(1, std::memory_order_relaxed);
    }
};

class ObsSinkCamera final : public VirtualCameraOutput
{
public:
    explicit ObsSinkCamera(MetalDevice& device) : owner_(&device) {}

    ~ObsSinkCamera() override
    {
        requestStop();
        if (worker_.joinable())
        {
            worker_.join();
        }
        releaseGpuResources();
    }

    bool initialize(std::string& error)
    {
        if (!buildPipeline(error) || !buildBufferPool(error))
        {
            return false;
        }

        transport_->format = nullptr;
        if (CMVideoFormatDescriptionCreate(kCFAllocatorDefault, kCVPixelFormatType_32BGRA,
                                           static_cast<int32_t>(kFrameWidth),
                                           static_cast<int32_t>(kFrameHeight), nullptr,
                                           &transport_->format) != noErr)
        {
            error = "Could not describe the 1920x1080 BGRA webcam format";
            return false;
        }

        // Connecting talks to a system extension over IPC and can take long
        // enough to be seen as a dropped frame. It happens on the worker.
        worker_ = std::thread([this] { run(); });
        return true;
    }

    void submit(GpuTexture& frame) override
    {
        if (state_.load(std::memory_order_acquire) != VirtualCameraState::Sending)
        {
            return;
        }

        MetalTexture& source = static_cast<MetalTexture&>(frame);
        if (!source.valid() || !pipeline_ || !pool_ || !textureCache_)
        {
            return;
        }

        id<MTLCommandBuffer> commands = owner_->processingCommandBuffer();
        if (!commands)
        {
            transport_->skipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        @autoreleasepool
        {
            CVMetalTextureCacheFlush(textureCache_, 0);

            CVPixelBufferRef pixels = nullptr;
            // WithAuxAttributes so exhausting the pool fails here rather than
            // growing without bound behind a consumer that stopped reading.
            if (CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(
                    kCFAllocatorDefault, pool_, (__bridge CFDictionaryRef)poolAuxAttributes_,
                    &pixels) != kCVReturnSuccess ||
                !pixels)
            {
                transport_->skipped.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            CVMetalTextureRef mapped = nullptr;
            if (CVMetalTextureCacheCreateTextureFromImage(
                    kCFAllocatorDefault, textureCache_, pixels, nullptr, MTLPixelFormatBGRA8Unorm,
                    kFrameWidth, kFrameHeight, 0, &mapped) != kCVReturnSuccess ||
                !mapped)
            {
                CVPixelBufferRelease(pixels);
                transport_->skipped.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            id<MTLTexture> target = CVMetalTextureGetTexture(mapped);
            if (!target)
            {
                CFRelease(mapped);
                CVPixelBufferRelease(pixels);
                transport_->skipped.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            const float targetAspect = static_cast<float>(kFrameWidth) /
                                       static_cast<float>(kFrameHeight);
            const float sourceAspect = static_cast<float>(source.width()) /
                                       static_cast<float>(std::max(source.height(), 1u));

            float scale[4] = {1.0f, 1.0f, 0.0f, 0.0f};
            if (sourceAspect > targetAspect)
            {
                scale[1] = sourceAspect / targetAspect;
            }
            else
            {
                scale[0] = targetAspect / sourceAspect;
            }

            MTLRenderPassDescriptor* pass        = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture     = target;
            pass.colorAttachments[0].loadAction  = MTLLoadActionDontCare;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;

            // The processing command buffer, not one of our own: the frame is
            // already there, and a second buffer would need a fence to know
            // the chain had finished writing it.
            id<MTLRenderCommandEncoder> encoder =
                [commands renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:pipeline_];
            [encoder setFragmentBytes:scale length:sizeof(scale) atIndex:0];
            [encoder setFragmentTexture:source.metal() atIndex:0];
            [encoder setFragmentSamplerState:sampler_ atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [encoder endEncoding];

            // The buffer only holds a picture once the GPU says so, and the
            // handler runs off the frame loop. Both references are owned by
            // the block: ARC does not retain CoreFoundation captures.
            std::shared_ptr<Transport> transport = transport_;
            [commands addCompletedHandler:^(id<MTLCommandBuffer>) {
                transport->enqueue(pixels);
                CFRelease(mapped);
                CVPixelBufferRelease(pixels);
            }];
        }
    }

    void requestStop() override
    {
        {
            std::lock_guard<std::mutex> lock(stopMutex_);
            stopRequested_ = true;
        }
        stopSignal_.notify_all();
    }

    VirtualCameraStats stats() const override
    {
        VirtualCameraStats snapshot;
        snapshot.state   = state_.load(std::memory_order_acquire);
        snapshot.sent    = transport_->sent.load(std::memory_order_relaxed);
        snapshot.skipped = transport_->skipped.load(std::memory_order_relaxed);
        return snapshot;
    }

    const std::string& error() const override { return error_; }

private:
    void run()
    {
        std::string error;
        if (!connect(error))
        {
            error_ = error;
            state_.store(VirtualCameraState::Failed, std::memory_order_release);
            return;
        }

        ATEMFX_LOG_INFO("Virtual camera: sending PROGRAM to the installed camera extension");
        state_.store(VirtualCameraState::Sending, std::memory_order_release);

        {
            std::unique_lock<std::mutex> lock(stopMutex_);
            stopSignal_.wait(lock, [this] { return stopRequested_; });
        }

        state_.store(VirtualCameraState::Stopping, std::memory_order_release);
        disconnect();
        state_.store(VirtualCameraState::Stopped, std::memory_order_release);
        ATEMFX_LOG_INFO("Virtual camera: released the camera extension");
    }

    bool connect(std::string& error)
    {
        CMIOObjectPropertyAddress address{kCMIOHardwarePropertyDevices,
                                          kCMIOObjectPropertyScopeGlobal,
                                          kCMIOObjectPropertyElementMain};
        UInt32 size = 0;
        if (CMIOObjectGetPropertyDataSize(kCMIOObjectSystemObject, &address, 0, nullptr, &size) !=
            kCMIOHardwareNoError)
        {
            error = "CoreMediaIO would not report the video devices";
            return false;
        }

        const std::size_t count = size / sizeof(CMIOObjectID);
        std::vector<CMIOObjectID> devices(count);
        UInt32                    used = 0;
        if (count == 0 || CMIOObjectGetPropertyData(kCMIOObjectSystemObject, &address, 0, nullptr,
                                                    size, &used, devices.data()) !=
                              kCMIOHardwareNoError)
        {
            error = "CoreMediaIO reported no video devices";
            return false;
        }

        NSString*    wanted = obsDeviceUuid();
        CMIODeviceID found  = 0;
        for (CMIOObjectID device : devices)
        {
            NSString* uid = stringProperty(device, kCMIODevicePropertyDeviceUID);
            if (uid && [uid caseInsensitiveCompare:wanted] == NSOrderedSame)
            {
                found = device;
                break;
            }

            // A renamed or reissued device is still the right one if it calls
            // itself the OBS camera. The stream count check below is what
            // actually decides whether we can push into it.
            NSString* name = stringProperty(device, kCMIOObjectPropertyName);
            if (found == 0 && name && [name containsString:@"OBS Virtual Camera"])
            {
                found = device;
            }
        }

        if (found == 0)
        {
            error = "No camera extension found. Install OBS Studio, open it once, "
                    "and enable OBS Virtual Camera in System Settings > General > "
                    "Login Items & Extensions > Camera Extensions.";
            return false;
        }

        address.mSelector = kCMIODevicePropertyStreams;
        if (CMIOObjectGetPropertyDataSize(found, &address, 0, nullptr, &size) !=
                kCMIOHardwareNoError ||
            size < 2 * sizeof(CMIOStreamID))
        {
            error = "The camera extension exposes no sink stream to send frames to";
            return false;
        }

        std::vector<CMIOStreamID> streams(size / sizeof(CMIOStreamID));
        if (CMIOObjectGetPropertyData(found, &address, 0, nullptr, size, &used, streams.data()) !=
            kCMIOHardwareNoError)
        {
            error = "Could not read the camera extension's streams";
            return false;
        }

        // Stream 0 is the source call applications read; stream 1 is the sink.
        const CMIOStreamID sink = streams[1];

        CMSimpleQueueRef queue = nullptr;
        if (CMIOStreamCopyBufferQueue(
                sink, [](CMIOStreamID, void*, void*) {}, nullptr, &queue) != noErr ||
            !queue)
        {
            error = "The camera extension refused a buffer queue";
            return false;
        }

        if (CMIODeviceStartStream(found, sink) != noErr)
        {
            CFRelease(queue);
            error = "The camera extension refused to start. Another application may be "
                    "sending to it already.";
            return false;
        }

        std::lock_guard<std::mutex> lock(transport_->mutex);
        transport_->device = found;
        transport_->stream = sink;
        transport_->queue  = queue;
        return true;
    }

    void disconnect()
    {
        CMSimpleQueueRef queue  = nullptr;
        CMIODeviceID     device = 0;
        CMIOStreamID     stream = 0;
        {
            // Cleared under the lock so a completion handler that arrives
            // during the stop drops its frame instead of using the stream.
            std::lock_guard<std::mutex> lock(transport_->mutex);
            queue              = transport_->queue;
            device             = transport_->device;
            stream             = transport_->stream;
            transport_->queue  = nullptr;
        }

        if (!queue)
        {
            return;
        }

        CMIODeviceStopStream(device, stream);
        CFRelease(queue);
    }

    bool buildPipeline(std::string& error)
    {
        id<MTLDevice> device = owner_->metal();
        if (!device)
        {
            error = "No Metal device";
            return false;
        }

        NSError*       compileError = nil;
        id<MTLLibrary> library      = [device newLibraryWithSource:@(kWebcamShaderSource)
                                                      options:nil
                                                        error:&compileError];
        if (!library)
        {
            error = std::string("Webcam shader: ") +
                    ([[compileError localizedDescription] UTF8String] ?: "unknown error");
            return false;
        }

        MTLRenderPipelineDescriptor* descriptor   = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction                 = [library newFunctionWithName:@"webcam_vertex"];
        descriptor.fragmentFunction               = [library newFunctionWithName:@"webcam_fragment"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        pipeline_ = [device newRenderPipelineStateWithDescriptor:descriptor error:&compileError];
        if (!pipeline_)
        {
            error = std::string("Webcam pipeline: ") +
                    ([[compileError localizedDescription] UTF8String] ?: "unknown error");
            return false;
        }

        MTLSamplerDescriptor* sampler = [[MTLSamplerDescriptor alloc] init];
        sampler.minFilter             = MTLSamplerMinMagFilterLinear;
        sampler.magFilter             = MTLSamplerMinMagFilterLinear;
        sampler.sAddressMode          = MTLSamplerAddressModeClampToEdge;
        sampler.tAddressMode          = MTLSamplerAddressModeClampToEdge;
        sampler_                      = [device newSamplerStateWithDescriptor:sampler];

        NSDictionary* textureAttributes = @{
            (id)kCVMetalTextureUsage : @(MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead)
        };
        if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device,
                                      (__bridge CFDictionaryRef)textureAttributes,
                                      &textureCache_) != kCVReturnSuccess)
        {
            error = "Could not create the Metal texture cache for webcam frames";
            return false;
        }

        return true;
    }

    bool buildBufferPool(std::string& error)
    {
        NSDictionary* attributes = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey : @(kFrameWidth),
            (id)kCVPixelBufferHeightKey : @(kFrameHeight),
            (id)kCVPixelBufferMetalCompatibilityKey : @YES,
            // IOSurface backing is what lets the extension read the frame
            // without a copy through system memory.
            (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
        };

        if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr,
                                    (__bridge CFDictionaryRef)attributes,
                                    &pool_) != kCVReturnSuccess)
        {
            error = "Could not allocate the webcam frame pool";
            return false;
        }

        poolAuxAttributes_ =
            @{(id)kCVPixelBufferPoolAllocationThresholdKey : @(kMaxPixelBuffers)};
        return true;
    }

    void releaseGpuResources()
    {
        if (textureCache_)
        {
            CFRelease(textureCache_);
            textureCache_ = nullptr;
        }
        if (pool_)
        {
            CVPixelBufferPoolRelease(pool_);
            pool_ = nullptr;
        }
        pipeline_ = nil;
        sampler_  = nil;
    }

    MetalDevice*               owner_    = nullptr;
    id<MTLRenderPipelineState> pipeline_ = nil;
    id<MTLSamplerState>        sampler_  = nil;

    CVPixelBufferPoolRef   pool_              = nullptr;
    NSDictionary*          poolAuxAttributes_ = nil;
    CVMetalTextureCacheRef textureCache_      = nullptr;

    std::shared_ptr<Transport> transport_ = std::make_shared<Transport>();

    std::thread             worker_;
    std::mutex              stopMutex_;
    std::condition_variable stopSignal_;
    bool                    stopRequested_ = false;

    std::atomic<VirtualCameraState> state_{VirtualCameraState::Starting};
    std::string                     error_;
};

} // namespace

bool virtualCameraSupported()
{
    if (@available(macOS 13.0, *))
    {
        return true;
    }
    return false;
}

std::unique_ptr<VirtualCameraOutput> createVirtualCameraOutput(GraphicsDevice& device,
                                                               std::string&    error)
{
    if (!virtualCameraSupported())
    {
        error = "Webcam output needs macOS 13 or newer";
        return nullptr;
    }

    auto camera = std::make_unique<ObsSinkCamera>(static_cast<MetalDevice&>(device));
    if (!camera->initialize(error))
    {
        return nullptr;
    }
    return camera;
}

} // namespace atemfx
