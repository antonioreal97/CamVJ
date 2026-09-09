#include "tracking/Tracker.h"

#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Log.h"
#include "tracking/source_mapping.h"

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

// Floor on VNTrackObjectRequest confidence. Below this the lock is lost for
// this cycle (framing holds) but the last observation is kept so Vision can
// re-acquire the same target. It is not a quality score.
constexpr float kLockConfidence = 0.2f;

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

CGRect visionBoxFromCandidate(const Candidate& candidate)
{
    const float width  = std::clamp(candidate.width, kMinLockExtent, 1.0f);
    const float height = std::clamp(candidate.height, kMinLockExtent, 1.0f);
    const float x0     = std::clamp(candidate.centerX - width * 0.5f, 0.0f, 1.0f);
    const float y0Top  = std::clamp(candidate.centerY - height * 0.5f, 0.0f, 1.0f);
    const float x1     = std::clamp(x0 + width, 0.0f, 1.0f);
    const float y1Top  = std::clamp(y0Top + height, 0.0f, 1.0f);

    CGRect box;
    box.origin.x    = x0;
    box.origin.y    = 1.0 - static_cast<double>(y1Top);
    box.size.width  = static_cast<double>(x1 - x0);
    box.size.height = static_cast<double>(y1Top - y0Top);
    return box;
}

TrackingCandidate toPublished(const Candidate& candidate)
{
    TrackingCandidate published;
    published.centerX    = candidate.centerX;
    published.centerY    = candidate.centerY;
    published.width      = candidate.width;
    published.height     = candidate.height;
    published.confidence = candidate.confidence;
    return published;
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

    void lock(float centerX, float centerY, float width, float height) override;
    void unlock() override;
    void setEnumerateCandidates(bool enable) override;
    void candidates(std::vector<TrackingCandidate>& out) const override;

    std::string status() const override;

private:
    void workerLoop();
    void analyze();
    void publish(const Candidate* candidate, bool fromOperatorLock);
    const Candidate* chooseAutomatically(const std::vector<Candidate>& people);
    bool collectPeople(VNImageRequestHandler* handler, std::vector<Candidate>& people);
    bool trackLockedObject(CVPixelBufferRef buffer, CGImagePropertyOrientation orientation,
                           Candidate& tracked);
    void applyPendingTarget();
    void beginLock(const Candidate& rect);
    void clearLock();

    std::thread             worker_;
    std::atomic<bool>       running_{false};
    std::atomic<bool>       hungry_{false};
    std::atomic<bool>       enumerateCandidates_{false};
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

    mutable std::mutex             resultMutex_;
    TrackingSnapshot               result_;
    Clock::time_point              resultTime_;
    bool                           hasResult_ = false;
    bool                           locked_    = false;
    uint64_t                       detections_ = 0;
    uint64_t                       cycles_     = 0;
    std::string                    failure_;
    std::vector<TrackingCandidate> publishedCandidates_;

    // UI thread writes, worker consumes. Never held together with resultMutex_.
    std::mutex targetMutex_;
    bool       pendingLock_   = false;
    bool       pendingUnlock_ = false;
    Candidate  pendingRect_;

    // Who the tracker chose for itself, before any operator Pick. Worker
    // thread only: the reset happens in applyPendingTarget(), which also runs
    // there, so this needs no mutex of its own.
    bool  autoChosen_  = false;
    float autoCenterX_ = 0.5f;
    float autoCenterY_ = 0.5f;

    VNDetectHumanRectanglesRequest* humanRequest_     = nil;
    VNDetectFaceRectanglesRequest*  faceRequest_      = nil;
    VNSequenceRequestHandler*       sequenceHandler_  = nil;
    VNDetectedObjectObservation*    trackObservation_ = nil;
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

    humanRequest_     = nil;
    faceRequest_      = nil;
    sequenceHandler_  = nil;
    trackObservation_ = nil;
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

void VisionTracker::lock(float centerX, float centerY, float width, float height)
{
    clampNormalizedRect(centerX, centerY, width, height);

    {
        std::lock_guard<std::mutex> lock(targetMutex_);
        pendingLock_           = true;
        pendingUnlock_         = false;
        pendingRect_.centerX   = centerX;
        pendingRect_.centerY   = centerY;
        pendingRect_.width     = width;
        pendingRect_.height    = height;
        pendingRect_.confidence = 1.0f;
    }

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        locked_            = true;
        result_.available  = true;
        result_.valid      = true;
        result_.locked     = true;
        result_.centerX    = centerX;
        result_.centerY    = centerY;
        result_.width      = width;
        result_.height     = height;
        result_.confidence = 1.0f;
        hasResult_         = true;
        resultTime_        = Clock::now();
        failure_.clear();
    }
}

