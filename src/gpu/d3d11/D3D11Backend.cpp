#include "gpu/d3d11/D3D11Device.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include <d3dcompiler.h>

#include "gpu/Backend.h"
#include "gpu/HalfFloat.h"
#include "gpu/ShaderPaths.h"
#include "platform/Window.h"

// The Direct3D 11 classes share a translation unit on purpose. Each is small
// and they are meaningless apart; splitting them would multiply the header
// surface without making any of them easier to read. The Metal backend is laid
// out the same way.

namespace atemfx {

namespace {

constexpr const char* kBackendSubdirectory = "hlsl";
constexpr const char* kCommonSourceFile    = "common.hlsli";
constexpr DXGI_FORMAT kProcessingFormat    = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kSwapChainFormat     = DXGI_FORMAT_R8G8B8A8_UNORM;

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

} // namespace

bool checkHr(HRESULT hr, const char* what, const char* file, int line)
{
    if (SUCCEEDED(hr))
    {
        return true;
    }

    ATEMFX_LOG_ERROR("%s failed: hr=0x%08lX (%s:%d)",
                     what, static_cast<unsigned long>(hr), file, line);
    return false;
}

// ---------------------------------------------------------------------------
// D3D11Texture
// ---------------------------------------------------------------------------

bool D3D11Texture::create(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format)
{
    release();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = format;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (!ATEMFX_CHECK_HR(device->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf()),
                         "CreateTexture2D") ||
        !ATEMFX_CHECK_HR(device->CreateRenderTargetView(texture_.Get(), nullptr, rtv_.GetAddressOf()),
                         "CreateRenderTargetView") ||
        !ATEMFX_CHECK_HR(device->CreateShaderResourceView(texture_.Get(), nullptr, srv_.GetAddressOf()),
                         "CreateShaderResourceView"))
    {
        release();
        return false;
    }

    width_  = width;
    height_ = height;
    format_ = format;
    return true;
}

bool D3D11Texture::createUploadable(ID3D11Device* device, uint32_t width, uint32_t height)
{
    release();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DYNAMIC;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;

    if (!ATEMFX_CHECK_HR(device->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf()),
                         "CreateTexture2D(upload)") ||
        !ATEMFX_CHECK_HR(device->CreateShaderResourceView(texture_.Get(), nullptr, srv_.GetAddressOf()),
                         "CreateShaderResourceView(upload)"))
    {
        release();
        return false;
    }

    width_  = width;
    height_ = height;
    format_ = desc.Format;
    return true;
}

void D3D11Texture::release()
{
    srv_.Reset();
    rtv_.Reset();
    texture_.Reset();
    width_  = 0;
    height_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
}

// ---------------------------------------------------------------------------
// D3D11ShaderLibrary
// ---------------------------------------------------------------------------

bool D3D11ShaderLibrary::initialize(ID3D11Device* device, std::string& error)
{
    device_    = device;
    directory_ = resolveShaderDirectory(kBackendSubdirectory, kCommonSourceFile);

    if (directory_.empty())
    {
        error = "Could not locate the HLSL shader directory. Set ATEMFX_SHADER_DIR.";
        return false;
    }

    ATEMFX_LOG_INFO("HLSL shader directory: %s", directory_.string().c_str());
    return compileVertexShader(error);
}

void D3D11ShaderLibrary::shutdown()
{
    shaders_.clear();
    vertexShader_.Reset();
    device_ = nullptr;
}

bool D3D11ShaderLibrary::compile(const std::filesystem::path& path,
                                 const char*                  entryPoint,
                                 const char*                  target,
                                 ComPtr<ID3DBlob>&            blob,
                                 std::string&                 error) const
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> errors;
    const HRESULT    hr = ::D3DCompileFromFile(path.c_str(),
                                            nullptr,
                                            D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                            entryPoint,
                                            target,
                                            flags,
                                            0,
                                            blob.ReleaseAndGetAddressOf(),
                                            errors.GetAddressOf());

    if (FAILED(hr))
    {
        error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                     errors->GetBufferSize())
                       : ("Failed to open " + path.string());
        return false;
    }

    // A successful compile can still produce warnings worth seeing.
    if (errors && errors->GetBufferSize() > 0)
    {
        ATEMFX_LOG_WARN("%s: %s",
                        path.filename().string().c_str(),
                        static_cast<const char*>(errors->GetBufferPointer()));
    }

    return true;
}

bool D3D11ShaderLibrary::compileVertexShader(std::string& error)
{
    ComPtr<ID3DBlob> blob;
    if (!compile(directory_ / "fullscreen.hlsl", "main", "vs_5_0", blob, error))
    {
        return false;
    }

    ComPtr<ID3D11VertexShader> shader;
    if (!ATEMFX_CHECK_HR(device_->CreateVertexShader(blob->GetBufferPointer(),
                                                     blob->GetBufferSize(),
                                                     nullptr,
                                                     shader.GetAddressOf()),
                         "CreateVertexShader"))
    {
        error = "CreateVertexShader failed for fullscreen.hlsl";
        return false;
    }

    vertexShader_ = shader;
    return true;
}

