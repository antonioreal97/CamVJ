#include "tracking/Tracker.h"

#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Log.h"

namespace atemfx {

namespace {

using Clock = std::chrono::steady_clock;

// Detection rate. Framing moves over hundreds of milliseconds, so twelve
// looks at the picture per second is plenty and leaves the machine to the
// video path. Raising it buys nothing the smoothing does not already give.
constexpr double kDetectionInterval = 1.0 / 12.0;

// After this long with nothing published the tracker is treated as silent,
// whatever it said last. Covers a stalled worker, not an ordinary dropout —
// a cycle that finds nobody publishes that fact immediately.
constexpr double kStaleAfterSeconds = 0.75;

// A face box turned into something the framing can use. A face is a fraction
// of the subject a camera operator would frame, so it is expanded to roughly
// head and shoulders. This keeps the zoom from jumping when detection falls
// back from body to face; it is an estimate, not a measurement.
constexpr float kFaceToBodyWidth  = 2.5f;
constexpr float kFaceToBodyHeight = 3.0f;
constexpr float kFaceToBodyDrop   = 0.9f;  // in face heights, downward

struct Candidate
{
    float centerX    = 0.5f;
    float centerY    = 0.5f;
    float width      = 0.0f;
    float height     = 0.0f;
    float confidence = 0.0f;
};

// Vision reports normalized rectangles with the origin at the bottom left.
// Everything else in this engine uses the top left.
Candidate fromObservation(VNDetectedObjectObservation* observation)
{
    const CGRect box = observation.boundingBox;

    Candidate candidate;
    candidate.width      = static_cast<float>(box.size.width);
    candidate.height     = static_cast<float>(box.size.height);
    candidate.centerX    = static_cast<float>(box.origin.x + box.size.width * 0.5);
    candidate.centerY    = 1.0f - static_cast<float>(box.origin.y + box.size.height * 0.5);
    candidate.confidence = observation.confidence;
    return candidate;
}

class VisionTracker final : public Tracker
{
public:
    ~VisionTracker() override { stop(); }

    bool start(std::string& error) override;
    void stop() override;

    void submit(const uint8_t* bgra,
                uint32_t       width,
                uint32_t       height,
                std::size_t    rowBytes,
                bool           bottomUp) override;

    bool latest(TrackingSnapshot& snapshot) const override;

    std::string status() const override;

private:
    void workerLoop();
    void analyze();
    void publish(const Candidate* candidate);

    // Picks the same subject as last time when it can. On a stage with three
    // people, choosing "the biggest box" every cycle makes the frame jump
    // between them; continuity matters more than picking the best detection.
    const Candidate* chooseSubject(const std::vector<Candidate>& candidates) const;

    std::thread             worker_;
    std::atomic<bool>       running_{false};
    std::atomic<bool>       hungry_{false};
    std::condition_variable frameArrived_;

    mutable std::mutex   frameMutex_;
    std::vector<uint8_t> pending_;
    uint32_t             pendingWidth_    = 0;
    uint32_t             pendingHeight_   = 0;
    std::size_t          pendingRowBytes_ = 0;
    bool                 pendingBottomUp_ = false;
    bool                 hasPending_      = false;
    uint64_t             framesSubmitted_ = 0;

    // Worker-thread copy. Swapped with pending_, so neither side allocates
    // once the frame size settles.
    std::vector<uint8_t> working_;
    uint32_t             workingWidth_    = 0;
    uint32_t             workingHeight_   = 0;
    std::size_t          workingRowBytes_ = 0;
    bool                 workingBottomUp_ = false;

    mutable std::mutex resultMutex_;
    TrackingSnapshot           result_;
    Clock::time_point          resultTime_;
    bool                       hasResult_ = false;
    uint64_t                   detections_ = 0;
    uint64_t                   cycles_     = 0;
    std::string                failure_;

    VNDetectHumanRectanglesRequest* humanRequest_ = nil;
    VNDetectFaceRectanglesRequest*  faceRequest_  = nil;
};

bool VisionTracker::start(std::string& error)
{
    if (running_.load(std::memory_order_acquire))
    {
        return true;
    }

    humanRequest_ = [[VNDetectHumanRectanglesRequest alloc] init];
    if (@available(macOS 12.0, *))
    {
        // Defaults to upper body only on macOS 12+. Framing wants the whole
        // person: headroom is measured from the top of the subject, and an
        // upper-body box would put it in the middle of their chest.
        humanRequest_.upperBodyOnly = NO;
    }

    faceRequest_ = [[VNDetectFaceRectanglesRequest alloc] init];

    if (!humanRequest_ || !faceRequest_)
    {
        error         = "Could not create the Vision requests";
        humanRequest_ = nil;
        faceRequest_  = nil;
        return false;
    }

    running_.store(true, std::memory_order_release);
    hungry_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { workerLoop(); });

    ATEMFX_LOG_INFO("Subject tracking: Vision, %.0f detections per second", 1.0 / kDetectionInterval);
    return true;
}

void VisionTracker::stop()
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

    humanRequest_ = nil;
    faceRequest_  = nil;
}

void VisionTracker::submit(const uint8_t* bgra,
                           uint32_t       width,
                           uint32_t       height,
                           std::size_t    rowBytes,
                           bool           bottomUp)
{
    // The capture thread pays nothing for a frame the detector is not ready
    // for, which is most of them: at 59.94 fps and twelve detections per
    // second this copies one frame in five and returns immediately for the
    // rest. Copying every frame would cost 500 MB/s of memory bandwidth on
    // the one thread that must never be late.
    if (!bgra || width == 0 || height == 0 || !hungry_.load(std::memory_order_acquire))
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
        ++framesSubmitted_;
    }

    hungry_.store(false, std::memory_order_release);
    frameArrived_.notify_one();
}

