#include "tracking/FaceSensor.h"

#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Log.h"
#include "tracking/face_tracks.h"

namespace atemfx {

namespace {

using Clock = std::chrono::steady_clock;

// Fifteen looks a second: every second frame of a 30 fps camera. Tiles follow
// the live face between detections because they sample the camera picture,
// not the detector, so a faster detector would buy little and cost CPU the
// video path may need.
constexpr double kDetectionInterval = 1.0 / 15.0;

// Silence longer than this means the worker stalled; publish an empty crowd
// rather than faces pinned where people used to be.
constexpr double kStaleAfterSeconds = 0.75;

// Below this Vision is guessing. The effect has its own, higher threshold for
// the operator to raise; this one only keeps noise out of the track table.
constexpr float kMinDetectorConfidence = 0.30f;

class VisionFaceSensor final : public FaceSensor
{
public:
    ~VisionFaceSensor() override { stop(); }

    bool start(std::string& error) override;
    void stop() override;
    void setActive(bool active) override;
    void reset() override;

    void submit(const uint8_t* bgra,
                uint32_t       width,
                uint32_t       height,
                std::size_t    rowBytes,
                bool           bottomUp) override;

    bool latest(FacesSnapshot& snapshot) const override;

    std::string status() const override;

private:
    void workerLoop();
    void analyze();

    std::thread             worker_;
    std::atomic<bool>       running_{false};
    std::atomic<bool>       active_{false};
    std::atomic<bool>       hungry_{false};
    std::atomic<bool>       resetRequested_{false};
    std::condition_variable frameArrived_;

    mutable std::mutex   frameMutex_;
    std::vector<uint8_t> pending_;
    uint32_t             pendingWidth_    = 0;
    uint32_t             pendingHeight_   = 0;
    std::size_t          pendingRowBytes_ = 0;
    bool                 pendingBottomUp_ = false;
    bool                 hasPending_      = false;

    // Worker-thread copy, swapped with pending_ so neither side allocates once
    // the frame size settles.
    std::vector<uint8_t> working_;
    uint32_t             workingWidth_    = 0;
    uint32_t             workingHeight_   = 0;
    std::size_t          workingRowBytes_ = 0;
    bool                 workingBottomUp_ = false;

    // Worker thread only.
    FaceTrackManager  tracks_;
    FaceTrackSettings trackSettings_;
    Clock::time_point epoch_ = Clock::now();

    mutable std::mutex resultMutex_;
    FacesSnapshot      result_;
    Clock::time_point  resultTime_;
    bool               hasResult_ = false;
    uint64_t           cycles_    = 0;
    std::string        failure_;

    VNDetectFaceRectanglesRequest* request_ = nil;
};

bool VisionFaceSensor::start(std::string& error)
{
    if (running_.load(std::memory_order_acquire))
    {
        return true;
    }

    request_ = [[VNDetectFaceRectanglesRequest alloc] init];
    if (!request_)
    {
        error = "Could not create the Vision face request";
        return false;
    }

    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { workerLoop(); });