bool D3D11ShaderLibrary::buildPixelShader(const std::string&         name,
                                          ComPtr<ID3D11PixelShader>& shader,
                                          std::string&               error)
{
    ComPtr<ID3DBlob> blob;
    if (!compile(directory_ / (name + ".hlsl"), "main", "ps_5_0", blob, error))
    {
        return false;
    }

    if (!ATEMFX_CHECK_HR(device_->CreatePixelShader(blob->GetBufferPointer(),
                                                    blob->GetBufferSize(),
                                                    nullptr,
                                                    shader.ReleaseAndGetAddressOf()),
                         "CreatePixelShader"))
    {
        error = "CreatePixelShader failed for " + name;
        return false;
    }

    return true;
}

ShaderHandle D3D11ShaderLibrary::shader(const std::string& name, std::string* error)
{
    if (const auto it = shaders_.find(name); it != shaders_.end())
    {
        // A cached failure is a null shader. Returning early here is what keeps
        // a broken shader from hitting the disk every frame.
        return it->second->pixelShader ? it->second.get() : nullptr;
    }

    auto entry  = std::make_unique<D3D11Shader>();
    entry->name = name;

    std::string localError;
    if (!buildPixelShader(name, entry->pixelShader, localError))
    {
        ATEMFX_LOG_ERROR("%s", localError.c_str());
        if (error)
        {
            *error = localError;
        }
        shaders_.emplace(name, std::move(entry));
        return nullptr;
    }

    ATEMFX_LOG_INFO("Compiled %s.hlsl", name.c_str());
    return shaders_.emplace(name, std::move(entry)).first->second.get();
}

bool D3D11ShaderLibrary::reloadAll(std::string& error)
{
    std::string firstError;
    bool        allSucceeded = true;

    std::string vertexError;
    if (!compileVertexShader(vertexError))
    {
        allSucceeded = false;
        firstError   = vertexError;
        ATEMFX_LOG_ERROR("fullscreen.hlsl: %s", vertexError.c_str());
    }

    for (auto& [name, entry] : shaders_)
    {
        ComPtr<ID3D11PixelShader> shader;
        std::string               localError;
        if (!buildPixelShader(name, shader, localError))
        {
            allSucceeded = false;
            if (firstError.empty())
            {
                firstError = localError;
            }
            ATEMFX_LOG_ERROR("%s", localError.c_str());
            continue;  // keep the previous, working shader
        }
        entry->pixelShader = shader;
    }

    error = firstError;
    if (allSucceeded)
    {
        ATEMFX_LOG_INFO("Reloaded %zu shaders", shaders_.size() + 1);
    }
    return allSucceeded;
}

// ---------------------------------------------------------------------------
// D3D11TargetPool
// ---------------------------------------------------------------------------

bool D3D11TargetPool::create(ID3D11Device*        device,
                             ID3D11DeviceContext* context,
                             uint32_t             width,
                             uint32_t             height,
                             DXGI_FORMAT          format)
{
    release();

    device_  = device;
    context_ = context;
    width_  = width;
    height_ = height;
    format_ = format;

    for (D3D11Texture& target : scratch_)
    {
        if (!target.create(device, width, height, format))
        {
            ATEMFX_LOG_ERROR("Failed to allocate a %ux%u processing target", width, height);
            release();
            return false;
        }
    }

    return true;
}

void D3D11TargetPool::release()
{
    persistent_.clear();
    for (D3D11Texture& target : scratch_)
    {
        target.release();
    }
    uploads_.clear();
    device_  = nullptr;
    context_ = nullptr;
    width_  = 0;
    height_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
}

GpuTexture& D3D11TargetPool::scratch(std::size_t index)
{
    return scratch_[index % kScratchCount];
}

GpuTexture& D3D11TargetPool::persistent(const std::string& key)
{
    if (const auto it = persistent_.find(key); it != persistent_.end())
    {
        return *it->second;
    }

    auto target = std::make_unique<D3D11Texture>();
    if (device_ && target->create(device_, width_, height_, format_))
    {
        ATEMFX_LOG_INFO("Allocated persistent target '%s' (%ux%u)", key.c_str(), width_, height_);
    }
    else
    {
        ATEMFX_LOG_ERROR("Failed to allocate persistent target '%s'", key.c_str());
    }

    return *persistent_.emplace(key, std::move(target)).first->second;
}

