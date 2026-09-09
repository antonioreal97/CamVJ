#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "effects/EffectParameters.h"
#include "gpu/Rhi.h"
#include "tracking/source_mapping.h"
#include "video/source_health.h"

namespace atemfx {

struct EffectContext;

// Identity of a selectable video input.
struct VideoSourceDescriptor
{
    std::string id;           // stable, usable in a preset: "camera:0x8020000005ac8600"
    std::string displayName;  // "FaceTime HD Camera"
    std::string category;     // "Internal", "Built-in", "USB", "Continuity", "SDI"
};

// Frames as capture handed them over: 8-bit BGRA, valid only for the duration
// of the call, delivered on the capture thread. The observer must copy and
// return; it must not block, allocate more than it has to, or touch the GPU.
using FrameObserver = std::function<void(const uint8_t* bgra,
                                         uint32_t       width,
                                         uint32_t       height,
                                         std::size_t    rowBytes,
                                         bool           bottomUp)>;

// A producer of frames at project resolution.
//
// This is the seam DeckLink capture will occupy in M1. The chain, the UI and
// the frame loop already talk to inputs only through this interface, so adding
// SDI means adding an implementation — not changing the pipeline.
class VideoSource
{
public:
    virtual ~VideoSource() = default;

    VideoSource(const VideoSource&)            = delete;
    VideoSource& operator=(const VideoSource&) = delete;

    virtual bool initialize(EffectContext& context, std::string& error) = 0;
    virtual void shutdown()                                             = 0;

    // Returns the frame to feed the chain, or nullptr when there is no
    // picture. A source with no signal is a normal state, not an error: a
    // camera takes a moment to open and can be unplugged mid-show.
    virtual GpuTexture* render(EffectContext& context) = 0;

    // One line for the UI: resolution and frame count, or why there is no
    // picture.
    virtual std::string status() const = 0;

    // Render-thread snapshot, refreshed by render() even without a picture.
    // GPU-generated sources have no external capture signal to time.
    virtual SourceHealth health() const { return {}; }

    // Taps the frames this source receives in system memory, for control-plane
    // consumers that cannot read the GPU. Sources with no CPU frames — the
    // test pattern is generated on the GPU — ignore it, and tracking simply
    // reports that it is seeing nothing.
    //
    // Must be called before initialize(): capture runs on its own thread from
    // that moment, and replacing the observer while frames are arriving is a
    // race.
    virtual void setFrameObserver(FrameObserver observer) { (void)observer; }

    // Identity unless the source had to be fitted or mirrored into the canvas.
    virtual SourceMapping mapping() const { return {}; }

    // Calibration geometry must reach PROGRAM at its native canvas size.
    // PROGRAM's Freeze/Black policy still applies after this chain bypass.
    virtual bool bypassEffects() const { return false; }

    const VideoSourceDescriptor& descriptor() const { return descriptor_; }

    ParameterSet&       parameters() { return parameters_; }
    const ParameterSet& parameters() const { return parameters_; }

protected:
    explicit VideoSource(VideoSourceDescriptor descriptor)
        : descriptor_(std::move(descriptor))
    {
    }

    VideoSourceDescriptor descriptor_;
    ParameterSet          parameters_;
};

} // namespace atemfx
