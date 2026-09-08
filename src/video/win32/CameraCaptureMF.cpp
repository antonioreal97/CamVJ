#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "core/Log.h"
#include "video/CameraCapture.h"
#include "video/VideoDevices.h"

// Media Foundation camera capture: the built-in webcam and any USB camera the
// system exposes as a video capture device.
//
// Unlike macOS there is no permission prompt to wait on — Windows either lets
// the device open or fails with E_ACCESSDENIED — so poll() has nothing to do.
// Frames are pulled by a dedicated reader thread; ReadSample is synchronous
// and must never run on the frame loop.

namespace atemfx {

namespace {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// Media Foundation is started once and left running for the life of the
// process. Shutting it down while another source is opening would be a race
// for no benefit.
void ensureMediaFoundation()
{
    static std::once_flag once;
    std::call_once(once, [] {
        if (FAILED(::MFStartup(MF_VERSION, MFSTARTUP_FULL)))
        {
            ATEMFX_LOG_ERROR("MFStartup failed; camera capture unavailable");
        }
    });
}

std::string toUtf8(const wchar_t* wide, UINT32 length)
{
    if (!wide || length == 0)
    {
        return {};
    }

    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length),
                                            nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length),
                          result.data(), bytes, nullptr, nullptr);
    return result;
}

std::string readStringAttribute(IMFActivate* device, const GUID& key)
{
    wchar_t* value  = nullptr;
    UINT32   length = 0;
    if (FAILED(device->GetAllocatedString(key, &value, &length)))
    {
        return {};
    }

    std::string result = toUtf8(value, length);
    ::CoTaskMemFree(value);
    return result;
}

ComPtr<IMFAttributes> videoCaptureAttributes()
{
    ComPtr<IMFAttributes> attributes;
    if (FAILED(::MFCreateAttributes(attributes.GetAddressOf(), 1)))
    {
        return nullptr;
    }

    if (FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
    {
        return nullptr;
    }

    return attributes;
}

} // namespace

class CameraCaptureMF final : public CameraCapture
{
public:
    ~CameraCaptureMF() override { stop(); }

    bool start(const std::string& deviceId, CameraFrameCallback onFrame, std::string& error) override;
    void stop() override;

    // Nothing is deferred on Windows: opening either succeeds or fails.
    void poll() override {}

    std::string status() const override;

private:
    void readLoop();
    void setStatus(const std::string& text);

    ComPtr<IMFSourceReader> reader_;
    std::thread             thread_;
    std::atomic<bool>       running_{false};

    CameraFrameCallback onFrame_;

    uint32_t frameWidth_  = 0;
    uint32_t frameHeight_ = 0;
    LONG     stride_      = 0;

    mutable std::mutex statusMutex_;
    std::string        status_;
};

void CameraCaptureMF::setStatus(const std::string& text)
{
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = text;
}

std::string CameraCaptureMF::status() const
{
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

bool CameraCaptureMF::start(const std::string& deviceId, CameraFrameCallback onFrame, std::string& error)
{
    ensureMediaFoundation();

    onFrame_ = std::move(onFrame);

    ComPtr<IMFAttributes> attributes = videoCaptureAttributes();
    if (!attributes)
    {
        error = "could not create the capture device query";
        return false;
    }

    IMFActivate** devices = nullptr;
    UINT32        count   = 0;
    if (FAILED(::MFEnumDeviceSources(attributes.Get(), &devices, &count)))
    {
        error = "could not enumerate capture devices";
        return false;
    }

    ComPtr<IMFMediaSource> source;
    for (UINT32 i = 0; i < count; ++i)
    {
        const std::string symbolicLink =
            readStringAttribute(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);

        if (symbolicLink == deviceId && !source)
        {
            const HRESULT hr = devices[i]->ActivateObject(IID_PPV_ARGS(source.GetAddressOf()));
            if (FAILED(hr))
            {
                error = hr == E_ACCESSDENIED
                            ? "camera access denied — Settings > Privacy & security > Camera"
                            : "could not open the camera";
            }
        }
        devices[i]->Release();
    }
    ::CoTaskMemFree(devices);

    if (!source)
    {
        if (error.empty())
        {
            error = "camera not found (unplugged?)";
        }
        setStatus(error);
        return false;
    }

    ComPtr<IMFAttributes> readerAttributes;
    if (FAILED(::MFCreateAttributes(readerAttributes.GetAddressOf(), 1)))
    {
        error = "could not configure the source reader";
        return false;
    }
    // Lets the reader insert a converter so we can ask for RGB32 whatever the
    // camera natively produces.
    readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    if (FAILED(::MFCreateSourceReaderFromMediaSource(source.Get(), readerAttributes.Get(),
                                                     reader_.GetAddressOf())))
    {
        error = "could not create the source reader";
        setStatus(error);
        return false;
    }

    ComPtr<IMFMediaType> mediaType;
    if (FAILED(::MFCreateMediaType(mediaType.GetAddressOf())) ||
        FAILED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(reader_->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, mediaType.Get())))
    {
        error = "camera does not support RGB32 output";
        setStatus(error);
        return false;
    }

    ComPtr<IMFMediaType> actualType;
    if (SUCCEEDED(reader_->GetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), actualType.GetAddressOf())))
    {
        UINT32 width  = 0;
        UINT32 height = 0;
        if (SUCCEEDED(::MFGetAttributeSize(actualType.Get(), MF_MT_FRAME_SIZE, &width, &height)))
        {
            frameWidth_  = width;
            frameHeight_ = height;
        }

        // A negative default stride means the frames arrive bottom-up, which
        // the blit shader corrects for free.
        INT32 stride = 0;
        if (SUCCEEDED(actualType->GetUINT32(MF_MT_DEFAULT_STRIDE,
                                            reinterpret_cast<UINT32*>(&stride))))
        {
            stride_ = stride;
        }
        else
        {
            stride_ = static_cast<LONG>(frameWidth_) * 4;
        }
    }

    if (frameWidth_ == 0 || frameHeight_ == 0)
    {
        error = "camera reported no frame size";
        setStatus(error);
        return false;
    }

    setStatus("opening camera");
    running_ = true;
    thread_  = std::thread([this] { readLoop(); });

    return true;
}

