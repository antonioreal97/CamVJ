// PROGRAM to an Apple ProRes 422 HQ movie on macOS.
//
// The frame the webcam and the wall receive is RGBA16Float at the processing
// resolution. ProRes 422 stores 10-bit 4:2:2 Y'CbCr, so the conversion is
// done here, on the GPU, into the encoder's own layout
// ('x422': 10-bit bi-planar 4:2:2, video range, Rec.709) — the encoder gets
// exactly what it stores and has nothing left to convert or dither. Where
// Metal cannot map that layout the recorder falls back to 8-bit BGRA and
// says so in the panel; the file is still ProRes 422 HQ.
//
// Nothing here runs on the frame loop except command encoding. Opening the
// file, appending frames and closing the file happen on one serial queue; a
// GPU completion handler is the only thing that hands a frame to it.
//
// The movie is written with fragments, so a crash or a pulled power cable
// leaves a file that plays up to the last fragment instead of an unreadable
// one. See docs/RECORDING.md.

#include "video/program_recorder.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <system_error>

#include "core/Log.h"
#include "gpu/metal/MetalDevice.h"

namespace atemfx {

namespace {

// The engine canvas. Recording it at any other size would only resample.
constexpr uint32_t kFrameWidth  = 1920;
constexpr uint32_t kFrameHeight = 1080;

// Frames between the GPU and the encoder. A 1080p x422 buffer is about 8 MB;
// eight is a quarter-second of slack at 60 fps, enough to ride out a slow
// fragment flush, and a hard bound when the disk cannot keep up.
constexpr int kMaxPixelBuffers = 8;

// 60000 carries 59.94 (1001 ticks) and 60 (1000 ticks) exactly.
constexpr int32_t kTimescale      = 60000;
constexpr int64_t kNominalFrame   = 1001;

// How long a fragment may hold frames before it is flushed to disk: the most
// a crash can lose.
constexpr double kFragmentSeconds = 2.0;

// How long destroying a recorder waits for the movie to close.
constexpr auto kFinishTimeout = std::chrono::seconds(20);

constexpr const char* kRecorderShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
};

vertex VSOutput rec_vertex(uint vertexId [[vertex_id]])
{
    const float2 uv = float2(float((vertexId << 1) & 2u), float(vertexId & 2u));

    VSOutput output;
    output.uv       = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

// scale.xy: fit (letterbox, never stretch), the same arithmetic as the webcam
// and the display output. scale.z: one luma pixel in uv.
static float3 fetch(float2 uv, float4 scale, texture2d<float> source, sampler samp)
{
    uv = (uv - 0.5) * scale.xy + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        return float3(0.0);
    }
    // Half floats carry values past white; the file cannot.
    return clamp(source.sample(samp, uv).rgb, 0.0, 1.0);
}

// Rec.709, the matrix the movie is tagged with.
constant float3 kLuma = float3(0.2126, 0.7152, 0.0722);

// A 10-bit code in the top ten bits of a 16-bit sample, which is how 'x422'
// stores it. Exact: the unorm write rounds code*64/65535 back to code*64.
static float code10(float code)
{
    return round(clamp(code, 0.0, 1023.0)) * 64.0 / 65535.0;
}

fragment float4 rec_luma(VSOutput in [[stage_in]],
                         constant float4& scale [[buffer(0)]],
                         texture2d<float> source [[texture(0)]],
                         sampler samp [[sampler(0)]])
{
    const float y = dot(fetch(in.uv, scale, source, samp), kLuma);
    return float4(code10(64.0 + 876.0 * y), 0.0, 0.0, 1.0);
}

// The chroma plane is half as wide: each sample stands for two luma pixels,
// whose centres are half a luma pixel either side of this one.
fragment float4 rec_chroma(VSOutput in [[stage_in]],
                           constant float4& scale [[buffer(0)]],
                           texture2d<float> source [[texture(0)]],
                           sampler samp [[sampler(0)]])
{
    const float2 step = float2(0.5 * scale.z, 0.0);
    const float3 rgb  = 0.5 * (fetch(in.uv - step, scale, source, samp) +
                               fetch(in.uv + step, scale, source, samp));
    const float  y    = dot(rgb, kLuma);
    const float  cb   = 512.0 + 896.0 * (rgb.b - y) / 1.8556;
    const float  cr   = 512.0 + 896.0 * (rgb.r - y) / 1.5748;
    return float4(code10(cb), code10(cr), 0.0, 1.0);
}

// 8-bit fallback: BGRA, converted by the encoder.
fragment float4 rec_bgra(VSOutput in [[stage_in]],
                         constant float4& scale [[buffer(0)]],
                         texture2d<float> source [[texture(0)]],
                         sampler samp [[sampler(0)]])
{
    return float4(fetch(in.uv, scale, source, samp), 1.0);
}
)MSL";

