#include "video/virtual_camera.h"

#include <cstdio>
#include <string>

namespace {

int checks = 0;
int failures = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

} // namespace

int main()
{
    using atemfx::VirtualCameraState;
    using atemfx::virtualCameraTallyLabel;

    expect(std::string(virtualCameraTallyLabel(VirtualCameraState::Starting, false)) == "START",
           "Starting tallies START");
    expect(std::string(virtualCameraTallyLabel(VirtualCameraState::Sending, false)) == "LIVE",
           "Sending tallies LIVE");
    expect(std::string(virtualCameraTallyLabel(VirtualCameraState::Stopping, false)) == "STOP",
           "Stopping tallies STOP");
    expect(std::string(virtualCameraTallyLabel(VirtualCameraState::Failed, false)) == "FAIL",
           "Failed tallies FAIL");
    expect(std::string(virtualCameraTallyLabel(VirtualCameraState::Stopped, false)) == "OFF",
           "clean Stopped tallies OFF");
    expect(std::string(virtualCameraTallyLabel(VirtualCameraState::Stopped, true)) == "FAIL",
           "Stopped after a sticky fault tallies FAIL");
    expect(std::string(virtualCameraTallyLabel(VirtualCameraState::Sending, true)) == "LIVE",
           "a live send wins over a stale fault flag");

    std::fprintf(stdout, "virtual camera status: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
