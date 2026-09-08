#pragma once

// The rendering interface the engine is written against.
//
// Everything above this header — effects, the chain, timing, the UI panels —
// is backend-neutral. Below it live two implementations: Direct3D 11 on
// Windows and Metal on macOS. The seam is deliberately narrow: the whole
// engine draws with exactly one primitive, a fullscreen pass from an optional
// source texture into a target texture, parameterised by a shader and a
// constant block.
//
// Keeping it this small is what makes a second backend tractable. Resist
// widening it; an effect that needs more than a fullscreen pass should say so
// and get a design, not a new RHI entry point.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gpu/EffectConstants.h"

namespace atemfx {

class Window;

enum class SamplerFilter
{
    Point,
    Linear,
};

// Opaque, backend-owned compiled shader. Never cache one across a reload.
using ShaderHandle = void*;

// A colour texture that can be both rendered into and sampled from.
class GpuTexture
{
public:
    virtual ~GpuTexture() = default;

    virtual uint32_t width() const  = 0;
    virtual uint32_t height() const = 0;
    virtual bool     valid() const  = 0;

    // For the UI preview only: an ID3D11ShaderResourceView* or an
    // id<MTLTexture>, whichever the ImGui backend in use expects.
    virtual void* nativeTexture() const = 0;
};

class ShaderLibrary
{
public:
    virtual ~ShaderLibrary() = default;

    // `name` is a base name with no extension or directory, e.g. "rgb_split".
    // Each backend resolves it to its own source file. Compiled on first use;
    // a failure is cached so the per-frame path never reaches the disk.
    virtual ShaderHandle shader(const std::string& name, std::string* error = nullptr) = 0;

    // Recompiles everything cached. A shader that fails to compile keeps its
    // previous version, so a typo degrades to "nothing changed" rather than a
    // black frame mid-show.
    virtual bool reloadAll(std::string& error) = 0;

    virtual std::string directory() const = 0;
};

class FullscreenPass
{
public:
    virtual ~FullscreenPass() = default;

    // The engine's only drawing primitive. `source` may be null for
    // generators. `history` is an optional second sample (t1) for effects
    // that persist a previous frame; it is not a second primitive. The
    // implementation must leave no texture bound as both a read and a write
    // when it returns.
    virtual void draw(GpuTexture&            target,
                      ShaderHandle           shader,
                      const GpuTexture*      source,
                      const EffectConstants& constants,
                      SamplerFilter          filter,
                      const GpuTexture*      history = nullptr) = 0;
};

// Owns every off-screen target. Two scratch targets carry the chain's
// ping-pong; persistent targets, addressed by key, survive across frames and
// are what feedback and trail effects will use. See docs/ARCHITECTURE.md.
class TargetPool
{
public:
    virtual ~TargetPool() = default;

    virtual GpuTexture& scratch(std::size_t index)           = 0;
    virtual GpuTexture& persistent(const std::string& key)   = 0;

    // A CPU-writable, GPU-sampleable 8-bit BGRA texture, for capture sources
    // that hand over frames in system memory. Reallocated when the requested
    // size changes, because a camera can be swapped for one of another size.
    // Not a render target: it is only ever a source.
    virtual GpuTexture& uploadTarget(const std::string& key, uint32_t width, uint32_t height) = 0;

    // Writes 8-bit BGRA pixels into a texture from uploadTarget. This is the
    // one place CPU pixels enter the pipeline, and it exists because capture
    // hardware delivers them that way. Everything downstream is GPU-only.
    virtual bool upload(GpuTexture& texture, const void* bgra8, std::size_t rowBytes) = 0;

    virtual uint32_t width() const  = 0;
    virtual uint32_t height() const = 0;
};

// A second presentation surface: a window on another display showing the
// processed frame and nothing else.
//
// This is the engine's program output. It is separate from the preview swap
// chain because it answers to a different clock and carries a different
// picture: the preview shows the operator what is happening, this shows the
// audience. Only the backend can build one, because only the backend knows
// what a swap chain is.
class OutputSurface
{
public:
    virtual ~OutputSurface() = default;

    // Scales `frame` into the surface and presents it. Letterboxes rather than
    // distorting: downstream is a fixed raster and a stretched picture is a
    // fault nobody can correct further down the chain.
    virtual void present(GpuTexture& frame) = 0;

    // Points, matching Window::width(); the implementation applies the
    // display's own scale.
    virtual void resize(uint32_t width, uint32_t height) = 0;

    // Pixels of the surface actually being presented.
    virtual uint32_t width() const  = 0;
    virtual uint32_t height() const = 0;
};

// One frame of the device, from acquiring an image to presenting it.
class GraphicsDevice
{
public:
    virtual ~GraphicsDevice() = default;

    // `window` may be null, which initialises the device headless: no swap
    // chain, no presentation, everything else identical. That is what the
    // --headless self-test runs on, and it is the reason the render path can
    // be verified without a display.
    virtual bool initialize(Window*  window,
                            uint32_t processingWidth,
                            uint32_t processingHeight) = 0;
    virtual void shutdown() = 0;

    // False means there is nothing to draw into this frame (minimised window,
    // no drawable available). Skip the frame; do not treat it as an error.
    virtual bool beginFrame() = 0;

    // Brackets the measured region: source generation plus the effect chain.
    virtual void beginProcessing() = 0;
    virtual void endProcessing()   = 0;

    // Prepares the presentation surface for the UI pass.
    virtual void beginUi() = 0;

    virtual void endFrame(bool vsync) = 0;

    virtual void onWindowResized(uint32_t width, uint32_t height) = 0;

    // `nativeHandle` is an OutputWindow's NSView* or HWND. Null on failure,
    // and always null on a headless device, which has no presentation at all.
    // Width and height are in points, as OutputWindow reports them.
    virtual std::unique_ptr<OutputSurface> createOutputSurface(void*    nativeHandle,
                                                               uint32_t width,
                                                               uint32_t height) = 0;

    virtual ShaderLibrary&  shaders()        = 0;
    virtual FullscreenPass& fullscreenPass() = 0;
    virtual TargetPool&     targets()        = 0;

    virtual float lastGpuMilliseconds() const = 0;
    virtual bool  hasGpuTiming() const        = 0;

    virtual const std::string& adapterName() const = 0;

    // Copies a texture back to the CPU as 8-bit RGBA, top row first. Stalls
    // the GPU: diagnostics and self-tests only, never the frame loop.
    virtual bool readback(GpuTexture& texture, std::vector<uint8_t>& rgba) = 0;
};

} // namespace atemfx