void VisionTracker::unlock()
{
    {
        std::lock_guard<std::mutex> lock(targetMutex_);
        pendingUnlock_ = true;
        pendingLock_   = false;
    }

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        locked_        = false;
        result_.locked = false;
    }
}

void VisionTracker::setEnumerateCandidates(bool enable)
{
    enumerateCandidates_.store(enable, std::memory_order_release);
}

void VisionTracker::candidates(std::vector<TrackingCandidate>& out) const
{
    std::lock_guard<std::mutex> lock(resultMutex_);
    out = publishedCandidates_;
}

void VisionTracker::beginLock(const Candidate& rect)
{
    sequenceHandler_  = [[VNSequenceRequestHandler alloc] init];
    trackObservation_ = [VNDetectedObjectObservation
        observationWithBoundingBox:visionBoxFromCandidate(rect)];
}

void VisionTracker::clearLock()
{
    sequenceHandler_  = nil;
    trackObservation_ = nil;
}

void VisionTracker::applyPendingTarget()
{
    bool      doLock   = false;
    bool      doUnlock = false;
    Candidate rect;

    {
        std::lock_guard<std::mutex> lock(targetMutex_);
        doLock         = pendingLock_;
        doUnlock       = pendingUnlock_;
        rect           = pendingRect_;
        pendingLock_   = false;
        pendingUnlock_ = false;
    }

    if (doUnlock || doLock)
    {
        // A new camera, or an operator who just aimed the shot: who the
        // tracker had chosen for itself says nothing about either.
        autoChosen_ = false;
    }

    if (doUnlock)
    {
        clearLock();
        std::lock_guard<std::mutex> lock(resultMutex_);
        locked_        = false;
        result_.locked = false;
    }

    if (doLock)
    {
        beginLock(rect);
        std::lock_guard<std::mutex> lock(resultMutex_);
        locked_        = true;
        result_.locked = true;
    }
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

bool VisionTracker::collectPeople(VNImageRequestHandler* handler, std::vector<Candidate>& people)
{
    NSError* error = nil;
    const BOOL ok  = [handler performRequests:@[humanRequest_, faceRequest_] error:&error];
    if (!ok)
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        failure_ = error.localizedDescription.UTF8String ? error.localizedDescription.UTF8String
                                                         : "detection failed";
        ++cycles_;
        return false;
    }

    for (VNObservation* observation in humanRequest_.results)
    {
        if ([observation isKindOfClass:[VNDetectedObjectObservation class]])
        {
            people.push_back(fromObservation((VNDetectedObjectObservation*)observation));
        }
    }

    // Faces only when no body was found: a seated presenter, a close-up, or a
    // shot too tight for the body detector to have anything to work with.
    if (people.empty())
    {
        for (VNFaceObservation* face in faceRequest_.results)
        {
            Candidate candidate = fromObservation(face);
            candidate.centerY += candidate.height * kFaceToBodyDrop;
            candidate.width *= kFaceToBodyWidth;
            candidate.height *= kFaceToBodyHeight;
            people.push_back(candidate);
        }
    }

    return true;
}

bool VisionTracker::trackLockedObject(CVPixelBufferRef buffer, CGImagePropertyOrientation orientation,
                                      Candidate& tracked)
{
    if (!sequenceHandler_ || !trackObservation_)
    {
        return false;
    }

    VNTrackObjectRequest* request =
        [[VNTrackObjectRequest alloc] initWithDetectedObjectObservation:trackObservation_];
    request.trackingLevel = VNRequestTrackingLevelAccurate;

    NSError* error = nil;
    const BOOL ok  = [sequenceHandler_ performRequests:@[request]
                                      onCVPixelBuffer:buffer
                                          orientation:orientation
                                                error:&error];
    if (!ok)
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        failure_ = error.localizedDescription.UTF8String ? error.localizedDescription.UTF8String
                                                         : "object tracking failed";
        return false;
    }

    VNDetectedObjectObservation* observation = nil;
    for (VNObservation* result in request.results)
    {
        if ([result isKindOfClass:[VNDetectedObjectObservation class]])
        {
            observation = (VNDetectedObjectObservation*)result;
            break;
        }
    }

    if (!observation)
    {
        return false;
    }

    // Keep the last box even when confidence is low so the next cycle can
    // re-acquire the same target instead of picking someone else.
    trackObservation_ = observation;
    tracked           = fromObservation(observation);
    return tracked.confidence >= kLockConfidence;
}

