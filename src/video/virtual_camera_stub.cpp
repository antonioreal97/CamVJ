// No webcam output on this platform.
//
// Windows has no camera extension equivalent: a virtual camera there is a
// DirectShow filter or a Media Foundation Virtual Camera, both of which need
// registration by an installer rather than a client connection at runtime.
// It is not written yet, so the UI reports the feature as unsupported instead
// of offering a control that cannot work. See docs/VIRTUAL_CAMERA.md.

#include "video/virtual_camera.h"

namespace atemfx {

bool virtualCameraSupported()
{
    return false;
}

std::unique_ptr<VirtualCameraOutput> createVirtualCameraOutput(GraphicsDevice& device,
                                                               std::string&    error)
{
    (void)device;
    error = "Webcam output is macOS only in this build";
    return nullptr;
}

} // namespace atemfx