std::string describe(NSError* error, const char* fallback)
{
    if (error && error.localizedDescription)
    {
        std::string text = error.localizedDescription.UTF8String;
        if (error.localizedFailureReason)
        {
            text += " (";
            text += error.localizedFailureReason.UTF8String;
            text += ")";
        }
        return text;
    }
    return fallback;
}

void tagRec709(CVPixelBufferRef pixels)
{
    CVBufferSetAttachment(pixels, kCVImageBufferColorPrimariesKey,
                          kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixels, kCVImageBufferTransferFunctionKey,
                          kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixels, kCVImageBufferYCbCrMatrixKey,
                          kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
}

CVPixelBufferPoolRef createPool(OSType format)
{
    NSDictionary* attributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(format),
        (id)kCVPixelBufferWidthKey : @(kFrameWidth),
        (id)kCVPixelBufferHeightKey : @(kFrameHeight),
        (id)kCVPixelBufferMetalCompatibilityKey : @YES,
        (id)kCVPixelBufferIOSurfacePropertiesKey : @{},
    };
    CVPixelBufferPoolRef pool = nullptr;
    if (CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr,
                                (__bridge CFDictionaryRef)attributes, &pool) != kCVReturnSuccess)
    {
        return nullptr;
    }
    return pool;
}

// Plane textures for one pixel buffer: luma and chroma for x422, one BGRA
// texture otherwise. On success the caller owns the references.
bool mapPlanes(CVMetalTextureCacheRef cache,
               CVPixelBufferRef       pixels,
               bool                   tenBit,
               CVMetalTextureRef      planes[2])
{
    planes[0] = nullptr;
    planes[1] = nullptr;

    if (!tenBit)
    {
        const bool mapped = CVMetalTextureCacheCreateTextureFromImage(
                                kCFAllocatorDefault, cache, pixels, nullptr, MTLPixelFormatBGRA8Unorm,
                                kFrameWidth, kFrameHeight, 0, &planes[0]) == kCVReturnSuccess &&
                            planes[0] && CVMetalTextureGetTexture(planes[0]);
        if (!mapped && planes[0])
        {
            CFRelease(planes[0]);
            planes[0] = nullptr;
        }
        return mapped;
    }

    const bool luma = CVMetalTextureCacheCreateTextureFromImage(
                          kCFAllocatorDefault, cache, pixels, nullptr, MTLPixelFormatR16Unorm,
                          kFrameWidth, kFrameHeight, 0, &planes[0]) == kCVReturnSuccess &&
                      planes[0] && CVMetalTextureGetTexture(planes[0]);
    const bool chroma = luma &&
                        CVMetalTextureCacheCreateTextureFromImage(
                            kCFAllocatorDefault, cache, pixels, nullptr, MTLPixelFormatRG16Unorm,
                            kFrameWidth / 2, kFrameHeight, 1, &planes[1]) == kCVReturnSuccess &&
                        planes[1] && CVMetalTextureGetTexture(planes[1]);
    if (!chroma)
    {
        if (planes[0])
        {
            CFRelease(planes[0]);
        }
        if (planes[1])
        {
            CFRelease(planes[1]);
        }
        planes[0] = nullptr;
        planes[1] = nullptr;
        return false;
    }
    return true;
}

// Everything the serial queue and the GPU completion handlers touch. Shared
// with every block by value, so a frame still in flight when the recorder is
// destroyed cannot reach a released writer, pool or texture cache.
struct Session
{
    std::filesystem::path path;

