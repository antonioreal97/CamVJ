#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>

#include "core/Log.h"
#include "video/CameraCapture.h"
#include "video/VideoDevices.h"

// AVFoundation camera capture.
//
// Two things make this more than a thin wrapper. First, camera access is
// gated by the operating system and the answer can arrive long after the
// request, so opening a device is asynchronous by nature and the UI has to be
// told what it is waiting for. Second, nothing here may block the frame loop:
// -startRunning takes hundreds of milliseconds, so it runs on a background
// queue and the render thread picks up the result through poll().

namespace atemfx {
class CameraCaptureAVF;
}

@interface AtemFxCameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, assign) atemfx::CameraCaptureAVF* owner;
@end

namespace atemfx {

namespace {

constexpr const char* kAccessDeniedMessage =
    "camera access denied — System Settings > Privacy & Security > Camera";

std::string toStdString(NSString* text)
{
    return text ? std::string(text.UTF8String ? text.UTF8String : "") : std::string();
}

std::atomic<bool> g_cameraHotplug{false};
id                g_connectObserver    = nil;
id                g_disconnectObserver = nil;

void ensureHotplugWatch()
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        void (^mark)(NSNotification*) = ^(NSNotification* notification) {
            (void)notification;
            g_cameraHotplug.store(true, std::memory_order_relaxed);
        };
        // Retained in the statics: addObserverForName: returns an object that
        // is released if nobody keeps it, and then USB connect is silent.
        g_connectObserver = [center addObserverForName:AVCaptureDeviceWasConnectedNotification
                                                object:nil
                                                 queue:nil
                                            usingBlock:mark];
        g_disconnectObserver =
            [center addObserverForName:AVCaptureDeviceWasDisconnectedNotification
                                object:nil
                                 queue:nil
                            usingBlock:mark];
    });
}

NSArray<AVCaptureDeviceType>* captureDeviceTypes()
{
    NSMutableArray<AVCaptureDeviceType>* types = [NSMutableArray array];
    [types addObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];

    if (@available(macOS 14.0, *))
    {
        // External covers USB and Thunderbolt cameras; Continuity covers an
        // iPhone or iPad acting as a webcam.
        [types addObject:AVCaptureDeviceTypeExternal];
        [types addObject:AVCaptureDeviceTypeContinuityCamera];
    }

    // UVC cameras (Sony FX30, many webcams) still advertise this pre-macOS 14
    // type on some OS builds even when External exists. Discovery with only
    // External then omits the device the System Settings camera list shows.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [types addObject:AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop

    return types;
}

std::string categoryFor(AVCaptureDevice* device)
{
    if (@available(macOS 14.0, *))
    {
        if ([device.deviceType isEqualToString:AVCaptureDeviceTypeExternal])
        {
            return "USB";
        }
        if ([device.deviceType isEqualToString:AVCaptureDeviceTypeContinuityCamera])
        {
            return "Continuity";
        }
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if ([device.deviceType isEqualToString:AVCaptureDeviceTypeExternalUnknown])
    {
        return "USB";
    }
#pragma clang diagnostic pop

    if ([device.deviceType isEqualToString:AVCaptureDeviceTypeBuiltInWideAngleCamera])
    {
        return "Built-in";
    }

    return "Camera";
}

// Session presets lie for a lot of UVC devices: canSetSessionPreset:1080p
// returns YES on an empty session, then the camera stays in a format that
// never produces BGRA frames. Pick 1920x1080 (or the closest below it) from
// the device's own format list. 4K is rejected because the capture thread
// memcpy of BGRA would be four times a 1080p frame.
AVCaptureDeviceFormat* pickPreferredFormat(AVCaptureDevice* device)
{
    AVCaptureDeviceFormat* best      = nil;
    int64_t                bestScore = 0;
    bool                   haveBest  = false;

    for (AVCaptureDeviceFormat* format in device.formats)
    {
        const CMVideoDimensions dim =
            CMVideoFormatDescriptionGetDimensions(format.formatDescription);
        if (dim.width <= 0 || dim.height <= 0)
        {
            continue;
        }

        const int64_t pixels = static_cast<int64_t>(dim.width) * dim.height;
        const int64_t target = 1920ll * 1080ll;
        int64_t       score  = (dim.width == 1920 && dim.height == 1080)
                             ? 1'000'000'000
                             : (pixels <= target ? pixels : -pixels);

        float maxFps = 0.0f;
        for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges)
        {
            maxFps = std::max(maxFps, static_cast<float>(range.maxFrameRate));
        }
        if (maxFps >= 29.0f)
        {
            score += 1000;
        }

        if (!haveBest || score > bestScore)
        {
            haveBest  = true;
            bestScore = score;
            best      = format;
        }
    }

    return best;
}