void VisionTracker::analyze()
{
    applyPendingTarget();

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

    const bool enumerate = enumerateCandidates_.load(std::memory_order_acquire);
    const bool locked    = trackObservation_ != nil;

    std::vector<Candidate> people;
    bool                   peopleOk = true;
    if (!locked || enumerate)
    {
        VNImageRequestHandler* handler =
            [[VNImageRequestHandler alloc] initWithCVPixelBuffer:buffer
                                                     orientation:orientation
                                                         options:@{}];
        people.reserve(8);
        peopleOk = collectPeople(handler, people);
    }

    if (locked)
    {
        Candidate tracked;
        if (trackLockedObject(buffer, orientation, tracked))
        {
            publish(&tracked, true);
        }
        else
        {
            // Lost this cycle. Do not choose another person.
            publish(nullptr, true);
        }
    }
    else if (peopleOk)
    {
        // Auto-select until the first Pick. A camera pointed at one presenter
        // has to frame them with nobody touching the machine: waiting for an
        // operator here published `valid = false` every cycle, which left
        // FramingController with no subject and the crop standing still.
        //
        // What made auto-picking feel unaimable was that it re-chose every
        // cycle and hopped between people. So the choice is sticky: biggest
        // once, then whoever is nearest to the person already being followed.
        // A Pick overrides it for good - after one, `locked_` makes publish()
        // ignore this path entirely and nothing switches subjects again.
        publish(chooseAutomatically(people), false);
    }
    else
    {
        CVPixelBufferRelease(buffer);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        // Boxes stay visible until a lock, and again in Pick mode so the
        // operator can switch. After a lock, drop them so SOURCE is not a
        // Christmas tree of grey rectangles.
        if (!locked || enumerate)
        {
            publishedCandidates_.clear();
            publishedCandidates_.reserve(people.size());
            for (const Candidate& person : people)
            {
                publishedCandidates_.push_back(toPublished(person));
            }
        }
        else if (!publishedCandidates_.empty())
        {
            publishedCandidates_.clear();
        }
    }

    CVPixelBufferRelease(buffer);
}

const Candidate* VisionTracker::chooseAutomatically(const std::vector<Candidate>& people)
{
    if (people.empty())
    {
        // Nobody in shot. Forget who we were following, so the next person to
        // walk in is chosen on their own merits rather than by being nearest
        // to someone who left.
        autoChosen_ = false;
        return nullptr;
    }

    const Candidate* best  = nullptr;
    float            score = -1.0f;
    for (const Candidate& person : people)
    {
        // Area on the first frame - the presenter is the one filling the shot.
        // After that, distance from the subject already being followed, so a
        // second person walking through does not steal the frame.
        float candidateScore;
        if (autoChosen_)
        {
            const float dx = person.centerX - autoCenterX_;
            const float dy = person.centerY - autoCenterY_;
            candidateScore = -(dx * dx + dy * dy);
        }
        else
        {
            candidateScore = person.width * person.height;
        }

        if (candidateScore > score)
        {
            score = candidateScore;
            best  = &person;
        }
    }

    if (best)
    {
        autoChosen_  = true;
        autoCenterX_ = best->centerX;
        autoCenterY_ = best->centerY;
    }
    return best;
}

void VisionTracker::publish(const Candidate* candidate, bool fromOperatorLock)
{
    std::lock_guard<std::mutex> lock(resultMutex_);

    if (locked_ && !fromOperatorLock)
    {
        return;
    }

    result_.available = true;
    result_.valid     = candidate != nullptr;
    result_.locked    = locked_;

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

    if (locked_)
    {
        if (!result_.valid)
        {
            return "lost lock  ·  waiting";
        }
        return "locked " + std::to_string(static_cast<int>(result_.confidence * 100.0f)) + "%  ·  " +
               std::to_string(detections_) + " seen";
    }

    if (!publishedCandidates_.empty())
    {
        const std::string shot =
            std::to_string(publishedCandidates_.size()) + " in shot  ·  click SOURCE to choose";
        return result_.valid ? "following  ·  " + shot : shot;
    }

    return "waiting  ·  click SOURCE to choose";
}

} // namespace

std::unique_ptr<Tracker> createSubjectTracker()
{
    return std::make_unique<VisionTracker>();
}

} // namespace atemfx