    dispatch_queue_t                      queue   = nil;
    AVAssetWriter*                        writer  = nil;
    AVAssetWriterInput*                   input   = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;

    // Built on the queue before the first frame, read by the frame loop only
    // once `state` says Recording.
    id<MTLDevice>              device            = nil;
    id<MTLRenderPipelineState> lumaPipeline      = nil;
    id<MTLRenderPipelineState> chromaPipeline    = nil;
    id<MTLRenderPipelineState> bgraPipeline      = nil;
    id<MTLSamplerState>        sampler           = nil;
    NSDictionary*              poolAuxAttributes = nil;
    CVPixelBufferPoolRef       pool              = nullptr;
    CVMetalTextureCacheRef     textureCache      = nullptr;

    // Queue only.
    bool   sessionStarted = false;
    bool   closing        = false;
    CMTime firstTime      = kCMTimeInvalid;
    CMTime lastTime       = kCMTimeInvalid;
    std::chrono::steady_clock::time_point lastPoll{};

    std::atomic<RecorderState> state{RecorderState::Starting};
    std::atomic<uint64_t>      written{0};
    std::atomic<uint64_t>      dropped{0};
    std::atomic<double>        seconds{0.0};
    std::atomic<uint64_t>      fileBytes{0};
    std::atomic<uint64_t>      freeBytes{0};
    std::atomic<bool>          tenBit{false};

    std::mutex              doneMutex;
    std::condition_variable doneSignal;
    std::string             error;   // published before state becomes Failed

    ~Session()
    {
        if (textureCache)
        {
            CFRelease(textureCache);
        }
        if (pool)
        {
            CVPixelBufferPoolRelease(pool);
        }
    }

    bool finished() const
    {
        const RecorderState s = state.load(std::memory_order_acquire);
        return s == RecorderState::Stopped || s == RecorderState::Failed;
    }

    void settle(RecorderState outcome)
    {
        {
            std::lock_guard<std::mutex> lock(doneMutex);
            state.store(outcome, std::memory_order_release);
        }
        doneSignal.notify_all();
    }

    void fail(const std::string& message)
    {
        if (finished())
        {
            return;
        }
        error   = message;
        closing = true;
        ATEMFX_LOG_ERROR("Recording: %s", message.c_str());
        if (writer && writer.status == AVAssetWriterStatusWriting)
        {
            // Fragments already on disk stay playable; cancelling only stops
            // the writer from touching the file again.
            [writer cancelWriting];
        }
        settle(RecorderState::Failed);
    }

    void pollDisk(bool force)
    {
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - lastPoll < std::chrono::seconds(1))
        {
            return;
        }
        lastPoll = now;

