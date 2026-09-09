#include "video/VideoDevices.h"

#include "decklink/decklink_capture.h"
#include "video/CameraCapture.h"
#include "video/CameraSource.h"
#include "video/DeckLinkSource.h"
#include "video/TestPatternSource.h"

namespace atemfx {

std::vector<VideoSourceDescriptor> enumerateVideoSources()
{
    std::vector<VideoSourceDescriptor> sources;

    // The test pattern is always first and always present: an input list that
    // can be empty is an input list that needs special cases everywhere.
    sources.push_back({kTestPatternSourceId, "Test Pattern", "Internal"});

    const std::vector<VideoSourceDescriptor> cameras = enumerateCameras();
    sources.insert(sources.end(), cameras.begin(), cameras.end());

    const std::vector<VideoSourceDescriptor> deckLinkSources = enumerateDeckLinkCaptureSources();
    sources.insert(sources.end(), deckLinkSources.begin(), deckLinkSources.end());

    return sources;
}

bool consumeVideoDeviceHotplug()
{
    return consumeCameraHotplug();
}

std::unique_ptr<VideoSource> createVideoSource(const VideoSourceDescriptor& descriptor)
{
    if (descriptor.id == kTestPatternSourceId)
    {
        return std::make_unique<TestPatternSource>();
    }

    if (descriptor.id.rfind(kCameraSourceIdPrefix, 0) == 0)
    {
        return std::make_unique<CameraSource>(descriptor);
    }

    if (descriptor.id.rfind(kDeckLinkSourceIdPrefix, 0) == 0)
    {
        return std::make_unique<DeckLinkSource>(descriptor);
    }

    return nullptr;
}

const VideoSourceDescriptor* findVideoSource(const std::vector<VideoSourceDescriptor>& sources,
                                             const std::string&                        id)
{
    for (const VideoSourceDescriptor& source : sources)
    {
        if (source.id == id)
        {
            return &source;
        }
    }
    return nullptr;
}

} // namespace atemfx
