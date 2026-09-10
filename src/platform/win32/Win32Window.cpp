#include <windows.h>

#include <atomic>
#include <memory>
#include <string>

#include "core/Log.h"
#include "gpu/Backend.h"
#include "platform/Display.h"
#include "platform/Window.h"
#include "platform/win32/Win32MessageHook.h"

namespace atemfx {

namespace {

constexpr const wchar_t* kWindowClassName = L"AtemFxMainWindow";

Win32MessageHook g_messageHook = nullptr;
std::atomic<bool> g_displayChanges{false};

std::wstring toWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }

    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                             static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                          result.data(), length);
    return result;
}

} // namespace

void setWin32MessageHook(Win32MessageHook hook)
{
    g_messageHook = hook;
}

bool consumeDisplayChanges()
{
    return g_displayChanges.exchange(false, std::memory_order_relaxed);
}

class Win32Window final : public Window
{
public:
    ~Win32Window() override { destroy(); }

    bool create(const std::string& title, uint32_t width, uint32_t height) override;
    void destroy() override;
    void runFrameLoop(const FrameCallback& onFrame) override;

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }
    bool     minimized() const override { return minimized_; }
    void*    nativeHandle() const override { return handle_; }

private:
    static LRESULT CALLBACK windowProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT                 windowProc(HWND, UINT, WPARAM, LPARAM);

    HWND     handle_    = nullptr;
    uint32_t width_     = 0;
    uint32_t height_    = 0;
    bool     minimized_ = false;
    bool     closed_    = false;
};

bool Win32Window::create(const std::string& title, uint32_t width, uint32_t height)
{
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc   = &Win32Window::windowProcThunk;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;

    // Registering twice is harmless; a genuine failure is not.
    if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        ATEMFX_LOG_ERROR("RegisterClassExW failed (error %lu)", ::GetLastError());
        return false;
    }

    RECT rect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    ::AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    const std::wstring wideTitle = toWide(title);

    handle_ = ::CreateWindowExW(0, kWindowClassName, wideTitle.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top,
                                nullptr, nullptr, instance, this);
    if (!handle_)
    {
        ATEMFX_LOG_ERROR("CreateWindowExW failed (error %lu)", ::GetLastError());
        return false;
    }

    RECT clientRect = {};
    ::GetClientRect(handle_, &clientRect);
    width_  = static_cast<uint32_t>(clientRect.right - clientRect.left);
    height_ = static_cast<uint32_t>(clientRect.bottom - clientRect.top);

    ::ShowWindow(handle_, SW_SHOWDEFAULT);
    ::UpdateWindow(handle_);

    ATEMFX_LOG_INFO("Window created: %ux%u", width_, height_);
    return true;
}

void Win32Window::destroy()
{
    if (handle_)
    {
        ::DestroyWindow(handle_);
        handle_ = nullptr;
    }
    closed_ = true;
}

void Win32Window::runFrameLoop(const FrameCallback& onFrame)
{
    while (!closed_)
    {
        MSG message = {};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                closed_ = true;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }

        if (closed_)
        {
            break;
        }

        if (minimized_)
        {
            // Nothing to present. Yield rather than spin; M0's loop is the UI
            // thread, and there is no video clock to keep.
            ::Sleep(10);
            continue;
        }

        onFrame();
    }
}

LRESULT CALLBACK Win32Window::windowProcThunk(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    Win32Window* self = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self               = static_cast<Win32Window*>(create->lpCreateParams);
        ::SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self)
        {
            self->handle_ = handle;
        }
    }
    else
    {
        self = reinterpret_cast<Win32Window*>(::GetWindowLongPtrW(handle, GWLP_USERDATA));
    }

    if (self)
    {
        return self->windowProc(handle, message, wParam, lParam);
    }

    return ::DefWindowProcW(handle, message, wParam, lParam);
}

LRESULT Win32Window::windowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Routing faults remain observable even if the UI consumes the message.
    if (message == WM_DISPLAYCHANGE)
    {
        g_displayChanges.store(true, std::memory_order_relaxed);
    }

    // The UI layer gets first refusal on input, exactly as ImGui expects.
    if (g_messageHook && g_messageHook(handle, message, wParam, lParam))
    {
        return 1;
    }

    switch (message)
    {
    case WM_SIZE:
    {
        minimized_ = wParam == SIZE_MINIMIZED;

        const uint32_t width  = static_cast<uint32_t>(LOWORD(lParam));
        const uint32_t height = static_cast<uint32_t>(HIWORD(lParam));

        if (!minimized_ && width > 0 && height > 0)
        {
            width_  = width;
            height_ = height;
            if (resizeCallback_)
            {
                resizeCallback_(width_, height_);
            }
        }
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        auto* info             = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 960;
        info->ptMinTrackSize.y = 600;
        return 0;
    }

    case WM_SYSCOMMAND:
        // Swallow the Alt-key menu activation: it steals focus mid-show.
        if ((wParam & 0xFFF0) == SC_KEYMENU)
        {
            return 0;
        }
        break;

    case WM_CLOSE:
        closed_ = true;
        return 0;

    case WM_DESTROY:
        closed_ = true;
        ::PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return ::DefWindowProcW(handle, message, wParam, lParam);
}

std::unique_ptr<Window> createPlatformWindow()
{
    return std::make_unique<Win32Window>();
}

} // namespace atemfx
