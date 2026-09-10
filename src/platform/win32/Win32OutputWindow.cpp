#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "core/Log.h"
#include "platform/Display.h"
#include "platform/OutputWindow.h"

// Display enumeration and the borderless output window, Win32 side.
//
// Never built or run on macOS. This mirrors src/platform/mac/MacDisplay.mm
// behaviour by behaviour; where the two differ, the macOS file is the one that
// has been exercised. See docs/VIDEO_PIPELINE.md.

namespace atemfx {

namespace {

constexpr const wchar_t* kOutputClassName = L"AtemFxOutputWindow";

std::string toUtf8(const wchar_t* wide)
{
    if (!wide)
    {
        return {};
    }

    const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
    {
        return {};
    }

    std::string result(static_cast<size_t>(length - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), length, nullptr, nullptr);
    return result;
}

struct MonitorRecord
{
    std::string id;
    std::string name;
    RECT        rect    = {};
    uint32_t    width   = 0;
    uint32_t    height  = 0;
    double      refresh = 0.0;
    bool        primary = false;
};

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
{
    auto* records = reinterpret_cast<std::vector<MonitorRecord>*>(userData);

    MONITORINFOEXW info = {};
    info.cbSize         = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
    {
        return TRUE;
    }

    MonitorRecord record;
    record.rect    = info.rcMonitor;
    record.width   = static_cast<uint32_t>(info.rcMonitor.right - info.rcMonitor.left);
    record.height  = static_cast<uint32_t>(info.rcMonitor.bottom - info.rcMonitor.top);
    record.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

    // The device name is stable while the display stays attached, which is the
    // same promise DisplayInfo::id makes.
    record.id = toUtf8(info.szDevice);

    DISPLAY_DEVICEW device = {};
    device.cb              = sizeof(device);
    if (::EnumDisplayDevicesW(info.szDevice, 0, &device, 0))
    {
        record.name = toUtf8(device.DeviceString);
    }
    if (record.name.empty())
    {
        record.name = record.id;
    }

    DEVMODEW mode = {};
    mode.dmSize   = sizeof(mode);
    if (::EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode))
    {
        // The monitor rectangle is in virtual-desktop coordinates, which the
        // window server has already scaled. The mode knows the real raster.
        if (mode.dmPelsWidth > 0 && mode.dmPelsHeight > 0)
        {
            record.width  = static_cast<uint32_t>(mode.dmPelsWidth);
            record.height = static_cast<uint32_t>(mode.dmPelsHeight);
        }
        record.refresh = static_cast<double>(mode.dmDisplayFrequency);
    }

    records->push_back(std::move(record));
    return TRUE;
}

std::vector<MonitorRecord> collectMonitors()
{
    std::vector<MonitorRecord> records;
    ::EnumDisplayMonitors(nullptr, nullptr, &collectMonitor, reinterpret_cast<LPARAM>(&records));
    return records;
}

} // namespace

std::vector<DisplayInfo> enumerateDisplays()
{
    std::vector<DisplayInfo> displays;

    for (const MonitorRecord& record : collectMonitors())
    {
        DisplayInfo info;
        info.id        = record.id;
        info.name      = record.name;
        info.width     = record.width;
        info.height    = record.height;
        info.refreshHz = record.refresh;
        info.primary   = record.primary;
        displays.push_back(std::move(info));
    }

    return displays;
}

// ---------------------------------------------------------------------------
// Win32OutputWindow
// ---------------------------------------------------------------------------

class Win32OutputWindow final : public OutputWindow
{
public:
    ~Win32OutputWindow() override { close(); }

    bool open(const std::string& displayId) override;
    void close() override;

    bool  isOpen() const override { return handle_ != nullptr; }
    void* nativeHandle() const override { return handle_; }

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }

    bool consumeCloseRequest() override
    {
        const bool requested = closeRequested_;
        closeRequested_      = false;
        return requested;
    }

private:
    static LRESULT CALLBACK windowProcThunk(HWND, UINT, WPARAM, LPARAM);

    HWND     handle_         = nullptr;
    uint32_t width_          = 0;
    uint32_t height_         = 0;
    bool     closeRequested_ = false;
};

LRESULT CALLBACK Win32OutputWindow::windowProcThunk(HWND   window,
                                                    UINT   message,
                                                    WPARAM wParam,
                                                    LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(window, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto* self = reinterpret_cast<Win32OutputWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    if (self && message == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        self->closeRequested_ = true;
        return 0;
    }

    // No WM_PAINT handling: every pixel comes from the swap chain.
    return ::DefWindowProcW(window, message, wParam, lParam);
}

bool Win32OutputWindow::open(const std::string& displayId)
{
    close();

    const std::vector<MonitorRecord> monitors = collectMonitors();

    const MonitorRecord* target = nullptr;
    for (const MonitorRecord& record : monitors)
    {
        if (record.id == displayId)
        {
            target = &record;
            break;
        }
    }

    if (!target)
    {
        ATEMFX_LOG_ERROR("Display %s is not connected", displayId.c_str());
        return false;
    }

    const HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc   = &Win32OutputWindow::windowProcThunk;
    windowClass.hInstance     = instance;
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kOutputClassName;

    if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        ATEMFX_LOG_ERROR("RegisterClassExW failed for the output window (error %lu)",
                         ::GetLastError());
        return false;
    }

    const LONG left   = target->rect.left;
    const LONG top    = target->rect.top;
    const LONG width  = target->rect.right - target->rect.left;
    const LONG height = target->rect.bottom - target->rect.top;

    // WS_POPUP: no frame, no caption, no system menu. WS_EX_TOPMOST so nothing
    // covers the feed; WS_EX_NOACTIVATE so the operator's keyboard stays with
    // the user interface when it opens.
    handle_ = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                kOutputClassName,
                                L"CamVJ Output",
                                WS_POPUP,
                                left, top, width, height,
                                nullptr, nullptr, instance, this);
    if (!handle_)
    {
        ATEMFX_LOG_ERROR("CreateWindowExW failed for the output window (error %lu)",
                         ::GetLastError());
        return false;
    }

    ::ShowWindow(handle_, SW_SHOWNA);

    RECT client = {};
    ::GetClientRect(handle_, &client);
    width_  = static_cast<uint32_t>(client.right - client.left);
    height_ = static_cast<uint32_t>(client.bottom - client.top);

    // The display driving the show must not blank behind a picture the window
    // server thinks is static.
    ::SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);

    ATEMFX_LOG_INFO("Output window open on display %s: %ux%u",
                    displayId.c_str(),
                    width_,
                    height_);
    return true;
}

void Win32OutputWindow::close()
{
    if (handle_)
    {
        ::DestroyWindow(handle_);
        handle_ = nullptr;

        ::SetThreadExecutionState(ES_CONTINUOUS);
    }

    width_          = 0;
    height_         = 0;
    closeRequested_ = false;
}

std::unique_ptr<OutputWindow> createOutputWindow()
{
    return std::make_unique<Win32OutputWindow>();
}

} // namespace atemfx