GpuTexture& D3D11TargetPool::uploadTarget(const std::string& key, uint32_t width, uint32_t height)
{
    auto it = uploads_.find(key);
    if (it != uploads_.end() && it->second->width() == width && it->second->height() == height)
    {
        return *it->second;
    }

    // A camera can be swapped for one of another size, so the texture follows
    // the frame rather than the project.
    auto target = std::make_unique<D3D11Texture>();
    if (device_ && width > 0 && height > 0 && target->createUploadable(device_, width, height))
    {
        ATEMFX_LOG_INFO("Allocated upload target '%s' (%ux%u)", key.c_str(), width, height);
    }

    if (it != uploads_.end())
    {
        it->second = std::move(target);
        return *it->second;
    }

    return *uploads_.emplace(key, std::move(target)).first->second;
}

bool D3D11TargetPool::upload(GpuTexture& texture, const void* bgra8, std::size_t rowBytes)
{
    D3D11Texture& destination = static_cast<D3D11Texture&>(texture);
    if (!destination.valid() || !bgra8 || !context_)
    {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context_->Map(destination.texture(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        return false;
    }

    // Row by row: the driver's pitch rarely matches the capture stride.
    const std::size_t copyBytes = std::min<std::size_t>(rowBytes, mapped.RowPitch);
    const auto*       sourceRow = static_cast<const uint8_t*>(bgra8);
    auto*             targetRow = static_cast<uint8_t*>(mapped.pData);

    for (uint32_t y = 0; y < destination.height(); ++y)
    {
        std::memcpy(targetRow, sourceRow, copyBytes);
        sourceRow += rowBytes;
        targetRow += mapped.RowPitch;
    }

    context_->Unmap(destination.texture(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// D3D11FullscreenPass
// ---------------------------------------------------------------------------

bool D3D11FullscreenPass::initialize(D3D11Device& device, std::string& error)
{
    owner_ = &device;

    ID3D11Device* d3d = device.device();

    D3D11_BUFFER_DESC constants = {};
    constants.ByteWidth      = sizeof(EffectConstants);
    constants.Usage          = D3D11_USAGE_DYNAMIC;
    constants.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (!ATEMFX_CHECK_HR(d3d->CreateBuffer(&constants, nullptr, constantBuffer_.GetAddressOf()),
                         "CreateBuffer(constant)"))
    {
        error = "Failed to create the shared constant buffer";
        return false;
    }

    // Clamp addressing: effects displace UVs past the edge (RGB Split, Mirror)
    // and wrapping there produces obvious, wrong-looking artefacts.
    D3D11_SAMPLER_DESC sampler = {};
    sampler.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD         = D3D11_FLOAT32_MAX;

    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (!ATEMFX_CHECK_HR(d3d->CreateSamplerState(&sampler, pointSampler_.GetAddressOf()),
                         "CreateSamplerState(point)"))
    {
        error = "Failed to create the point sampler";
        return false;
    }

    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (!ATEMFX_CHECK_HR(d3d->CreateSamplerState(&sampler, linearSampler_.GetAddressOf()),
                         "CreateSamplerState(linear)"))
    {
        error = "Failed to create the linear sampler";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode        = D3D11_FILL_SOLID;
    rasterizer.CullMode        = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    if (!ATEMFX_CHECK_HR(d3d->CreateRasterizerState(&rasterizer, rasterizer_.GetAddressOf()),
                         "CreateRasterizerState"))
    {
        error = "Failed to create the rasterizer state";
        return false;
    }

    // Opaque: the chain overwrites, it never composites. Blending belongs to
    // the layer system, which is not part of V1.
    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (!ATEMFX_CHECK_HR(d3d->CreateBlendState(&blend, blendState_.GetAddressOf()), "CreateBlendState"))
    {
        error = "Failed to create the blend state";
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthStencil = {};
    depthStencil.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (!ATEMFX_CHECK_HR(d3d->CreateDepthStencilState(&depthStencil, depthStencilState_.GetAddressOf()),
                         "CreateDepthStencilState"))
    {
        error = "Failed to create the depth stencil state";
        return false;
    }

    return true;
}

void D3D11FullscreenPass::shutdown()
{
    depthStencilState_.Reset();
    blendState_.Reset();
    rasterizer_.Reset();
    linearSampler_.Reset();
    pointSampler_.Reset();
    constantBuffer_.Reset();
    owner_ = nullptr;
}

void D3D11FullscreenPass::draw(GpuTexture&            target,
                               ShaderHandle           shader,
                               const GpuTexture*      source,
                               const EffectConstants& constants,
                               SamplerFilter          filter,
                               const GpuTexture*      history)
{
    const D3D11Shader* program = static_cast<const D3D11Shader*>(shader);
    if (!owner_ || !program || !program->pixelShader || !target.valid())
    {
        return;
    }

    ID3D11DeviceContext* context      = owner_->context();
    ID3D11VertexShader*  vertexShader = static_cast<D3D11ShaderLibrary&>(owner_->shaders())
                                           .fullscreenVertexShader();
    if (!context || !vertexShader)
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer_.Get(), 0);
    }

    D3D11Texture&           destination = static_cast<D3D11Texture&>(target);
    ID3D11RenderTargetView* rtv         = destination.rtv();
    context->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT viewport = {};
    viewport.Width    = static_cast<float>(destination.width());
    viewport.Height   = static_cast<float>(destination.height());
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    // Complete state on every call, on purpose: ImGui shares the immediate
    // context, and a pass that inherits state is a pass that fails
    // intermittently.
    context->RSSetState(rasterizer_.Get());
    context->OMSetBlendState(blendState_.Get(), nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(depthStencilState_.Get(), 0);

    // No vertex buffer and no input layout: the vertex shader builds the
    // triangle from SV_VertexID.
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vertexShader, nullptr, 0);
    context->PSSetShader(program->pixelShader.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);

    ID3D11ShaderResourceView* srvs[2] = {
        source ? static_cast<const D3D11Texture*>(source)->srv() : nullptr,
        history ? static_cast<const D3D11Texture*>(history)->srv() : nullptr,
    };
    ID3D11SamplerState* sampler =
        (filter == SamplerFilter::Point) ? pointSampler_.Get() : linearSampler_.Get();
    ID3D11Buffer* buffer = constantBuffer_.Get();

    context->PSSetShaderResources(0, 2, srvs);
    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetConstantBuffers(0, 1, &buffer);
    context->VSSetConstantBuffers(0, 1, &buffer);

    context->Draw(3, 0);

    // Unbind both ends. The chain reuses its two targets in rotation, so this
    // pass's shader resource is the next pass's render target; leaving either
    // bound produces a read/write hazard and a silently black frame.
    ID3D11ShaderResourceView* nullSrvs[2] = {};
    context->PSSetShaderResources(0, 2, nullSrvs);

    ID3D11RenderTargetView* nullRtv = nullptr;
    context->OMSetRenderTargets(1, &nullRtv, nullptr);
}

// ---------------------------------------------------------------------------
// D3D11GpuTimer
// ---------------------------------------------------------------------------

bool D3D11GpuTimer::initialize(ID3D11Device* device)
{
    D3D11_QUERY_DESC disjointDesc = {};
    disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

    D3D11_QUERY_DESC timestampDesc = {};
    timestampDesc.Query = D3D11_QUERY_TIMESTAMP;

    for (Slot& slot : slots_)
    {
        if (!ATEMFX_CHECK_HR(device->CreateQuery(&disjointDesc, slot.disjoint.GetAddressOf()),
                             "CreateQuery(disjoint)") ||
            !ATEMFX_CHECK_HR(device->CreateQuery(&timestampDesc, slot.start.GetAddressOf()),
                             "CreateQuery(timestamp start)") ||
            !ATEMFX_CHECK_HR(device->CreateQuery(&timestampDesc, slot.stop.GetAddressOf()),
                             "CreateQuery(timestamp stop)"))
        {
            shutdown();
            return false;
        }
    }

    return true;
}

void D3D11GpuTimer::shutdown()
{
    for (Slot& slot : slots_)
    {
        slot.disjoint.Reset();
        slot.start.Reset();
        slot.stop.Reset();
        slot.pending = false;
    }
    activeSlot_ = -1;
}

void D3D11GpuTimer::begin(ID3D11DeviceContext* context)
{
    const int slotIndex = static_cast<int>(frameIndex_ % kSlotCount);
    Slot&     slot      = slots_[slotIndex];

    // The GPU is more than kSlotCount frames behind. Skip this measurement
    // rather than overwrite a query still in flight.
    if (slot.pending || !slot.disjoint)
    {
        activeSlot_ = -1;
        return;
    }

    activeSlot_ = slotIndex;
    context->Begin(slot.disjoint.Get());
    context->End(slot.start.Get());
}

void D3D11GpuTimer::end(ID3D11DeviceContext* context)
{
    if (activeSlot_ < 0)
    {
        ++frameIndex_;
        return;
    }

    Slot& slot = slots_[activeSlot_];
    context->End(slot.stop.Get());
    context->End(slot.disjoint.Get());
    slot.pending = true;

    activeSlot_ = -1;
    ++frameIndex_;
}

void D3D11GpuTimer::update(ID3D11DeviceContext* context)
{
    for (Slot& slot : slots_)
    {
        if (!slot.pending)
        {
            continue;
        }

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
        if (context->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
        {
            continue;
        }

        UINT64 start = 0;
        UINT64 stop  = 0;
        if (context->GetData(slot.start.Get(), &start, sizeof(start),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(slot.stop.Get(), &stop, sizeof(stop),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
        {
            continue;
        }

        slot.pending = false;

        // Disjoint means the GPU clock changed frequency mid-measurement. The
        // numbers are meaningless; drop them rather than report a spike.
        if (disjoint.Disjoint || disjoint.Frequency == 0 || stop < start)
        {
            continue;
        }

        milliseconds_ = static_cast<float>(static_cast<double>(stop - start) * 1000.0 /
                                           static_cast<double>(disjoint.Frequency));
        hasResult_    = true;
    }
}

// ---------------------------------------------------------------------------
// D3D11OutputSurface
// ---------------------------------------------------------------------------

namespace {

// Self-contained, and compiled from memory rather than from shaders/hlsl: this
// pass is not an effect, it does not use EffectCB, and it must not inherit
// whatever common.hlsli declares.
constexpr const char* kOutputShaderSource = R"HLSL(
cbuffer OutputCB : register(b0)
{
    float4 uScale;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOutput vertex_main(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    output.uv       = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

    return output;
}

Texture2D    uSource  : register(t0);
SamplerState uSampler : register(s0);

// uScale.xy applies to centred coordinates: greater than one shrinks the frame
// inside the display, which is what produces the letterbox bars.
float4 pixel_main(VSOutput input) : SV_Target
{
    float2 uv = (input.uv - 0.5) * uScale.xy + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    return float4(uSource.Sample(uSampler, uv).rgb, 1.0);
}
)HLSL";

bool compileOutputShader(const char*       entryPoint,
                         const char*       target,
                         ComPtr<ID3DBlob>& blob,
                         std::string&      error)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> errors;
    const HRESULT    hr = ::D3DCompile(kOutputShaderSource,
                                    std::strlen(kOutputShaderSource),
                                    "output",
                                    nullptr,
                                    nullptr,
                                    entryPoint,
                                    target,
                                    flags,
                                    0,
                                    blob.ReleaseAndGetAddressOf(),
                                    errors.GetAddressOf());

    if (FAILED(hr))
    {
        error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                     errors->GetBufferSize())
                       : "D3DCompile failed for the output shader";
        return false;
    }

    return true;
}

} // namespace

bool D3D11OutputSurface::initialize(D3D11Device& device,
                                    void*        nativeHandle,
                                    uint32_t     width,
                                    uint32_t     height)
{
    owner_ = &device;

    HWND window = static_cast<HWND>(nativeHandle);
    if (!window)
    {
        ATEMFX_LOG_ERROR("Output window has no HWND");
        return false;
    }

    ID3D11Device* d3d = device.device();
    if (!d3d)
    {
        return false;
    }

    width_  = std::max(width, 1u);
    height_ = std::max(height, 1u);

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (!ATEMFX_CHECK_HR(d3d->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf())),
                         "QueryInterface(IDXGIDevice1)"))
    {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (!ATEMFX_CHECK_HR(dxgiDevice->GetAdapter(adapter.GetAddressOf()), "IDXGIDevice1::GetAdapter"))
    {
        return false;
    }

    ComPtr<IDXGIFactory2> factory;
    if (!ATEMFX_CHECK_HR(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())),
                         "IDXGIAdapter::GetParent(IDXGIFactory2)"))
    {
        return false;
    }

    // No tearing flag and no tearing present: this swap chain is the show
    // clock, so it always waits for the output display.
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width            = width_;
    desc.Height           = height_;
    desc.Format           = kSwapChainFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = 2;
    desc.Scaling          = DXGI_SCALING_STRETCH;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode        = DXGI_ALPHA_MODE_UNSPECIFIED;

    if (!ATEMFX_CHECK_HR(factory->CreateSwapChainForHwnd(d3d, window, &desc, nullptr, nullptr,
                                                         swapChain_.GetAddressOf()),
                         "CreateSwapChainForHwnd(output)"))
    {
        return false;
    }

    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

    if (!createBackBufferView())
    {
        return false;
    }

    std::string      error;
    ComPtr<ID3DBlob> blob;

    if (!compileOutputShader("vertex_main", "vs_5_0", blob, error))
    {
        ATEMFX_LOG_ERROR("Output vertex shader: %s", error.c_str());
        return false;
    }
    if (!ATEMFX_CHECK_HR(d3d->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                 nullptr, vertexShader_.GetAddressOf()),
                         "CreateVertexShader(output)"))
    {
        return false;
    }

    if (!compileOutputShader("pixel_main", "ps_5_0", blob, error))
    {
        ATEMFX_LOG_ERROR("Output pixel shader: %s", error.c_str());
        return false;
    }
    if (!ATEMFX_CHECK_HR(d3d->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                nullptr, pixelShader_.GetAddressOf()),
                         "CreatePixelShader(output)"))
    {
        return false;
    }

    D3D11_BUFFER_DESC constants = {};
    constants.ByteWidth      = 16;   // one float4, the 16-byte minimum
    constants.Usage          = D3D11_USAGE_DYNAMIC;
    constants.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (!ATEMFX_CHECK_HR(d3d->CreateBuffer(&constants, nullptr, constantBuffer_.GetAddressOf()),
                         "CreateBuffer(output constant)"))
    {
        return false;
    }

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD         = D3D11_FLOAT32_MAX;
    if (!ATEMFX_CHECK_HR(d3d->CreateSamplerState(&sampler, sampler_.GetAddressOf()),
                         "CreateSamplerState(output)"))
    {
        return false;
    }

    // Its own states, for the same reason D3D11FullscreenPass sets complete
    // state on every draw: ImGui shares the immediate context.
    D3D11_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode        = D3D11_FILL_SOLID;
    rasterizer.CullMode        = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    if (!ATEMFX_CHECK_HR(d3d->CreateRasterizerState(&rasterizer, rasterizer_.GetAddressOf()),
                         "CreateRasterizerState(output)"))
    {
        return false;
    }

    D3D11_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (!ATEMFX_CHECK_HR(d3d->CreateBlendState(&blend, blendState_.GetAddressOf()),
                         "CreateBlendState(output)"))
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthStencil = {};
    depthStencil.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (!ATEMFX_CHECK_HR(d3d->CreateDepthStencilState(&depthStencil,
                                                      depthStencilState_.GetAddressOf()),
                         "CreateDepthStencilState(output)"))
    {
        return false;
    }

    ATEMFX_LOG_INFO("Output surface ready: %ux%u pixels", width_, height_);
    return true;
}

bool D3D11OutputSurface::createBackBufferView()
{
    backBufferView_.Reset();

    ComPtr<ID3D11Texture2D> backBuffer;
    if (!ATEMFX_CHECK_HR(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())),
                         "IDXGISwapChain1::GetBuffer(output)"))
    {
        return false;
    }

    return ATEMFX_CHECK_HR(owner_->device()->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                                                    backBufferView_.GetAddressOf()),
                           "CreateRenderTargetView(output)");
}

