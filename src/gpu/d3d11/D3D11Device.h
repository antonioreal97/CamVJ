#pragma once

// Windows only. Included by the Direct3D 11 backend and by the Direct3D 11 UI
// layer; never by portable code, which sees this backend solely through
// gpu/Rhi.h.

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/Log.h"
#include "gpu/Rhi.h"

namespace atemfx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

bool checkHr(HRESULT hr, const char* what, const char* file, int line);

// Logs and returns false on failure. Never throws: the video path returns
// status, it does not unwind.
#define ATEMFX_CHECK_HR(hr, what) ::atemfx::checkHr((hr), (what), __FILE__, __LINE__)

class D3D11Texture final : public GpuTexture
{
public:
    bool create(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format);

    // CPU-writable and sample-only: dynamic BGRA8, for capture frames arriving
    // in system memory. A dynamic texture cannot also be a render target.
    bool createUploadable(ID3D11Device* device, uint32_t width, uint32_t height);

    void release();

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }
    bool     valid() const override { return texture_ != nullptr; }
    void*    nativeTexture() const override { return srv_.Get(); }

    ID3D11Texture2D*          texture() const { return texture_.Get(); }
    ID3D11RenderTargetView*   rtv() const { return rtv_.Get(); }
    ID3D11ShaderResourceView* srv() const { return srv_.Get(); }
    DXGI_FORMAT               format() const { return format_; }

private:
    ComPtr<ID3D11Texture2D>          texture_;
    ComPtr<ID3D11RenderTargetView>   rtv_;
    ComPtr<ID3D11ShaderResourceView> srv_;

    uint32_t    width_  = 0;
    uint32_t    height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
};

struct D3D11Shader
{
    std::string               name;
    ComPtr<ID3D11PixelShader> pixelShader;
};

class D3D11ShaderLibrary final : public ShaderLibrary
{
public:
    bool initialize(ID3D11Device* device, std::string& error);
    void shutdown();

    ShaderHandle shader(const std::string& name, std::string* error = nullptr) override;
    bool         reloadAll(std::string& error) override;
    std::string  directory() const override { return directory_.string(); }

    ID3D11VertexShader* fullscreenVertexShader() const { return vertexShader_.Get(); }

private:
    bool compile(const std::filesystem::path& path,
                 const char*                  entryPoint,
                 const char*                  target,
                 ComPtr<ID3DBlob>&            blob,
                 std::string&                 error) const;

    bool compileVertexShader(std::string& error);
    bool buildPixelShader(const std::string& name, ComPtr<ID3D11PixelShader>& shader, std::string& error);

    ID3D11Device*         device_ = nullptr;
    std::filesystem::path directory_;

    ComPtr<ID3D11VertexShader> vertexShader_;

    // unique_ptr so a rehash never invalidates a handle already handed out.
    std::unordered_map<std::string, std::unique_ptr<D3D11Shader>> shaders_;
};

class D3D11TargetPool final : public TargetPool
{
public:
    bool create(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format);

    // CPU-writable and sample-only: dynamic BGRA8, for capture frames arriving
    // in system memory. A dynamic texture cannot also be a render target.
    bool createUploadable(ID3D11Device* device, uint32_t width, uint32_t height);

    void release();

    GpuTexture& scratch(std::size_t index) override;
    GpuTexture& persistent(const std::string& key) override;
    GpuTexture& uploadTarget(const std::string& key, uint32_t width, uint32_t height) override;
    bool        upload(GpuTexture& texture, const void* bgra8, std::size_t rowBytes) override;

    uint32_t    width() const override { return width_; }
    uint32_t    height() const override { return height_; }
    DXGI_FORMAT format() const { return format_; }

private:
    static constexpr std::size_t kScratchCount = 2;

    ID3D11Device*        device_  = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    uint32_t             width_   = 0;
    uint32_t             height_  = 0;
    DXGI_FORMAT          format_  = DXGI_FORMAT_UNKNOWN;

    D3D11Texture                                                   scratch_[kScratchCount];
    std::unordered_map<std::string, std::unique_ptr<D3D11Texture>> persistent_;
    std::unordered_map<std::string, std::unique_ptr<D3D11Texture>> uploads_;
};

class D3D11Device;

class D3D11FullscreenPass final : public FullscreenPass
{
public:
    bool initialize(D3D11Device& device, std::string& error);
    void shutdown();

    void draw(GpuTexture&            target,
              ShaderHandle           shader,
              const GpuTexture*      source,
              const EffectConstants& constants,
              SamplerFilter          filter,
              const GpuTexture*      history) override;

private:
    D3D11Device* owner_ = nullptr;

