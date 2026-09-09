#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace atemfx {

class GraphicsDevice;
class GpuTexture;

enum class VirtualCameraState { Starting, Sending, Stopping, Stopped, Failed };

struct VirtualCameraStats
{
    VirtualCameraState state = VirtualCameraState::Starting;
    uint64_t sent = 0;
    uint64_t skipped = 0;
};

// A local camera output, separate from capture, effects and display output.
// Native frame transport stays below the backend seam. See VIRTUAL_CAMERA.md.
class VirtualCameraOutput
{
public:
    virtual ~VirtualCameraOutput() = default;

    // Called after ProgramOutput, BEFORE endProcessing commits the frame.
    // Bounded, non-blocking: a slow camera consumer never stalls PROGRAM.
    virtual void submit(GpuTexture& frame) = 0;
    virtual void requestStop() = 0;
    virtual VirtualCameraStats stats() const = 0;

    // Read only after stats reports Failed (the worker has published it).
    virtual const std::string& error() const = 0;
};

// CMake selects the implementation. On macOS this uses the already installed
// OBS Camera Extension; it does not install or modify a system extension.
bool virtualCameraSupported();
std::unique_ptr<VirtualCameraOutput> createVirtualCameraOutput(
    GraphicsDevice& device, std::string& error);

} // namespace atemfx