void D3D11OutputSurface::shutdown()
{
    depthStencilState_.Reset();
    blendState_.Reset();
    rasterizer_.Reset();
    sampler_.Reset();
    constantBuffer_.Reset();
    pixelShader_.Reset();
    vertexShader_.Reset();
    backBufferView_.Reset();
    swapChain_.Reset();

    owner_  = nullptr;
    width_  = 0;
    height_ = 0;
}

void D3D11OutputSurface::resize(uint32_t width, uint32_t height)
{
    if (!swapChain_ || width == 0 || height == 0)
    {
        return;
    }

    width_  = width;
    height_ = height;

    backBufferView_.Reset();
    if (!ATEMFX_CHECK_HR(swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0),
                         "IDXGISwapChain1::ResizeBuffers(output)"))
    {
        return;
    }

    createBackBufferView();
}

void D3D11OutputSurface::present(GpuTexture& frame)
{
    if (!owner_ || !swapChain_ || !backBufferView_ || !pixelShader_ || !vertexShader_)
    {
        return;
    }

    D3D11Texture& source = static_cast<D3D11Texture&>(frame);
    if (!source.valid() || width_ == 0 || height_ == 0)
    {
        return;
    }

    ID3D11DeviceContext* context = owner_->context();
    if (!context)
    {
        return;
    }

    const float targetAspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float sourceAspect =
        static_cast<float>(source.width()) / static_cast<float>(std::max(source.height(), 1u));

    // Fit, the same arithmetic as shaders/hlsl/source_blit.hlsl.
    float scale[4] = {1.0f, 1.0f, 0.0f, 0.0f};
    if (sourceAspect > targetAspect)
    {
        scale[1] = sourceAspect / targetAspect;
    }
    else
    {
        scale[0] = targetAspect / sourceAspect;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, scale, sizeof(scale));
        context->Unmap(constantBuffer_.Get(), 0);
    }

    ID3D11RenderTargetView* rtv = backBufferView_.Get();
    context->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT viewport = {};
    viewport.Width    = static_cast<float>(width_);
    viewport.Height   = static_cast<float>(height_);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(rtv, black);

    context->RSSetState(rasterizer_.Get());
    context->OMSetBlendState(blendState_.Get(), nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(depthStencilState_.Get(), 0);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);

    ID3D11ShaderResourceView* srv     = source.srv();
    ID3D11SamplerState*       state   = sampler_.Get();
    ID3D11Buffer*             buffer  = constantBuffer_.Get();

    context->PSSetShaderResources(0, 1, &srv);
    context->PSSetSamplers(0, 1, &state);
    context->PSSetConstantBuffers(0, 1, &buffer);

    context->Draw(3, 0);

    // The frame this just sampled is a chain target that the next frame will
    // render into. Leaving it bound is the read/write hazard the fullscreen
    // pass documents.
    ID3D11ShaderResourceView* nullSrv = nullptr;
    context->PSSetShaderResources(0, 1, &nullSrv);

    ID3D11RenderTargetView* nullRtv = nullptr;
    context->OMSetRenderTargets(1, &nullRtv, nullptr);

    const HRESULT hr = swapChain_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        ATEMFX_LOG_ERROR("Graphics device removed while presenting the output (hr=0x%08lX)",
                         static_cast<unsigned long>(hr));
    }
}

