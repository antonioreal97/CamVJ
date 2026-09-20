#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>

namespace atemfx {

class GraphicsDevice;
class GpuTexture;

enum class RecorderState { Starting, Recording, Stopping, Stopped, Failed };

struct RecorderStats
{
    RecorderState state = RecorderState::Stopped;
    uint64_t written = 0;   // frames handed to the encoder
    uint64_t dropped = 0;   // frames PROGRAM offered that did not reach the file
    double   seconds = 0.0; // media time from the first frame to the last
    uint64_t fileBytes = 0; // size on disk, sampled about once a second
    uint64_t freeBytes = 0; // free space on the recording volume, same cadence
    bool     tenBit    = false; // false means the 8-bit fallback path is in use
};

// PROGRAM to a file, separate from capture, effects, display and webcam.
// See docs/RECORDING.md.
//
// Same contract as VirtualCameraOutput: the frame loop only offers a frame.
// Opening the file, encoding and closing the file happen off the render
// thread, and an encoder or a disk that cannot keep up costs dropped frames
// in the stats — never a stalled PROGRAM.
class ProgramRecorder
{
public:
    virtual ~ProgramRecorder() = default;

    // Called after ProgramOutput, BEFORE endProcessing commits the frame.
    virtual void submit(GpuTexture& frame) = 0;

    // Finishes the file asynchronously. The recorder reports Stopped once the
    // movie is closed and playable. Destroying a recorder that has not
    // stopped finishes the file first, bounded by a timeout.
    virtual void requestStop() = 0;

    virtual RecorderStats stats() const = 0;

    virtual const std::filesystem::path& path() const = 0;

    // Read only after stats reports Failed.
    virtual const std::string& error() const = 0;
};

// CMake selects the implementation. macOS writes Apple ProRes 422 HQ through
// AVFoundation; other platforms report the feature as unsupported.
bool programRecorderSupported();
std::unique_ptr<ProgramRecorder> createProgramRecorder(GraphicsDevice&              device,
                                                       const std::filesystem::path& file,
                                                       std::string&                 error);

// --- Portable policy, unit tested -----------------------------------------

// "CamVJ 2026-09-18 21-04-33.mov". Sortable, and free of ':' so the name
// survives every filesystem a show drive is likely to be formatted with.
std::string recordingFileName(const std::tm& localTime);

// A path in `directory` that does not exist yet: the timestamped name, or the
// same name with " 2", " 3"... when two takes start in the same second. A
// recording must never replace an earlier one.
std::filesystem::path uniqueRecordingPath(const std::filesystem::path& directory,
                                          const std::tm&               localTime);

// "1:02:03" or "02:03" — what the panel shows beside the REC light.
std::string formatRecordingTime(double seconds);

// Minutes of recording the free space still holds at the rate observed so
// far. Negative when there is not enough history to say.
double recordingMinutesLeft(uint64_t freeBytes, uint64_t fileBytes, double seconds);

// Below this, a take is refused before it starts: ProRes 422 HQ at 1080p60
// fills it in well under a minute, which is not a recording worth starting.
constexpr uint64_t kRecordingMinimumFreeBytes = 2ull * 1024ull * 1024ull * 1024ull;

} // namespace atemfx