void VisionTracker::workerLoop()
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

            if (!hasPending_)
            {
                hungry_.store(true, std::memory_order_release);
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

        // Pace the detector rather than the capture thread: sleeping here
        // leaves submit() free to return without copying anything.
        std::this_thread::sleep_for(std::chrono::duration<double>(kDetectionInterval));
        hungry_.store(true, std::memory_order_release);
    }
}

void VisionTracker::analyze()
{
    CVPixelBufferRef buffer = nullptr;

    // No copy: Vision reads straight out of the worker's own frame, which
    // stays alive for the whole call.
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

    // A bottom-up frame is vertically mirrored. Telling Vision so is not
    // cosmetic: an upside-down person is not detected at all.
    const CGImagePropertyOrientation orientation =
        workingBottomUp_ ? kCGImagePropertyOrientationDownMirrored : kCGImagePropertyOrientationUp;

    VNImageRequestHandler* handler =
        [[VNImageRequestHandler alloc] initWithCVPixelBuffer:buffer
                                                 orientation:orientation
                                                     options:@{}];

    NSError* error = nil;
    const BOOL ok  = [handler performRequests:@[humanRequest_, faceRequest_] error:&error];

    CVPixelBufferRelease(buffer);

    if (!ok)
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        failure_ = error.localizedDescription.UTF8String ? error.localizedDescription.UTF8String
                                                         : "detection failed";
        ++cycles_;
        return;
    }

    std::vector<Candidate> candidates;
    candidates.reserve(8);

    for (VNObservation* observation in humanRequest_.results)
    {
        if ([observation isKindOfClass:[VNDetectedObjectObservation class]])
        {
            candidates.push_back(fromObservation((VNDetectedObjectObservation*)observation));
        }
    }

    // Faces only when no body was found: a seated presenter, a close-up, or a
    // shot too tight for the body detector to have anything to work with.
    if (candidates.empty())
    {
        for (VNFaceObservation* face in faceRequest_.results)
        {
            Candidate candidate = fromObservation(face);
            candidate.centerY += candidate.height * kFaceToBodyDrop;
            candidate.width *= kFaceToBodyWidth;
            candidate.height *= kFaceToBodyHeight;
            candidates.push_back(candidate);
        }
    }

    publish(chooseSubject(candidates));
}

const Candidate* VisionTracker::chooseSubject(const std::vector<Candidate>& candidates) const
{
    if (candidates.empty())
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(resultMutex_);

    const Candidate* best  = nullptr;
    float            score = -1.0e9f;

    for (const Candidate& candidate : candidates)
    {
        // Area keeps the framing on whoever is closest to the camera when
        // there is nobody to stay with; distance keeps it on the same person
        // once there is.
        float value = candidate.confidence + candidate.width * candidate.height;

        if (hasResult_ && result_.valid)
        {
            const float dx = candidate.centerX - result_.centerX;
            const float dy = candidate.centerY - result_.centerY;
            value -= 2.0f * std::sqrt(dx * dx + dy * dy);
        }

        if (value > score)
        {
            score = value;
            best  = &candidate;
        }
    }

    return best;
}

void VisionTracker::publish(const Candidate* candidate)
{
    std::lock_guard<std::mutex> lock(resultMutex_);

    result_.available = true;
    result_.valid     = candidate != nullptr;

    if (candidate)
    {
        result_.centerX    = std::clamp(candidate->centerX, 0.0f, 1.0f);
        result_.centerY    = std::clamp(candidate->centerY, 0.0f, 1.0f);
        result_.width      = std::clamp(candidate->width, 0.0f, 1.0f);
        result_.height     = std::clamp(candidate->height, 0.0f, 1.0f);
        result_.confidence = std::clamp(candidate->confidence, 0.0f, 1.0f);
        ++detections_;
    }
    else
    {
        result_.confidence = 0.0f;
    }

    resultTime_ = Clock::now();
    hasResult_  = true;
    failure_.clear();
    ++cycles_;
}

bool VisionTracker::latest(TrackingSnapshot& snapshot) const
{
    std::lock_guard<std::mutex> lock(resultMutex_);

    if (!hasResult_)
    {
        return false;
    }

    snapshot = result_;

    const double age = std::chrono::duration<double>(Clock::now() - resultTime_).count();
    if (age > kStaleAfterSeconds)
    {
        snapshot.valid = false;
    }

    return true;
}

std::string VisionTracker::status() const
{
    std::lock_guard<std::mutex> frames(frameMutex_);
    std::lock_guard<std::mutex> results(resultMutex_);

    if (!failure_.empty())
    {
        return failure_;
    }

    if (framesSubmitted_ == 0)
    {
        return "no frames from this input";
    }

    if (cycles_ == 0)
    {
        return "starting";
    }

    if (!result_.valid)
    {
        return "no subject  ·  " + std::to_string(detections_) + " seen";
    }

    return "subject " + std::to_string(static_cast<int>(result_.confidence * 100.0f)) + "%  ·  " +
           std::to_string(detections_) + " seen";
}

} // namespace

std::unique_ptr<Tracker> createSubjectTracker()
{
    return std::make_unique<VisionTracker>();
}

} // namespace atemfx