// ---------------------------------------------------------------------------
// D3D11Device
// ---------------------------------------------------------------------------

bool D3D11Device::initialize(Window* window, uint32_t processingWidth, uint32_t processingHeight)
{
    headless_ = window == nullptr;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL       obtained    = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = ::D3D11CreateDevice(nullptr,
                                     D3D_DRIVER_TYPE_HARDWARE,
                                     nullptr,
                                     flags,
                                     requested,
                                     static_cast<UINT>(std::size(requested)),
                                     D3D11_SDK_VERSION,
                                     device_.GetAddressOf(),
                                     &obtained,
                                     context_.GetAddressOf());

#ifdef _DEBUG
    if (FAILED(hr))
    {
        // The debug layer is a separate optional Windows component. Its absence
        // must not stop the application from starting.
        ATEMFX_LOG_WARN("D3D11 debug layer unavailable, retrying without it");
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                 requested, static_cast<UINT>(std::size(requested)),
                                 D3D11_SDK_VERSION, device_.GetAddressOf(), &obtained,
                                 context_.GetAddressOf());
    }
#endif

    if (!ATEMFX_CHECK_HR(hr, "D3D11CreateDevice"))
    {
        return false;
    }

    if (!headless_ && !createSwapChain(static_cast<HWND>(window->nativeHandle()),
                                       window->width(),
                                       window->height()))
    {
        return false;
    }

    std::string error;
    if (!shaders_.initialize(device_.Get(), error))
    {
        ATEMFX_LOG_ERROR("Shader library: %s", error.c_str());
        return false;
    }

    if (!targets_.create(device_.Get(), context_.Get(), processingWidth, processingHeight,
                         kProcessingFormat))
    {
        return false;
    }

    if (!fullscreenPass_.initialize(*this, error))
    {
        ATEMFX_LOG_ERROR("Fullscreen pass: %s", error.c_str());
        return false;
    }

    if (!gpuTimer_.initialize(device_.Get()))
    {
        // Losing the timer costs diagnostics, not video. Carry on.
        ATEMFX_LOG_WARN("GPU timing unavailable");
    }

    ATEMFX_LOG_INFO("D3D11 device ready: %s (feature level %s, tearing %s, %s)",
                    adapterName_.c_str(),
                    obtained == D3D_FEATURE_LEVEL_11_1 ? "11_1" : "11_0",
                    tearingSupported_ ? "supported" : "unsupported",
                    headless_ ? "headless" : "windowed");
    return true;
}