    ATEMFX_LOG_INFO("Face sensor: Vision, %.0f detections per second while an effect asks",
                    1.0 / kDetectionInterval);
    return true;
}

void VisionFaceSensor::stop()
{
    if (!running_.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    frameArrived_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
    request_ = nil;
}

void VisionFaceSensor::setActive(bool active)
{
    const bool was = active_.exchange(active, std::memory_order_acq_rel);
    if (was == active)
    {
        return;
    }

    if (active)
    {
        hungry_.store(true, std::memory_order_release);
        ATEMFX_LOG_INFO("Face sensor: active");
    }
    else
    {
        hungry_.store(false, std::memory_order_release);
        resetRequested_.store(true, std::memory_order_release);
        ATEMFX_LOG_INFO("Face sensor: idle");
    }
}

void VisionFaceSensor::reset()
{
    resetRequested_.store(true, std::memory_order_release);

    std::lock_guard<std::mutex> lock(resultMutex_);
    result_.count = 0;
}

void VisionFaceSensor::submit(const uint8_t* bgra,
                              uint32_t       width,
                              uint32_t       height,
                              std::size_t    rowBytes,
                              bool           bottomUp)
{
    // Most frames return here: idle, or the worker is still busy with the last
    // one. Only a hungry worker costs the capture thread a copy.
    if (!bgra || width == 0 || height == 0 || !active_.load(std::memory_order_acquire) ||
        !hungry_.load(std::memory_order_acquire))
    {
        return;
    }

    const std::size_t bytes = rowBytes * height;

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        pending_.resize(bytes);
        std::memcpy(pending_.data(), bgra, bytes);

        pendingWidth_    = width;
        pendingHeight_   = height;
        pendingRowBytes_ = rowBytes;
        pendingBottomUp_ = bottomUp;
        hasPending_      = true;
    }

    hungry_.store(false, std::memory_order_release);
    frameArrived_.notify_one();
}

bool VisionFaceSensor::latest(FacesSnapshot& snapshot) const
{
    std::lock_guard<std::mutex> lock(resultMutex_);

    if (!active_.load(std::memory_order_acquire) || !hasResult_)
    {
        return false;
    }

    snapshot = result_;
    const double age = std::chrono::duration<double>(Clock::now() - resultTime_).count();
    if (age > kStaleAfterSeconds)
    {
        snapshot.count = 0;
    }
    return true;
}

std::string VisionFaceSensor::status() const
{
    std::lock_guard<std::mutex> lock(resultMutex_);

    if (!failure_.empty())
    {
        return "faces: " + failure_;
    }
    if (!active_.load(std::memory_order_acquire))
    {
        return "faces: idle";
    }
    if (!hasResult_)
    {
        return "faces: waiting for frames";
    }

    char line[64];
    std::snprintf(line, sizeof(line), "faces: %u tracked", result_.count);
    return line;
}

void VisionFaceSensor::workerLoop()
{
    while (running_.load(std::memory_order_acquire))
    {
        {
            std::unique_lock<std::mutex> lock(frameMutex_);
            frameArrived_.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return hasPending_ || !running_.load(std::memory_order_acquire);
            });

            if (!running_.load(std::memory_order_acquire))
            {
                return;
            }

            if (resetRequested_.exchange(false, std::memory_order_acq_rel))
            {
                tracks_.reset();
                hasPending_ = false;
            }

            if (!hasPending_)
            {
                if (active_.load(std::memory_order_acquire))
                {
                    hungry_.store(true, std::memory_order_release);
                }
                continue;
            }

            working_.swap(pending_);
            workingWidth_    = pendingWidth_;
            workingHeight_   = pendingHeight_;
            workingRowBytes_ = pendingRowBytes_;
            workingBottomUp_ = pendingBottomUp_;
            hasPending_      = false;
        }

        @autoreleasepool
        {
            analyze();
        }

        std::this_thread::sleep_for(std::chrono::duration<double>(kDetectionInterval));
        if (active_.load(std::memory_order_acquire))
        {
            hungry_.store(true, std::memory_order_release);
        }
    }
}

void VisionFaceSensor::analyze()
{
    CVPixelBufferRef buffer = nullptr;

    // No copy: Vision reads the worker's own frame, alive for the whole call.
    const CVReturn created = CVPixelBufferCreateWithBytes(kCFAllocatorDefault,
                                                          workingWidth_,
                                                          workingHeight_,
                                                          kCVPixelFormatType_32BGRA,
                                                          working_.data(),
                                                          workingRowBytes_,
                                                          nullptr,
                                                          nullptr,
                                                          nullptr,
                                                          &buffer);
    if (created != kCVReturnSuccess || !buffer)
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        failure_ = "could not wrap the captured frame";
        return;
    }

    // A bottom-up frame is vertically mirrored; an upside-down face is not
    // detected at all.
    const CGImagePropertyOrientation orientation =
        workingBottomUp_ ? kCGImagePropertyOrientationDownMirrored : kCGImagePropertyOrientationUp;

    VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:buffer
                                                                              orientation:orientation
                                                                                  options:@{}];
    NSError*   error = nil;
    const BOOL ok    = [handler performRequests:@[request_] error:&error];
    CVPixelBufferRelease(buffer);

    if (!ok)
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        failure_ = error.localizedDescription.UTF8String ? error.localizedDescription.UTF8String
                                                         : "detection failed";
        ++cycles_;
        return;
    }

    FaceDetection detections[kMaxFaces];
    std::size_t   count = 0;
    for (VNFaceObservation* face in request_.results)
    {
        if (count == kMaxFaces || face.confidence < kMinDetectorConfidence)
        {
            continue;
        }

        // Vision's origin is bottom left; everything else here is top left.
        const CGRect   box       = face.boundingBox;
        FaceDetection& detection = detections[count++];
        detection.width          = static_cast<float>(box.size.width);
        detection.height         = static_cast<float>(box.size.height);
        detection.centerX        = static_cast<float>(box.origin.x + box.size.width * 0.5);
        detection.centerY        = 1.0f - static_cast<float>(box.origin.y + box.size.height * 0.5);
        detection.confidence     = face.confidence;
    }

    const double now = std::chrono::duration<double>(Clock::now() - epoch_).count();
    tracks_.update(detections, count, now, trackSettings_);

    FacesSnapshot published;
    published.available = true;
    tracks_.snapshot(published);

    std::lock_guard<std::mutex> lock(resultMutex_);
    result_     = published;
    resultTime_ = Clock::now();
    hasResult_  = true;
    failure_.clear();
    ++cycles_;
}

} // namespace

std::unique_ptr<FaceSensor> createFaceSensor()
{
    return std::make_unique<VisionFaceSensor>();
}

} // namespace atemfx