bool applyPreferredFormat(AVCaptureDevice* device)
{
    AVCaptureDeviceFormat* format = pickPreferredFormat(device);
    if (!format)
    {
        return false;
    }

    NSError* error = nil;
    if (![device lockForConfiguration:&error])
    {
        ATEMFX_LOG_WARN("Camera format lock failed: %s",
                        toStdString(error.localizedDescription).c_str());
        return false;
    }

    device.activeFormat = format;

    AVFrameRateRange* fastest = nil;
    for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges)
    {
        if (!fastest || range.maxFrameRate > fastest.maxFrameRate)
        {
            fastest = range;
        }
    }
    if (fastest)
    {
        // Lock to the fastest rate the format actually offers. The FX30 USB
        // stream is 1080p30; a built-in camera often does 60.
        device.activeVideoMinFrameDuration = fastest.minFrameDuration;
        device.activeVideoMaxFrameDuration = fastest.minFrameDuration;
    }

    [device unlockForConfiguration];

    const CMVideoDimensions dim =
        CMVideoFormatDescriptionGetDimensions(format.formatDescription);
    ATEMFX_LOG_INFO("Camera format %dx%d", static_cast<int>(dim.width),
                    static_cast<int>(dim.height));
    return true;
}

} // namespace

class CameraCaptureAVF final : public CameraCapture
{
public:
    ~CameraCaptureAVF() override { stop(); }

    bool start(const std::string& deviceId, CameraFrameCallback onFrame, std::string& error) override;
    void stop() override;
    void poll() override;

    std::string status() const override;

    // Called on the capture queue by the delegate.
    void deliver(CMSampleBufferRef sampleBuffer);

private:
    // Shared with the permission completion handler, which can outlive this
    // object and runs on a queue of the system's choosing.
    struct PermissionState
    {
        std::atomic<bool> resolved{false};
        std::atomic<bool> granted{false};
    };

    bool openSession(std::string& error);
    void setStatus(const std::string& text);

    AVCaptureSession*         session_  = nil;
    AVCaptureVideoDataOutput* output_   = nil;
    AtemFxCameraDelegate*     delegate_ = nil;
    dispatch_queue_t          queue_    = nullptr;

    std::string         deviceId_;
    CameraFrameCallback onFrame_;

    mutable std::mutex statusMutex_;
    std::string        status_;

    std::shared_ptr<PermissionState> permission_ = std::make_shared<PermissionState>();
    bool                             permissionHandled_ = false;
    bool                             sessionOpen_       = false;
    std::atomic<bool>                firstFrameSeen_{false};
};

void CameraCaptureAVF::setStatus(const std::string& text)
{
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = text;
}

std::string CameraCaptureAVF::status() const
{
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

bool CameraCaptureAVF::start(const std::string& deviceId, CameraFrameCallback onFrame, std::string& error)
{
    deviceId_ = deviceId;
    onFrame_  = std::move(onFrame);

    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo])
    {
    case AVAuthorizationStatusAuthorized:
        return openSession(error);

    case AVAuthorizationStatusNotDetermined:
    {
        setStatus("waiting for camera permission");

        // The handler can fire after this object is gone, so it touches only
        // the shared state, never `this`. poll() picks the answer up on the
        // render thread.
        std::shared_ptr<PermissionState> permission = permission_;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
                                     permission->granted.store(granted == YES);
                                     permission->resolved.store(true);
                                 }];
        return true;  // the source is valid, it just has no picture yet
    }

    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:
    default:
        error = kAccessDeniedMessage;
        setStatus(error);
        return false;
    }
}