bool D3D11Device::createSwapChain(HWND window, uint32_t width, uint32_t height)
{
    width_  = std::max(width, 1u);
    height_ = std::max(height, 1u);

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (!ATEMFX_CHECK_HR(device_.As(&dxgiDevice), "QueryInterface(IDXGIDevice1)"))
    {
        return false;
    }

    // One frame of queued work. Deeper queues raise throughput and latency; a
    // live video tool wants the opposite trade.
    dxgiDevice->SetMaximumFrameLatency(1);

    ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(dxgiDevice->GetAdapter(adapter.GetAddressOf())))
    {
        DXGI_ADAPTER_DESC adapterDesc = {};
        if (SUCCEEDED(adapter->GetDesc(&adapterDesc)))
        {
            adapterName_ = toUtf8(adapterDesc.Description);
        }
    }

    ComPtr<IDXGIFactory2> factory;
    if (!adapter || !ATEMFX_CHECK_HR(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())),
                                     "IDXGIAdapter::GetParent(IDXGIFactory2)"))
    {
        return false;
    }

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5)))
    {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                    &allowTearing, sizeof(allowTearing))))
        {
            tearingSupported_ = allowTearing == TRUE;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width            = width_;
    desc.Height           = height_;
    desc.Format           = kSwapChainFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = 2;
    desc.Scaling          = DXGI_SCALING_STRETCH;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode        = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags            = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

    if (!ATEMFX_CHECK_HR(factory->CreateSwapChainForHwnd(device_.Get(), window, &desc,
                                                         nullptr, nullptr,
                                                         swapChain_.GetAddressOf()),
                         "CreateSwapChainForHwnd"))
    {
        return false;
    }

    // Alt+Enter fullscreen is DXGI's, not ours. A live tool must not change
    // display mode because someone brushed a key.
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

    return createBackBufferView();
}

