#include "overlays/overlay_platform.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdio>
#include <limits>
#include <mutex>
#include <thread>

namespace atemfx {
namespace {

using Microsoft::WRL::ComPtr;

std::string windowsError(const char* prefix, HRESULT result)
{
    char text[128];
    std::snprintf(text, sizeof(text), "%s (0x%08lx)", prefix,
                  static_cast<unsigned long>(result));
    return text;
}

class ComApartment
{
public:
    explicit ComApartment(DWORD mode) : result_(CoInitializeEx(nullptr, mode)) {}
    ~ComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
    HRESULT result() const { return result_; }

private:
    HRESULT result_;
};

struct PickerSharedState
{
    std::mutex          mutex;
    OverlayPickerResult result;
    std::atomic<bool>   cancelled{false};
    std::atomic<HWND>   dialogWindow{nullptr};
};

class WindowsOverlaySourcePicker final : public OverlaySourcePicker
{
public:
    WindowsOverlaySourcePicker() : shared_(std::make_shared<PickerSharedState>()) {}

    ~WindowsOverlaySourcePicker() override
    {
        cancel();
        // The shared result owns everything the worker touches. Detach keeps
        // shutdown non-blocking if Windows has not yet delivered WM_CLOSE to
        // the native modal dialog.
        if (worker_.joinable()) worker_.detach();
    }

    bool begin(OverlayPickerMode mode, void* nativeParent, std::string& error) override
    {
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            if (shared_->result.state == OverlayPickerState::Picking)
            {
                error = "An overlay picker is already open";
                return false;
            }
        }
        if (worker_.joinable()) worker_.join();

        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            shared_->result = {};
            shared_->result.state = OverlayPickerState::Picking;
        }
        shared_->cancelled.store(false, std::memory_order_release);
        const auto shared = shared_;
        const HWND parent = static_cast<HWND>(nativeParent);
        worker_ = std::thread([shared, mode, parent]() {
            OverlayPickerResult result;
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
            {
                result.state = OverlayPickerState::Failed;
                result.error = windowsError("Could not initialize the overlay picker", initialized);
            }
            else
            {
                ComPtr<IFileOpenDialog> dialog;
                HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&dialog));
                if (SUCCEEDED(hr))
                {
                    FILEOPENDIALOGOPTIONS options = {};
                    dialog->GetOptions(&options);
                    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
                    if (mode == OverlayPickerMode::SequenceDirectory)
                        options |= FOS_PICKFOLDERS;
                    else
                        options |= FOS_FILEMUSTEXIST;
                    dialog->SetOptions(options);
                    dialog->SetTitle(mode == OverlayPickerMode::StillPng
                                         ? L"Import Overlay PNG"
                                         : L"Import Overlay PNG Sequence");
                    if (mode == OverlayPickerMode::StillPng)
                    {
                        const COMDLG_FILTERSPEC filter[] = {{L"PNG image", L"*.png"}};
                        dialog->SetFileTypes(1, filter);
                        dialog->SetDefaultExtension(L"png");
                    }
                    ComPtr<IOleWindow> dialogOleWindow;
                    HWND dialogWindow = nullptr;
                    if (SUCCEEDED(dialog.As(&dialogOleWindow)))
                        dialogOleWindow->GetWindow(&dialogWindow);
                    shared->dialogWindow.store(dialogWindow, std::memory_order_release);
                    if (shared->cancelled.load(std::memory_order_acquire))
                        hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                    else
                        hr = dialog->Show(parent);
                    shared->dialogWindow.store(nullptr, std::memory_order_release);
                    if (shared->cancelled.load(std::memory_order_acquire) ||
                        hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                    {
                        result.state = OverlayPickerState::Cancelled;
                    }
                    else if (SUCCEEDED(hr))
                    {
                        ComPtr<IShellItem> item;
                        PWSTR selected = nullptr;
                        hr = dialog->GetResult(&item);
                        if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &selected);
                        if (SUCCEEDED(hr) && selected)
                        {
                            result.state = OverlayPickerState::Selected;
                            result.path = std::filesystem::path(selected);
                        }
                        else
                        {
                            result.state = OverlayPickerState::Failed;
                            result.error = windowsError("Could not read the selected path", hr);
                        }
                        if (selected) CoTaskMemFree(selected);
                    }
                    else
                    {
                        result.state = OverlayPickerState::Failed;
                        result.error = windowsError("Overlay picker failed", hr);
                    }
                }
                else
                {
                    result.state = OverlayPickerState::Failed;
                    result.error = windowsError("Could not create the overlay picker", hr);
                }
            }
            if (SUCCEEDED(initialized)) CoUninitialize();
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->result = std::move(result);
        });
        return true;
    }

    OverlayPickerState state() const override
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        return shared_->result.state;
    }

    bool poll(OverlayPickerResult& out) override
    {
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            const OverlayPickerState current = shared_->result.state;
            if (current != OverlayPickerState::Selected &&
                current != OverlayPickerState::Cancelled &&
                current != OverlayPickerState::Failed)
                return false;
            out = std::move(shared_->result);
            shared_->result = {};
        }
        if (worker_.joinable()) worker_.join();
        return true;
    }

    void cancel() override
    {
        shared_->cancelled.store(true, std::memory_order_release);
        if (const HWND dialog = shared_->dialogWindow.load(std::memory_order_acquire))
            PostMessageW(dialog, WM_CLOSE, 0, 0);
    }

