// No PROGRAM recording on this platform yet.
//
// The Windows path is Media Foundation (IMFSinkWriter) fed from a D3D11
// readback or a shared texture. It is not written, so the UI reports the
// feature as unsupported instead of offering a control that cannot work.
// See docs/RECORDING.md.

#include "video/program_recorder.h"

namespace atemfx {

bool programRecorderSupported()
{
    return false;
}

std::unique_ptr<ProgramRecorder> createProgramRecorder(GraphicsDevice&              device,
                                                       const std::filesystem::path& file,
                                                       std::string&                 error)
{
    (void)device;
    (void)file;
    error = "Recording is macOS only in this build";
    return nullptr;
}

} // namespace atemfx
