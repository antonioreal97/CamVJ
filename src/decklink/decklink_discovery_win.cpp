#include "decklink/decklink_discovery.h"

#include <climits>
#include <cstdint>
#include <string>

#include <windows.h>
#include <oleauto.h>
#include <wrl/client.h>

#include "DeckLinkAPI.h"
#include "DeckLinkAPIVersion.h"
#include "core/Log.h"

namespace atemfx {
namespace {

using Microsoft::WRL::ComPtr;

class ComApartment
{
public:
    ComApartment() : result_(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment()
    {
        // S_FALSE also adds a reference to the calling thread's apartment.
        if (SUCCEEDED(result_))
            ::CoUninitialize();
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    HRESULT result() const { return result_; }

private:
    HRESULT result_;
};

class OwnedBstr
{
public:
    OwnedBstr() = default;
    ~OwnedBstr() { ::SysFreeString(value_); }
    OwnedBstr(const OwnedBstr&) = delete;
    OwnedBstr& operator=(const OwnedBstr&) = delete;

    BSTR* address() { return &value_; }

    std::string text() const
    {
        const UINT length = ::SysStringLen(value_);
        if (length == 0 || length > static_cast<UINT>(INT_MAX))
            return "<unavailable>";

        const int bytes = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            value_, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        if (bytes == 0)
            return "<invalid UTF-16>";

        std::string result(static_cast<std::size_t>(bytes), '\0');
        if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value_,
                static_cast<int>(length), result.data(), bytes, nullptr, nullptr) != bytes)
            return "<invalid UTF-16>";

        // Keep each device-supplied value on one diagnostic line.
        for (char& character : result)
        {
            if (static_cast<unsigned char>(character) < 0x20 || character == 0x7f)
                character = ' ';
        }
        return result;
    }

private:
    BSTR value_ = nullptr;
};

void logResult(const char* operation, HRESULT result)
{
    ATEMFX_LOG_WARN("DeckLink %s unavailable (HRESULT 0x%08lx).",
                    operation, static_cast<unsigned long>(result));
}

void logRuntimeVersion()
{
    ComPtr<IDeckLinkAPIInformation> information;
    HRESULT result = ::CoCreateInstance(CLSID_CDeckLinkAPIInformation, nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(information.GetAddressOf()));
    if (result != S_OK || !information)
    {
        logResult("runtime API information", result);
        return;
    }

    OwnedBstr version;
    result = information->GetString(BMDDeckLinkAPIVersion, version.address());
    if (result == S_OK)
        ATEMFX_LOG_INFO("DeckLink runtime API: %s", version.text().c_str());
    else
        logResult("runtime API version", result);
}

void logConnections(IDeckLinkProfileAttributes& attributes,
                    BMDDeckLinkAttributeID attribute, const char* direction)
{
    LONGLONG value = 0;
    const HRESULT result = attributes.GetInt(attribute, &value);
    if (result != S_OK)
    {
        logResult(direction, result);
        return;
    }

    struct Connection { uint64_t bit; const char* name; };
    static constexpr Connection connections[] = {
        {bmdVideoConnectionSDI, "SDI"},
        {bmdVideoConnectionHDMI, "HDMI"},
        {bmdVideoConnectionOpticalSDI, "Optical SDI"},
        {bmdVideoConnectionComponent, "Component"},
        {bmdVideoConnectionComposite, "Composite"},
        {bmdVideoConnectionSVideo, "S-Video"},
    };

    const auto mask = static_cast<uint64_t>(value);
    uint64_t knownBits = 0;
    std::string names;
    for (const auto& connection : connections)
    {
        knownBits |= connection.bit;
        if ((mask & connection.bit) != 0)
        {
            if (!names.empty()) names += ", ";
            names += connection.name;
        }
    }
    if ((mask & ~knownBits) != 0)
    {
        if (!names.empty()) names += ", ";
        names += "unknown connection bits";
    }
    if (names.empty()) names = "none";
    ATEMFX_LOG_INFO("  %s: %s (mask 0x%llx)", direction, names.c_str(),
                    static_cast<unsigned long long>(mask));
}

void logDevice(IDeckLink& device, unsigned int index)
{
    OwnedBstr displayName;
    OwnedBstr modelName;
    const HRESULT displayResult = device.GetDisplayName(displayName.address());
    const HRESULT modelResult = device.GetModelName(modelName.address());
    ATEMFX_LOG_INFO("DeckLink device %u: %s", index,
                    displayResult == S_OK ? displayName.text().c_str() : "<unavailable>");
    ATEMFX_LOG_INFO("  model: %s",
                    modelResult == S_OK ? modelName.text().c_str() : "<unavailable>");
    if (displayResult != S_OK) logResult("display name", displayResult);
    if (modelResult != S_OK) logResult("model name", modelResult);

    ComPtr<IDeckLinkProfileAttributes> attributes;
    const HRESULT result = device.QueryInterface(IID_PPV_ARGS(attributes.GetAddressOf()));
    if (result != S_OK || !attributes)
    {
        logResult("active profile attributes", result);
        return;
    }

    LONGLONG ioSupport = 0;
    const HRESULT ioResult = attributes->GetInt(BMDDeckLinkVideoIOSupport, &ioSupport);
    if (ioResult == S_OK)
    {
        ATEMFX_LOG_INFO("  active profile: capture=%s, playback=%s (mask 0x%llx)",
            (ioSupport & bmdDeviceSupportsCapture) != 0 ? "yes" : "no",
            (ioSupport & bmdDeviceSupportsPlayback) != 0 ? "yes" : "no",
            static_cast<unsigned long long>(ioSupport));
    }
    else
        logResult("video I/O support", ioResult);

    logConnections(*attributes.Get(), BMDDeckLinkVideoInputConnections, "video input connections");
    logConnections(*attributes.Get(), BMDDeckLinkVideoOutputConnections, "video output connections");
}

} // namespace

int runDeckLinkDiscovery()
{
    ATEMFX_LOG_INFO("DeckLink discovery (Windows, SDK %s)", BLACKMAGIC_DECKLINK_API_VERSION_STRING);
    const ComApartment apartment;
    if (FAILED(apartment.result()))
    {
        ATEMFX_LOG_ERROR("DeckLink COM initialisation failed (HRESULT 0x%08lx).",
                         static_cast<unsigned long>(apartment.result()));
        return 1;
    }

    ComPtr<IDeckLinkIterator> iterator;
    const HRESULT result = ::CoCreateInstance(CLSID_CDeckLinkIterator, nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(iterator.GetAddressOf()));
    if (result != S_OK || !iterator)
    {
        ATEMFX_LOG_ERROR("DeckLink iterator creation failed (HRESULT 0x%08lx). "
                         "Install or repair Blackmagic Desktop Video for this SDK, "
                         "then check the device in Desktop Video Setup.",
                         static_cast<unsigned long>(result));
        return 1;
    }

    logRuntimeVersion();
    ATEMFX_LOG_INFO("Connections below are supported types, not cable or signal detection. "
                    "Device indices are temporary enumeration order.");

    unsigned int count = 0;
    while (true)
    {
        ComPtr<IDeckLink> device;
        const HRESULT next = iterator->Next(device.GetAddressOf());
        if (next == S_FALSE)
            break;
        if (next != S_OK || !device)
        {
            ATEMFX_LOG_ERROR("DeckLink enumeration failed after %u device(s) "
                             "(HRESULT 0x%08lx).", count, static_cast<unsigned long>(next));
            return 1;
        }
        logDevice(*device.Get(), count++);
    }

    ATEMFX_LOG_INFO("DeckLink discovery complete: %u device(s).", count);
    if (count == 0)
        ATEMFX_LOG_INFO("No DeckLink devices found. Check hardware and Desktop Video Setup.");
    return 0;
}

} // namespace atemfx