private:
    std::shared_ptr<PickerSharedState> shared_;
    std::thread                        worker_;
};

} // namespace

bool decodeOverlayPng(const std::filesystem::path& path,
                      DecodedOverlayImage& out, std::string& error)
{
    out.width = 0;
    out.height = 0;
    out.rowBytes = 0;
    out.bgra8.clear();
    ComApartment apartment(COINIT_MULTITHREADED);
    const HRESULT initialized = apartment.result();
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    {
        error = windowsError("Could not initialize PNG decoding", initialized);
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
    GUID container = {};
    UINT frameCount = 0;
    if (SUCCEEDED(hr)) hr = decoder->GetContainerFormat(&container);
    if (SUCCEEDED(hr)) hr = decoder->GetFrameCount(&frameCount);
    if (FAILED(hr) || container != GUID_ContainerFormatPng || frameCount != 1)
    {
        error = "Overlay is not a single-frame PNG image";
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || !((width == 1920 && height == 1080) ||
                        (width == 1080 && height == 1920)))
    {
        error = "Overlay must be exactly 1920x1080 or 1080x1920";
        return false;
    }
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (FAILED(hr) || width == 0 || height == 0 ||
        static_cast<std::size_t>(width) >
            std::numeric_limits<std::size_t>::max() / 4U / height)
    {
        error = windowsError("Could not decode overlay PNG", hr);
        return false;
    }

    out.width = width;
    out.height = height;
    out.rowBytes = static_cast<std::size_t>(width) * 4U;
    out.bgra8.resize(out.rowBytes * height);
    if (out.bgra8.size() > std::numeric_limits<UINT>::max() ||
        out.rowBytes > std::numeric_limits<UINT>::max())
    {
        out.width = 0;
        out.height = 0;
        out.rowBytes = 0;
        out.bgra8.clear();
        error = "Overlay PNG is too large";
        return false;
    }
    hr = converter->CopyPixels(nullptr, static_cast<UINT>(out.rowBytes),
                               static_cast<UINT>(out.bgra8.size()), out.bgra8.data());
    if (FAILED(hr))
    {
        out.width = 0;
        out.height = 0;
        out.rowBytes = 0;
        out.bgra8.clear();
        error = windowsError("Could not copy overlay PNG pixels", hr);
        return false;
    }
    return true;
}

bool replaceOverlayFileAtomically(const std::filesystem::path& source,
                                  const std::filesystem::path& destination,
                                  std::string& error)
{
    if (!MoveFileExW(source.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = "Could not publish overlay manifest (Windows error " +
                std::to_string(GetLastError()) + ")";
        return false;
    }
    return true;
}

std::unique_ptr<OverlaySourcePicker> createOverlaySourcePicker()
{
    return std::make_unique<WindowsOverlaySourcePicker>();
}

} // namespace atemfx