        std::error_code ec;
        const auto      size = std::filesystem::file_size(path, ec);
        if (!ec)
        {
            fileBytes.store(size, std::memory_order_relaxed);
        }
        const auto space = std::filesystem::space(path.parent_path(), ec);
        if (!ec)
        {
            freeBytes.store(space.available, std::memory_order_relaxed);
        }
    }

    // On the queue. Compiling shaders and probing pixel formats take long
    // enough to be seen as a dropped frame, so they happen here too.
    void open()
    {
        std::string buildError;
        if (!buildPipelines(buildError) || !buildPool(buildError))
        {
            fail(buildError);
            return;
        }

        NSURL*   url   = [NSURL fileURLWithPath:@(path.c_str())];
        NSError* createError = nil;
        writer = [[AVAssetWriter alloc] initWithURL:url
                                           fileType:AVFileTypeQuickTimeMovie
                                              error:&createError];
        if (!writer)
        {
            fail("Could not create the movie: " + describe(createError, "unknown error"));
            return;
        }
        writer.movieFragmentInterval    = CMTimeMakeWithSeconds(kFragmentSeconds, kTimescale);
        writer.movieTimeScale           = kTimescale;
        writer.shouldOptimizeForNetworkUse = NO;

        NSDictionary* settings = @{
            AVVideoCodecKey : AVVideoCodecTypeAppleProRes422HQ,
            AVVideoWidthKey : @(kFrameWidth),
            AVVideoHeightKey : @(kFrameHeight),
            AVVideoColorPropertiesKey : @{
                AVVideoColorPrimariesKey : AVVideoColorPrimaries_ITU_R_709_2,
                AVVideoTransferFunctionKey : AVVideoTransferFunction_ITU_R_709_2,
                AVVideoYCbCrMatrixKey : AVVideoYCbCrMatrix_ITU_R_709_2,
            },
        };
        if (![writer canApplyOutputSettings:settings forMediaType:AVMediaTypeVideo])
        {
            fail("This Mac cannot encode ProRes 422 HQ at 1920x1080");
            return;
        }

        input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                   outputSettings:settings];
        input.expectsMediaDataInRealTime = YES;
        input.mediaTimeScale             = kTimescale;
        adaptor = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                       sourcePixelBufferAttributes:nil];

        if (![writer canAddInput:input])
        {
            fail("The movie writer refused a ProRes video track");
            return;
        }
        [writer addInput:input];

        if (![writer startWriting])
        {
            fail("Could not start writing: " + describe(writer.error, "unknown error"));
            return;
        }

        pollDisk(true);
        ATEMFX_LOG_INFO("Recording: ProRes 422 HQ %s to %s", tenBit.load() ? "10-bit" : "8-bit",
                        path.string().c_str());

        RecorderState expected = RecorderState::Starting;
        state.compare_exchange_strong(expected, RecorderState::Recording, std::memory_order_acq_rel);
    }

    // On the queue. Owns one reference to `pixels` and releases it.
    void append(CVPixelBufferRef pixels, CMTime time)
    {
        // Stopping still appends: frames submitted before the stop are ahead
        // of finish() on this queue and belong to the take. `closing` is set
        // by finish() itself, so anything after it arrived too late.
        const RecorderState current = state.load(std::memory_order_acquire);
        if (closing || (current != RecorderState::Recording && current != RecorderState::Stopping))
        {
            // In flight when the take ended. Not a drop: the take is over.
            CVPixelBufferRelease(pixels);
            return;
        }

        const bool accepted = [&]() {
            if (writer.status != AVAssetWriterStatusWriting)
            {
                fail("The movie writer stopped: " + describe(writer.error, "unknown error"));
                return false;
            }

            if (!sessionStarted)
            {
                [writer startSessionAtSourceTime:time];
                sessionStarted = true;
                firstTime      = time;
            }
            else if (CMTimeCompare(time, lastTime) <= 0)
            {
                // Two frames inside one tick of the movie clock: the second
                // cannot be placed, and it is not a picture anyone would see.
                return false;
            }

            if (!input.readyForMoreMediaData)
            {
                return false;
            }

            if (![adaptor appendPixelBuffer:pixels withPresentationTime:time])
            {
                // Disk full and a removed drive both arrive here.
                fail("Writing the movie failed: " + describe(writer.error, "unknown error"));
                return false;
            }

            lastTime = time;
            seconds.store(CMTimeGetSeconds(CMTimeSubtract(time, firstTime)),
                          std::memory_order_relaxed);
            return true;
        }();

        CVPixelBufferRelease(pixels);
        (accepted ? written : dropped).fetch_add(1, std::memory_order_relaxed);
        if (accepted)
        {
            pollDisk(false);
        }
    }

    bool buildPipelines(std::string& problem)
    {
        if (!device)
        {
            problem = "No Metal device";
            return false;
        }

        NSError*       compileError = nil;
        id<MTLLibrary> library      = [device newLibraryWithSource:@(kRecorderShaderSource)
                                                      options:nil
                                                        error:&compileError];
        if (!library)
        {
            problem = "Recorder shader: " + describe(compileError, "unknown error");
            return false;
        }

        id<MTLFunction> vertex = [library newFunctionWithName:@"rec_vertex"];
        auto pipeline = [&](NSString* fragment, MTLPixelFormat format) -> id<MTLRenderPipelineState> {
            MTLRenderPipelineDescriptor* descriptor    = [[MTLRenderPipelineDescriptor alloc] init];
            descriptor.vertexFunction                  = vertex;
            descriptor.fragmentFunction                = [library newFunctionWithName:fragment];
            descriptor.colorAttachments[0].pixelFormat = format;
            NSError* pipelineError = nil;
            id<MTLRenderPipelineState> built =
                [device newRenderPipelineStateWithDescriptor:descriptor error:&pipelineError];
            if (!built)
            {
                compileError = pipelineError;
            }
            return built;
        };

        lumaPipeline   = pipeline(@"rec_luma", MTLPixelFormatR16Unorm);
        chromaPipeline = pipeline(@"rec_chroma", MTLPixelFormatRG16Unorm);
        bgraPipeline   = pipeline(@"rec_bgra", MTLPixelFormatBGRA8Unorm);
        if (!lumaPipeline || !chromaPipeline || !bgraPipeline)
        {
            problem = "Recorder pipeline: " + describe(compileError, "unknown error");
            return false;
        }

        MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
        samplerDescriptor.minFilter             = MTLSamplerMinMagFilterLinear;
        samplerDescriptor.magFilter             = MTLSamplerMinMagFilterLinear;
        samplerDescriptor.sAddressMode          = MTLSamplerAddressModeClampToEdge;
        samplerDescriptor.tAddressMode          = MTLSamplerAddressModeClampToEdge;
        sampler = [device newSamplerStateWithDescriptor:samplerDescriptor];

        NSDictionary* textureAttributes = @{
            (id)kCVMetalTextureUsage : @(MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead)
        };
        if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device,
                                      (__bridge CFDictionaryRef)textureAttributes,
                                      &textureCache) != kCVReturnSuccess)
        {
            problem = "Could not create the Metal texture cache for recording";
            return false;
        }
        return true;
    }

    // 10-bit when Metal can render into the encoder's own layout on this
    // machine; decided once, by trying, rather than by a list of models.
    bool buildPool(std::string& problem)
    {
        poolAuxAttributes = @{(id)kCVPixelBufferPoolAllocationThresholdKey : @(kMaxPixelBuffers)};

        if (CVPixelBufferPoolRef candidate = createPool(kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange))
        {
            bool             mapped = false;
            CVPixelBufferRef probe  = nullptr;
            if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, candidate, &probe) ==
                    kCVReturnSuccess &&
                probe)
            {
                CVMetalTextureRef planes[2] = {nullptr, nullptr};
                if (mapPlanes(textureCache, probe, true, planes))
                {
                    CFRelease(planes[0]);
                    CFRelease(planes[1]);
                    mapped = true;
                }
                CVPixelBufferRelease(probe);
            }

            if (mapped)
            {
                pool = candidate;
                tenBit.store(true, std::memory_order_release);
                return true;
            }
            CVPixelBufferPoolRelease(candidate);
        }

        ATEMFX_LOG_WARN("Recording: 10-bit 4:2:2 buffers unavailable to Metal; using 8-bit BGRA");
        pool = createPool(kCVPixelFormatType_32BGRA);
        if (!pool)
        {
            problem = "Could not allocate the recording frame pool";
            return false;
        }
        return true;
    }

    // On the queue.
    void finish(const std::shared_ptr<Session>& keep)
    {
        closing = true;
        if (finished())
        {
            return;
        }

        if (!writer || writer.status != AVAssetWriterStatusWriting)
        {
            fail("The movie writer stopped: " + describe(writer ? writer.error : nil, "unknown error"));
            return;
        }

        if (!sessionStarted)
        {
            // Stopped before the first frame reached the encoder: an empty
            // movie is not a take, so leave nothing behind.
            [writer cancelWriting];
            std::error_code ec;
            std::filesystem::remove(path, ec);
            ATEMFX_LOG_INFO("Recording: stopped before any frame; no file kept");
            settle(RecorderState::Stopped);
            return;
        }

        // The last frame lasts one nominal frame rather than none.
        const CMTime end = CMTimeAdd(lastTime, CMTimeMake(kNominalFrame, kTimescale));
        [input markAsFinished];
        [writer endSessionAtSourceTime:end];
        // A copy, not the reference: blocks must own what they outlive.
        std::shared_ptr<Session> hold = keep;
        [writer finishWritingWithCompletionHandler:^{
            dispatch_async(hold->queue, ^{
                if (hold->writer.status == AVAssetWriterStatusCompleted)
                {
                    hold->pollDisk(true);
                    ATEMFX_LOG_INFO("Recording: closed %s (%llu frames, %llu dropped)",
                                    hold->path.string().c_str(),
                                    static_cast<unsigned long long>(hold->written.load()),
                                    static_cast<unsigned long long>(hold->dropped.load()));
                    hold->settle(RecorderState::Stopped);
                }
                else
                {
                    hold->fail("Closing the movie failed: " +
                               describe(hold->writer.error, "unknown error"));
                }
            });
        }];
    }
};