bool D3D11Device::createBackBufferView()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    if (!ATEMFX_CHECK_HR(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())),
                         "IDXGISwapChain::GetBuffer"))
    {
        return false;
    }

    return ATEMFX_CHECK_HR(device_->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                                           backBufferView_.ReleaseAndGetAddressOf()),
                           "CreateRenderTargetView(back buffer)");
}

void D3D11Device::shutdown()
{
    gpuTimer_.shutdown();
    fullscreenPass_.shutdown();
    targets_.release();
    shaders_.shutdown();

    backBufferView_.Reset();

    if (context_)
    {
        context_->ClearState();
        context_->Flush();
    }

    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
}

bool D3D11Device::beginFrame()
{
    if (!headless_ && !backBufferView_)
    {
        return false;
    }

    gpuTimer_.update(context_.Get());
    return true;
}

void D3D11Device::beginProcessing()
{
    gpuTimer_.begin(context_.Get());
}

void D3D11Device::endProcessing()
{
    gpuTimer_.end(context_.Get());
}

void D3D11Device::beginUi()
{
    if (headless_ || !backBufferView_)
    {
        return;
    }

    ID3D11RenderTargetView* view = backBufferView_.Get();
    context_->OMSetRenderTargets(1, &view, nullptr);

    D3D11_VIEWPORT viewport = {};
    viewport.Width    = static_cast<float>(width_);
    viewport.Height   = static_cast<float>(height_);
    viewport.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &viewport);

    const float background[4] = {0.04f, 0.04f, 0.05f, 1.0f};
    context_->ClearRenderTargetView(view, background);
}