bool CameraCaptureAVF::openSession(std::string& error)
{
    AVCaptureDevice* device = [AVCaptureDevice deviceWithUniqueID:@(deviceId_.c_str())];
    if (!device)
    {
        error = "camera not found (unplugged?)";
        return false;
    }

    session_ = [[AVCaptureSession alloc] init];
    [session_ beginConfiguration];

    NSError*              deviceError = nil;
    AVCaptureDeviceInput* input       = [AVCaptureDeviceInput deviceInputWithDevice:device
                                                                             error:&deviceError];
    if (!input || ![session_ canAddInput:input])
    {
        error = deviceError ? toStdString(deviceError.localizedDescription)
                            : std::string("camera input rejected");
        [session_ commitConfiguration];
        session_ = nil;
        return false;
    }
    [session_ addInput:input];

    // UVC (Sony FX30 and friends) needs the device format set explicitly.
    // Presets on an empty session claim 1080p and then never deliver frames.
    if (!applyPreferredFormat(device))
    {
        if ([session_ canSetSessionPreset:AVCaptureSessionPreset1920x1080])
        {
            session_.sessionPreset = AVCaptureSessionPreset1920x1080;
        }
        else if ([session_ canSetSessionPreset:AVCaptureSessionPreset1280x720])
        {
            session_.sessionPreset = AVCaptureSessionPreset1280x720;
        }
        else
        {
            session_.sessionPreset = AVCaptureSessionPresetHigh;
        }
    }

    output_ = [[AVCaptureVideoDataOutput alloc] init];
    // BGRA so the upload is a memcpy and the conversion to the processing
    // format happens on the GPU.
    output_.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    // Newest picture wins, same policy the SDI frame queue will use in M1.
    output_.alwaysDiscardsLateVideoFrames = YES;

    if (![session_ canAddOutput:output_])
    {
        error    = "camera output rejected";
        [session_ commitConfiguration];
        session_ = nil;
        output_  = nil;
        return false;
    }

    queue_          = dispatch_queue_create("fx.atem.camera", DISPATCH_QUEUE_SERIAL);
    delegate_       = [[AtemFxCameraDelegate alloc] init];
    delegate_.owner = this;
    [output_ setSampleBufferDelegate:delegate_ queue:queue_];
    [session_ addOutput:output_];
    [session_ commitConfiguration];

    sessionOpen_ = true;
    setStatus("opening camera");

    // -startRunning blocks for hundreds of milliseconds. It never happens on
    // the frame loop.
    AVCaptureSession* session = session_;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        [session startRunning];
    });

    return true;
}

void CameraCaptureAVF::poll()
{
    if (permissionHandled_ || sessionOpen_ || !permission_->resolved.load())
    {
        return;
    }

    permissionHandled_ = true;

    if (!permission_->granted.load())
    {
        setStatus(kAccessDeniedMessage);
        return;
    }

    std::string error;
    if (!openSession(error))
    {
        setStatus(error);
    }
}

void CameraCaptureAVF::stop()
{
    if (output_)
    {
        [output_ setSampleBufferDelegate:nil queue:nil];
    }

    if (session_)
    {
        [session_ stopRunning];
    }

    // A callback can already be in flight on the capture queue. Draining the
    // serial queue is the barrier that makes it safe to tear the object down.
    if (queue_)
    {
        dispatch_sync(queue_, ^{
        });
    }

    delegate_.owner = nullptr;

    delegate_    = nil;
    output_      = nil;
    session_     = nil;
    queue_       = nullptr;
    sessionOpen_ = false;
    onFrame_     = nullptr;
}

void CameraCaptureAVF::deliver(CMSampleBufferRef sampleBuffer)
{
    if (!onFrame_)
    {
        return;
    }

    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!image)
    {
        return;
    }

    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);

    CameraFrame frame;
    frame.pixels   = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(image));
    frame.width    = static_cast<uint32_t>(CVPixelBufferGetWidth(image));
    frame.height   = static_cast<uint32_t>(CVPixelBufferGetHeight(image));
    frame.rowBytes = CVPixelBufferGetBytesPerRow(image);

    if (frame.pixels)
    {
        onFrame_(frame);

        // Clear the "opening" note once, rather than taking the status lock on
        // every frame.
        if (!firstFrameSeen_.exchange(true))
        {
            setStatus("");
        }
    }

    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
}

std::vector<VideoSourceDescriptor> enumerateCameras()
{
    ensureHotplugWatch();

    std::vector<VideoSourceDescriptor> cameras;

    @autoreleasepool
    {
        AVCaptureDeviceDiscoverySession* discovery = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:captureDeviceTypes()
                                  mediaType:AVMediaTypeVideo
                                   position:AVCaptureDevicePositionUnspecified];

        NSMutableSet<NSString*>* seen = [NSMutableSet set];
        for (AVCaptureDevice* device in discovery.devices)
        {
            if (device.uniqueID == nil || [seen containsObject:device.uniqueID])
            {
                continue;
            }
            [seen addObject:device.uniqueID];

            VideoSourceDescriptor descriptor;
            descriptor.id          = std::string(kCameraSourceIdPrefix) + toStdString(device.uniqueID);
            descriptor.displayName = toStdString(device.localizedName);
            descriptor.category    = categoryFor(device);
            cameras.push_back(std::move(descriptor));
        }
    }

    return cameras;
}

std::unique_ptr<CameraCapture> createCameraCapture()
{
    return std::make_unique<CameraCaptureAVF>();
}

bool consumeCameraHotplug()
{
    ensureHotplugWatch();
    return g_cameraHotplug.exchange(false, std::memory_order_relaxed);
}

} // namespace atemfx

@implementation AtemFxCameraDelegate

- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection
{
    (void)output;
    (void)connection;

    if (self.owner)
    {
        self.owner->deliver(sampleBuffer);
    }
}

@end