void CameraCaptureMF::readLoop()
{
    // The reader is used only from this thread.
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    bool firstFrame = true;

    while (running_.load())
    {
        DWORD              streamFlags = 0;
        ComPtr<IMFSample>  sample;
        LONGLONG           timestamp = 0;

        const HRESULT hr = reader_->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr,
            &streamFlags, &timestamp, sample.GetAddressOf());

        if (FAILED(hr))
        {
            setStatus("camera read failed");
            break;
        }

        if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            setStatus("camera disconnected");
            break;
        }

        if (!sample)
        {
            continue;  // a timeout, not an error
        }

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf())))
        {
            continue;
        }

        BYTE*  data      = nullptr;
        DWORD  maxLength = 0;
        DWORD  length    = 0;
        if (SUCCEEDED(buffer->Lock(&data, &maxLength, &length)) && data)
        {
            const LONG absoluteStride = stride_ < 0 ? -stride_ : stride_;

            CameraFrame frame;
            frame.width    = frameWidth_;
            frame.height   = frameHeight_;
            frame.rowBytes = static_cast<std::size_t>(absoluteStride);
            frame.bottomUp = stride_ < 0;
            frame.pixels   = data;

            if (onFrame_)
            {
                onFrame_(frame);
            }

            if (firstFrame)
            {
                firstFrame = false;
                setStatus("");
            }

            buffer->Unlock();
        }
    }

    ::CoUninitialize();
}

void CameraCaptureMF::stop()
{
    running_ = false;

    if (thread_.joinable())
    {
        thread_.join();
    }

    reader_.Reset();
    onFrame_ = nullptr;
}

std::vector<VideoSourceDescriptor> enumerateCameras()
{
    ensureMediaFoundation();

    std::vector<VideoSourceDescriptor> cameras;

    ComPtr<IMFAttributes> attributes = videoCaptureAttributes();
    if (!attributes)
    {
        return cameras;
    }

    IMFActivate** devices = nullptr;
    UINT32        count   = 0;
    if (FAILED(::MFEnumDeviceSources(attributes.Get(), &devices, &count)))
    {
        return cameras;
    }

    for (UINT32 i = 0; i < count; ++i)
    {
        const std::string symbolicLink =
            readStringAttribute(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
        const std::string friendlyName =
            readStringAttribute(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_FRIENDLY_NAME);

        if (!symbolicLink.empty())
        {
            VideoSourceDescriptor descriptor;
            descriptor.id          = std::string(kCameraSourceIdPrefix) + symbolicLink;
            descriptor.displayName = friendlyName.empty() ? "Camera" : friendlyName;
            // Media Foundation does not say what a device is attached by, and
            // guessing from the symbolic link is not worth the false labels.
            descriptor.category = "Camera";
            cameras.push_back(std::move(descriptor));
        }

        devices[i]->Release();
    }

    ::CoTaskMemFree(devices);
    return cameras;
}

std::unique_ptr<CameraCapture> createCameraCapture()
{
    return std::make_unique<CameraCaptureMF>();
}

bool consumeCameraHotplug()
{
    return false;
}

} // namespace atemfx