void D3D11Device::endFrame(bool vsync)
{
    if (headless_ || !swapChain_)
    {
        return;
    }

    const UINT interval = vsync ? 1u : 0u;
    const UINT flags    = (!vsync && tearingSupported_) ? DXGI_PRESENT_ALLOW_TEARING : 0u;

    const HRESULT hr = swapChain_->Present(interval, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        // Device loss is survivable in principle, but recovering it belongs to
        // the milestone that owns hardware robustness. Report it loudly now.
        ATEMFX_LOG_ERROR("Graphics device removed (hr=0x%08lX). Restart required.",
                         static_cast<unsigned long>(hr));
    }
}

std::unique_ptr<OutputSurface> D3D11Device::createOutputSurface(void*    nativeHandle,
                                                                uint32_t width,
                                                                uint32_t height)
{
    if (headless_)
    {
        ATEMFX_LOG_ERROR("A headless device has no display output");
        return nullptr;
    }

    auto surface = std::make_unique<D3D11OutputSurface>();
    if (!surface->initialize(*this, nativeHandle, width, height))
    {
        return nullptr;
    }
    return surface;
}

void D3D11Device::onWindowResized(uint32_t width, uint32_t height)
{
    if (!swapChain_ || width == 0 || height == 0)
    {
        return;
    }
    if (width == width_ && height == height_ && backBufferView_)
    {
        return;
    }

    // The view must be released before ResizeBuffers, and nothing may still be
    // bound to the pipeline.
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    backBufferView_.Reset();

    const UINT flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    if (!ATEMFX_CHECK_HR(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags),
                         "IDXGISwapChain::ResizeBuffers"))
    {
        return;
    }

    width_  = width;
    height_ = height;
    createBackBufferView();
}

bool D3D11Device::readback(GpuTexture& texture, std::vector<uint8_t>& rgba)
{
    D3D11Texture& source = static_cast<D3D11Texture&>(texture);
    if (!source.valid())
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    source.texture()->GetDesc(&desc);

    // The half-float unpacking below is specific to R16G16B16A16_FLOAT, which
    // is the only format the pool hands out. Fail loudly rather than silently
    // produce garbage if that ever changes.
    if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
    {
        ATEMFX_LOG_ERROR("Readback only supports R16G16B16A16_FLOAT");
        return false;
    }

    desc.Usage          = D3D11_USAGE_STAGING;
    desc.BindFlags      = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags      = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (!ATEMFX_CHECK_HR(device_->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()),
                         "CreateTexture2D(staging)"))
    {
        return false;
    }

    context_->CopyResource(staging.Get(), source.texture());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (!ATEMFX_CHECK_HR(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
                         "Map(staging)"))
    {
        return false;
    }

    const uint32_t width  = source.width();
    const uint32_t height = source.height();
    rgba.resize(static_cast<std::size_t>(width) * height * 4);

    for (uint32_t y = 0; y < height; ++y)
    {
        const uint16_t* row =
            reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) +
                                              static_cast<std::size_t>(y) * mapped.RowPitch);
        for (uint32_t i = 0; i < width * 4; ++i)
        {
            const float value = std::clamp(halfToFloat(row[i]), 0.0f, 1.0f);
            rgba[static_cast<std::size_t>(y) * width * 4 + i] =
                static_cast<uint8_t>(value * 255.0f + 0.5f);
        }
    }

    context_->Unmap(staging.Get(), 0);
    return true;
}

// ---------------------------------------------------------------------------
// Backend factory
// ---------------------------------------------------------------------------

std::unique_ptr<GraphicsDevice> createGraphicsDevice()
{
    return std::make_unique<D3D11Device>();
}

const char* backendName()
{
    return "Direct3D 11";
}

} // namespace atemfx