class ProResRecorder final : public ProgramRecorder
{
public:
    ProResRecorder(MetalDevice& device, std::filesystem::path file) : owner_(&device)
    {
        session_->path   = std::move(file);
        session_->device = device.metal();
    }

    ~ProResRecorder() override
    {
        requestStop();

        // The movie is only playable once it is closed. Waiting here is the
        // price of a file that survives quitting mid-take; bounded, because a
        // hung drive must not hang the application with it.
        std::unique_lock<std::mutex> lock(session_->doneMutex);
        if (!session_->doneSignal.wait_for(lock, kFinishTimeout,
                                           [this] { return session_->finished(); }))
        {
            ATEMFX_LOG_WARN("Recording: %s did not close in time; the file keeps its "
                            "fragments up to the last flush",
                            session_->path.string().c_str());
        }
    }

    bool initialize(std::string& error)
    {
        std::error_code ec;
        std::filesystem::create_directories(session_->path.parent_path(), ec);
        if (ec)
        {
            error = "Could not create " + session_->path.parent_path().string() + ": " + ec.message();
            return false;
        }

        const auto space = std::filesystem::space(session_->path.parent_path(), ec);
        if (!ec && space.available < kRecordingMinimumFreeBytes)
        {
            error = "Not enough free space on " + session_->path.parent_path().string() +
                    " to record (under 2 GB)";
            return false;
        }

        session_->queue = dispatch_queue_create("camvj.recorder", DISPATCH_QUEUE_SERIAL);
        std::shared_ptr<Session> session = session_;
        dispatch_async(session_->queue, ^{
            session->open();
        });
        return true;
    }

