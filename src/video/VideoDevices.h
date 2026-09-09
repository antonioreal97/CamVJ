#pragma once

#include <memory>
#include <string>
#include <vector>

#include "video/VideoSource.h"

namespace atemfx {

inline constexpr const char* kTestPatternSourceId  = "test_pattern";
inline constexpr const char* kCameraSourceIdPrefix = "camera:";

// Every input the machine currently offers: the internal test pattern first,
// then each camera the operating system reports — the built-in webcam, USB
// cameras, and on macOS 14+ an iPhone acting as a Continuity Camera — followed
// by capture-capable DeckLink devices in Windows SDK builds.
//
// Safe to call repeatedly. Enumeration never opens a device, so it never
// triggers a camera permission prompt; only selecting a camera does that.
std::vector<VideoSourceDescriptor> enumerateVideoSources();

// True when a camera appeared or disappeared since the last poll. Checked
// between frames so a USB camera plugged in while the app is running shows
// up without a restart. Never opens a device.
bool consumeVideoDeviceHotplug();

// Creates the source named by `descriptor`. Returns nullptr for an unknown id.
std::unique_ptr<VideoSource> createVideoSource(const VideoSourceDescriptor& descriptor);

// Looks up a descriptor by id in a list from enumerateVideoSources.
const VideoSourceDescriptor* findVideoSource(const std::vector<VideoSourceDescriptor>& sources,
                                             const std::string&                        id);

} // namespace atemfx