    ComPtr<ID3D11Buffer>            constantBuffer_;
    ComPtr<ID3D11SamplerState>      pointSampler_;
    ComPtr<ID3D11SamplerState>      linearSampler_;
    ComPtr<ID3D11RasterizerState>   rasterizer_;
    ComPtr<ID3D11BlendState>        blendState_;
    ComPtr<ID3D11DepthStencilState> depthStencilState_;
};

// GPU timing with timestamp queries, read back several frames late and always
// with DONOTFLUSH so the CPU never waits on the GPU. A timer that stalls the
// pipeline to report how fast the pipeline is would be worse than no timer.
class D3D11GpuTimer
{
public:
    bool initialize(ID3D11Device* device);
    void shutdown();

    void begin(ID3D11DeviceContext* context);
    void end(ID3D11DeviceContext* context);
    void update(ID3D11DeviceContext* context);

    float milliseconds() const { return milliseconds_; }
    bool  hasResult() const { return hasResult_; }

private:
    static constexpr int kSlotCount = 3;

    struct Slot
    {
        ComPtr<ID3D11Query> disjoint;
        ComPtr<ID3D11Query> start;
        ComPtr<ID3D11Query> stop;
        bool                pending = false;
    };

    Slot     slots_[kSlotCount];
    uint64_t frameIndex_   = 0;
    int      activeSlot_   = -1;
    float    milliseconds_ = 0.0f;
    bool     hasResult_    = false;
};

// The program feed on a second display: its own swap chain on the output
// window, and its own vertex and pixel shaders. It cannot borrow the shader
// library's, which are compiled from files against the processing format and
// the shared effect constant buffer; this pass is not an effect.
class D3D11OutputSurface final : public OutputSurface
{
public:
    ~D3D11OutputSurface() override { shutdown(); }

    bool initialize(D3D11Device& device, void* nativeHandle, uint32_t width, uint32_t height);
    void shutdown();

    void present(GpuTexture& frame) override;
    void resize(uint32_t width, uint32_t height) override;

    uint32_t width() const override { return width_; }
    uint32_t height() const override { return height_; }

private:
    bool createBackBufferView();

    D3D11Device* owner_ = nullptr;

    ComPtr<IDXGISwapChain1>         swapChain_;
    ComPtr<ID3D11RenderTargetView>  backBufferView_;
    ComPtr<ID3D11VertexShader>      vertexShader_;
    ComPtr<ID3D11PixelShader>       pixelShader_;
    ComPtr<ID3D11Buffer>            constantBuffer_;
    ComPtr<ID3D11SamplerState>      sampler_;
    ComPtr<ID3D11RasterizerState>   rasterizer_;
    ComPtr<ID3D11BlendState>        blendState_;
    ComPtr<ID3D11DepthStencilState> depthStencilState_;

    uint32_t width_  = 0;
    uint32_t height_ = 0;
};

class D3D11Device final : public GraphicsDevice
{
public:
    bool initialize(Window* window, uint32_t processingWidth, uint32_t processingHeight) override;
    void shutdown() override;

    bool beginFrame() override;
    void beginProcessing() override;
    void endProcessing() override;
    void beginUi() override;
    void endFrame(bool vsync) override;

    void onWindowResized(uint32_t width, uint32_t height) override;

    std::unique_ptr<OutputSurface> createOutputSurface(void*    nativeHandle,
                                                       uint32_t width,
                                                       uint32_t height) override;

    ShaderLibrary&  shaders() override { return shaders_; }
    FullscreenPass& fullscreenPass() override { return fullscreenPass_; }
    TargetPool&     targets() override { return targets_; }

    float lastGpuMilliseconds() const override { return gpuTimer_.milliseconds(); }
    bool  hasGpuTiming() const override { return gpuTimer_.hasResult(); }

    const std::string& adapterName() const override { return adapterName_; }

    bool readback(GpuTexture& texture, std::vector<uint8_t>& rgba) override;

    // Used by D3D11FullscreenPass and the Direct3D 11 UI layer.
    ID3D11Device*        device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }

private:
    bool createSwapChain(HWND window, uint32_t width, uint32_t height);
    bool createBackBufferView();

    ComPtr<ID3D11Device>           device_;
    ComPtr<ID3D11DeviceContext>    context_;
    ComPtr<IDXGISwapChain1>        swapChain_;
    ComPtr<ID3D11RenderTargetView> backBufferView_;

    D3D11ShaderLibrary  shaders_;
    D3D11TargetPool     targets_;
    D3D11FullscreenPass fullscreenPass_;
    D3D11GpuTimer       gpuTimer_;

    std::string adapterName_ = "unknown";

    uint32_t width_            = 0;
    uint32_t height_           = 0;
    bool     headless_         = false;
    bool     tearingSupported_ = false;
};

} // namespace atemfx