    void submit(GpuTexture& frame) override
    {
        Session& session = *session_;
        if (session.state.load(std::memory_order_acquire) != RecorderState::Recording)
        {
            return;
        }

        MetalTexture& source = static_cast<MetalTexture&>(frame);
        id<MTLCommandBuffer> commands = owner_->processingCommandBuffer();
        if (!source.valid() || !commands)
        {
            session.dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Taken now, on the frame loop, not when the GPU finishes: the file's
        // clock must follow when PROGRAM showed the frame, not queue jitter.
        const CMTime time = CMTimeConvertScale(CMClockGetTime(CMClockGetHostTimeClock()),
                                               kTimescale, kCMTimeRoundingMethod_Default);

        @autoreleasepool
        {
            CVMetalTextureCacheFlush(session.textureCache, 0);

            CVPixelBufferRef pixels = nullptr;
            // WithAuxAttributes so a stalled encoder exhausts the pool and
            // costs dropped frames here instead of memory without bound.
            if (CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(
                    kCFAllocatorDefault, session.pool,
                    (__bridge CFDictionaryRef)session.poolAuxAttributes, &pixels) != kCVReturnSuccess ||
                !pixels)
            {
                session.dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            tagRec709(pixels);

            CVMetalTextureRef planes[2] = {nullptr, nullptr};
            if (!mapPlanes(session.textureCache, pixels, session.tenBit.load(), planes))
            {
                CVPixelBufferRelease(pixels);
                session.dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            const float targetAspect = static_cast<float>(kFrameWidth) / static_cast<float>(kFrameHeight);
            const float sourceAspect = static_cast<float>(source.width()) /
                                       static_cast<float>(std::max(source.height(), 1u));
            float scale[4] = {1.0f, 1.0f, 1.0f / static_cast<float>(kFrameWidth), 0.0f};
            if (sourceAspect > targetAspect)
            {
                scale[1] = sourceAspect / targetAspect;
            }
            else
            {
                scale[0] = targetAspect / sourceAspect;
            }

            // The processing command buffer, like the webcam: the frame is
            // already there, and a buffer of our own would need a fence.
            if (session.tenBit.load())
            {
                encodePass(commands, CVMetalTextureGetTexture(planes[0]), session.lumaPipeline, source, scale);
                encodePass(commands, CVMetalTextureGetTexture(planes[1]), session.chromaPipeline, source, scale);
            }
            else
            {
                encodePass(commands, CVMetalTextureGetTexture(planes[0]), session.bgraPipeline, source, scale);
            }

            std::shared_ptr<Session> shared = session_;
            CVMetalTextureRef        luma   = planes[0];
            CVMetalTextureRef        chroma = planes[1];
            [commands addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
                CFRelease(luma);
                if (chroma)
                {
                    CFRelease(chroma);
                }
                if (buffer.status != MTLCommandBufferStatusCompleted)
                {
                    CVPixelBufferRelease(pixels);
                    shared->dropped.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                // The pixel buffer reference moves to the queue with the frame.
                dispatch_async(shared->queue, ^{
                    shared->append(pixels, time);
                });
            }];
        }
    }

    void requestStop() override
    {
        RecorderState current = session_->state.load(std::memory_order_acquire);
        while (current == RecorderState::Starting || current == RecorderState::Recording)
        {
            if (session_->state.compare_exchange_weak(current, RecorderState::Stopping,
                                                      std::memory_order_acq_rel))
            {
                if (!session_->queue)
                {
                    // Never initialised: nothing was opened, nothing to close.
                    session_->settle(RecorderState::Stopped);
                    return;
                }
                std::shared_ptr<Session> session = session_;
                dispatch_async(session_->queue, ^{
                    session->finish(session);
                });
                return;
            }
        }
    }

    RecorderStats stats() const override
    {
        RecorderStats snapshot;
        snapshot.state     = session_->state.load(std::memory_order_acquire);
        snapshot.written   = session_->written.load(std::memory_order_relaxed);
        snapshot.dropped   = session_->dropped.load(std::memory_order_relaxed);
        snapshot.seconds   = session_->seconds.load(std::memory_order_relaxed);
        snapshot.fileBytes = session_->fileBytes.load(std::memory_order_relaxed);
        snapshot.freeBytes = session_->freeBytes.load(std::memory_order_relaxed);
        snapshot.tenBit    = session_->tenBit.load(std::memory_order_acquire);
        return snapshot;
    }

    const std::filesystem::path& path() const override { return session_->path; }

    const std::string& error() const override { return session_->error; }

private:
    void encodePass(id<MTLCommandBuffer>       commands,
                    id<MTLTexture>             target,
                    id<MTLRenderPipelineState> pipeline,
                    MetalTexture&              source,
                    const float                scale[4])
    {
        MTLRenderPassDescriptor* pass        = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture     = target;
        pass.colorAttachments[0].loadAction  = MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        [encoder setFragmentBytes:scale length:4 * sizeof(float) atIndex:0];
        [encoder setFragmentTexture:source.metal() atIndex:0];
        [encoder setFragmentSamplerState:session_->sampler atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
    }

    MetalDevice* owner_ = nullptr;

    std::shared_ptr<Session> session_ = std::make_shared<Session>();
};

} // namespace

bool programRecorderSupported()
{
    return true;
}

std::unique_ptr<ProgramRecorder> createProgramRecorder(GraphicsDevice&              device,
                                                       const std::filesystem::path& file,
                                                       std::string&                 error)
{
    auto recorder = std::make_unique<ProResRecorder>(static_cast<MetalDevice&>(device), file);
    if (!recorder->initialize(error))
    {
        // Nothing was opened; the destructor's stop settles immediately.
        return nullptr;
    }
    return recorder;
}

} // namespace atemfx
