#include "VideoEffects.h"
#include "GpuDevice.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr int MaxFaces = 4;

    // Must match the cbuffer layout in the shader (HLSL packs into 16-byte registers).
    struct alignas(16) Constants
    {
        float crop[4];      // visible part of the input: x, y, w, h (normalized)
        float content[4];   // where the picture goes in the output (letterboxing): x, y, w, h
        float inputSize[2]; // visible input size in pixels
        float uvScale[2];   // visible size / texture size (decoded textures can be padded, e.g. 1088 rows)
        float flip[2];
        float redness;
        float smoothing;
        int faceCount;      // -1: no face information -> retouch all skin-coloured pixels
        float texel[2];     // 1 / texture size (luma), to keep samples inside the visible area
        float pad;
        float faces[MaxFaces][4]; // ellipses: centre x, y and radii x, y (normalized input coordinates)
    };
    static_assert(sizeof(Constants) == 144);

    const char ShaderSource[] = R"hlsl(
Texture2D<float>  InY  : register(t0);
Texture2D<float2> InUV : register(t1);
SamplerState Linear    : register(s0);

cbuffer Params : register(b0)
{
    float4 Crop;
    float4 Content;
    float2 InputSize;
    float2 UVScale;
    float2 Flip;
    float  Redness;
    float  Smoothing;
    int    FaceCount;
    float2 Texel;
    float  Pad;
    float4 Faces[4];
};

// Keep samples inside the visible picture: decoded textures can have padding rows (e.g. 1088 for
// 1080p) that the bilinear filter would otherwise blend into the edge.
float2 ClampY(float2 t)  { return clamp(t, 0.5 * Texel, UVScale - 0.5 * Texel); }
float2 ClampUV(float2 t) { return clamp(t, Texel, UVScale - Texel); } // chroma texels are 2x larger

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VS(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uv;
    return o;
}

// Output position -> normalized input position. Returns false outside the picture (letterbox).
bool MapToInput(float2 outUV, out float2 p)
{
    if (Flip.x > 0.5) outUV.x = 1 - outUV.x;
    if (Flip.y > 0.5) outUV.y = 1 - outUV.y;
    float2 c = (outUV - Content.xy) / Content.zw;
    p = Crop.xy + c * Crop.zw;
    return all(c >= 0) && all(c <= 1);
}

// Soft YCbCr skin-tone classifier (Cb 77..127, Cr 133..173 in 8-bit video levels).
float SkinWeight(float2 cbcr)
{
    float cb = cbcr.x * 255, cr = cbcr.y * 255;
    return smoothstep(70, 82, cb) * (1 - smoothstep(122, 134, cb)) *
           smoothstep(128, 140, cr) * (1 - smoothstep(170, 186, cr));
}

// 1 inside the (enlarged) face ellipses with a soft edge; 1 everywhere when there is no face info.
float FaceMask(float2 p)
{
    if (FaceCount < 0) return 1;
    float m = 0;
    for (int i = 0; i < FaceCount; i++)
    {
        float2 d = (p - Faces[i].xy) / Faces[i].zw;
        m = max(m, 1 - smoothstep(0.6, 1.0, dot(d, d)));
    }
    return m;
}

float PS_Y(VSOut i) : SV_Target
{
    float2 p;
    if (!MapToInput(i.uv, p)) return 16.0 / 255;
    float2 t = p * UVScale;
    float y = InY.SampleLevel(Linear, ClampY(t), 0);

    if (Smoothing > 0.001)
    {
        float mask = SkinWeight(InUV.SampleLevel(Linear, ClampUV(t), 0)) * FaceMask(p) * Smoothing;
        if (mask > 0.001)
        {
            // Edge-preserving (bilateral) blur: averages similar luma, keeps eyes/hair/edges sharp.
            float2 texel = UVScale / InputSize;
            float radius = max(1.5, InputSize.y / 300.0);
            float sum = y, weight = 1;
            [unroll] for (int k = 0; k < 12; k++)
            {
                float a = k * 0.5235988;
                float2 dir = float2(cos(a), sin(a)) * texel * radius;
                [unroll] for (int ring = 1; ring <= 2; ring++)
                {
                    float s = InY.SampleLevel(Linear, ClampY(t + dir * ring), 0);
                    float d = (s - y) * 18;
                    float w = exp(-d * d) * (ring == 1 ? 1.0 : 0.6);
                    sum += s * w;
                    weight += w;
                }
            }
            y = lerp(y, sum / weight, saturate(mask));
        }
    }
    return y;
}

float2 PS_UV(VSOut i) : SV_Target
{
    float2 p;
    if (!MapToInput(i.uv, p)) return float2(0.5, 0.5);
    float2 cbcr = InUV.SampleLevel(Linear, ClampUV(p * UVScale), 0);

    if (Redness > 0.001)
    {
        float mask = SkinWeight(cbcr) * FaceMask(p);
        // Pull Cr (red difference) towards a neutral skin tone; nudge Cb slightly to stay natural.
        float excess = max(0, cbcr.y - 142.0 / 255);
        cbcr.y -= excess * Redness * mask * 0.9;
        cbcr.x += excess * Redness * mask * 0.2;
    }
    return cbcr;
}
)hlsl";

    HRESULT Compile(const char* entry, const char* profile, ComPtr<ID3DBlob>& blob)
    {
        ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "VideoEffects", nullptr, nullptr,
                                entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
        if (FAILED(hr) && errors)
        {
            OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        }
        return hr;
    }

    HRESULT CreateTexture(ID3D11Device* device, UINT w, UINT h, DXGI_FORMAT format, D3D11_USAGE usage,
                          UINT bind, UINT cpu, ComPtr<ID3D11Texture2D>& texture)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = usage;
        desc.BindFlags = bind;
        desc.CPUAccessFlags = cpu;
        texture.Reset();
        return device->CreateTexture2D(&desc, nullptr, &texture);
    }
}

HRESULT VideoEffects::Initialize(GpuDevice* gpu, UINT outputWidth, UINT outputHeight)
{
    m_ready = false;
    m_gpu = gpu;
    if (!gpu || !gpu->Device()) return E_INVALIDARG;
    ID3D11Device* device = gpu->Device();
    m_outW = outputWidth;
    m_outH = outputHeight;

    ComPtr<ID3DBlob> vs, psY, psUV;
    HRESULT hr = Compile("VS", "vs_5_0", vs);
    if (SUCCEEDED(hr)) hr = Compile("PS_Y", "ps_5_0", psY);
    if (SUCCEEDED(hr)) hr = Compile("PS_UV", "ps_5_0", psUV);
    if (FAILED(hr)) return hr;

    hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &m_vs);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(psY->GetBufferPointer(), psY->GetBufferSize(), nullptr, &m_psY);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(psUV->GetBufferPointer(), psUV->GetBufferSize(), nullptr, &m_psUV);
    if (FAILED(hr)) return hr;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sampler, &m_sampler);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb, nullptr, &m_constants);
    if (FAILED(hr)) return hr;

    hr = CreateOutputs(outputWidth, outputHeight);
    if (FAILED(hr)) return hr;

    m_ready = true;
    return S_OK;
}

HRESULT VideoEffects::CreateOutputs(UINT width, UINT height)
{
    ID3D11Device* device = m_gpu->Device();
    m_rtvY.Reset();
    m_rtvUV.Reset();
    HRESULT hr = CreateTexture(device, width, height, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, m_outY);
    if (SUCCEEDED(hr)) hr = CreateTexture(device, width / 2, height / 2, DXGI_FORMAT_R8G8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, m_outUV);
    if (SUCCEEDED(hr)) hr = CreateTexture(device, width, height, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, m_readY);
    if (SUCCEEDED(hr)) hr = CreateTexture(device, width / 2, height / 2, DXGI_FORMAT_R8G8_UNORM, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, m_readUV);
    if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(m_outY.Get(), nullptr, &m_rtvY);
    if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(m_outUV.Get(), nullptr, &m_rtvUV);
    if (FAILED(hr)) return hr;
    m_outW = width;
    m_outH = height;
    return S_OK;
}

HRESULT VideoEffects::SetOutputSize(UINT width, UINT height)
{
    if (!m_ready) return E_UNEXPECTED;
    if (width == m_outW && height == m_outH) return S_OK;
    HRESULT hr = CreateOutputs(width, height);
    if (FAILED(hr)) m_ready = false;
    return hr;
}

HRESULT VideoEffects::EnsureInput(UINT width, UINT height)
{
    if (m_input && width == m_inW && height == m_inH) return S_OK;
    ID3D11Device* device = m_gpu->Device();
    m_inputY.Reset();
    m_inputUV.Reset();
    m_inputUpload.Reset();

    HRESULT hr = CreateTexture(device, width, height, DXGI_FORMAT_NV12, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, m_input);
    if (FAILED(hr)) return hr;

    // NV12 plane views: luma as R8, interleaved chroma as R8G8 (half size).
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Format = DXGI_FORMAT_R8_UNORM;
    hr = device->CreateShaderResourceView(m_input.Get(), &srv, &m_inputY);
    if (FAILED(hr)) return hr;
    srv.Format = DXGI_FORMAT_R8G8_UNORM;
    hr = device->CreateShaderResourceView(m_input.Get(), &srv, &m_inputUV);
    if (FAILED(hr)) return hr;

    m_inW = width;
    m_inH = height;
    return S_OK;
}

HRESULT VideoEffects::Upload(const DecodedFrame& frame)
{
    ID3D11DeviceContext* context = m_gpu->Context();
    if (frame.texture)
    {
        D3D11_TEXTURE2D_DESC desc{};
        frame.texture->GetDesc(&desc);
        HRESULT hr = EnsureInput(desc.Width, desc.Height);
        if (FAILED(hr)) return hr;
        context->CopySubresourceRegion(m_input.Get(), 0, 0, 0, 0, frame.texture, frame.subresource, nullptr);
        return S_OK;
    }

    // Software-decoded frame: write it into an NV12 staging texture and copy that to the GPU.
    UINT w = (frame.width + 1) & ~1u, h = (frame.height + 1) & ~1u;
    HRESULT hr = EnsureInput(w, h);
    if (FAILED(hr)) return hr;
    if (!m_inputUpload)
    {
        hr = CreateTexture(m_gpu->Device(), w, h, DXGI_FORMAT_NV12, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_WRITE, m_inputUpload);
        if (FAILED(hr)) return hr;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(m_inputUpload.Get(), 0, D3D11_MAP_WRITE, 0, &mapped);
    if (FAILED(hr)) return hr;
    auto dst = static_cast<uint8_t*>(mapped.pData);
    for (UINT y = 0; y < frame.height; ++y)
        memcpy(dst + static_cast<size_t>(y) * mapped.RowPitch, frame.y + static_cast<LONG_PTR>(y) * frame.pitch, frame.width);
    uint8_t* dstUV = dst + static_cast<size_t>(mapped.RowPitch) * h;
    for (UINT y = 0; y < frame.height / 2; ++y)
        memcpy(dstUV + static_cast<size_t>(y) * mapped.RowPitch, frame.uv + static_cast<LONG_PTR>(y) * frame.pitch, frame.width);
    context->Unmap(m_inputUpload.Get(), 0);
    context->CopyResource(m_input.Get(), m_inputUpload.Get());
    return S_OK;
}

HRESULT VideoEffects::Process(const DecodedFrame& frame, const FrameParams& params, const OutputCallback& onOutput)
{
    if (!m_ready) return E_UNEXPECTED;
    if (frame.width < 2 || frame.height < 2) return E_INVALIDARG;
    ID3D11DeviceContext* context = m_gpu->Context();

    HRESULT hr = Upload(frame);
    if (FAILED(hr)) return hr;

    Constants c{};
    c.crop[0] = params.crop.x;
    c.crop[1] = params.crop.y;
    c.crop[2] = params.crop.w;
    c.crop[3] = params.crop.h;

    // Fit the (cropped) input aspect into the output, letterboxing if it differs (e.g. portrait).
    float inAspect = static_cast<float>(frame.width) / frame.height;
    float outAspect = static_cast<float>(m_outW) / m_outH;
    float cw = 1, ch = 1;
    if (std::abs(inAspect - outAspect) > 0.01f)
    {
        if (inAspect > outAspect) ch = outAspect / inAspect;
        else cw = inAspect / outAspect;
    }
    c.content[0] = (1 - cw) / 2;
    c.content[1] = (1 - ch) / 2;
    c.content[2] = cw;
    c.content[3] = ch;

    c.inputSize[0] = static_cast<float>(frame.width);
    c.inputSize[1] = static_cast<float>(frame.height);
    c.uvScale[0] = static_cast<float>(frame.width) / m_inW;
    c.uvScale[1] = static_cast<float>(frame.height) / m_inH;
    c.texel[0] = 1.0f / m_inW;
    c.texel[1] = 1.0f / m_inH;

    // Rotating 180 degrees = flipping both axes; mirror flips X (again).
    c.flip[0] = (params.effects.mirror != params.effects.rotate180) ? 1.0f : 0.0f;
    c.flip[1] = params.effects.rotate180 ? 1.0f : 0.0f;
    c.redness = std::clamp(params.effects.redness, 0.0f, 1.0f);
    c.smoothing = std::clamp(params.effects.smoothing, 0.0f, 1.0f);

    if (!params.hasFaceInfo)
    {
        c.faceCount = -1;
    }
    else
    {
        c.faceCount = static_cast<int>(std::min<size_t>(params.faces.size(), MaxFaces));
        for (int i = 0; i < c.faceCount; ++i)
        {
            const NormRect& f = params.faces[i];
            // Enlarge the detection box (eyebrows..chin) to cover forehead, cheeks and jaw.
            c.faces[i][0] = f.cx();
            c.faces[i][1] = f.cy() - f.h * 0.08f;
            c.faces[i][2] = f.w * 0.9f;
            c.faces[i][3] = f.h * 1.0f;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(m_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return hr;
    memcpy(mapped.pData, &c, sizeof(c));
    context->Unmap(m_constants.Get(), 0);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(m_vs.Get(), nullptr, 0);
    ID3D11Buffer* buffers[] = { m_constants.Get() };
    context->PSSetConstantBuffers(0, 1, buffers);
    ID3D11ShaderResourceView* views[] = { m_inputY.Get(), m_inputUV.Get() };
    context->PSSetShaderResources(0, 2, views);
    ID3D11SamplerState* samplers[] = { m_sampler.Get() };
    context->PSSetSamplers(0, 1, samplers);
    context->RSSetState(nullptr);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(nullptr, 0);

    D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(m_outW), static_cast<float>(m_outH), 0, 1 };
    ID3D11RenderTargetView* rtvY[] = { m_rtvY.Get() };
    context->OMSetRenderTargets(1, rtvY, nullptr);
    context->RSSetViewports(1, &viewport);
    context->PSSetShader(m_psY.Get(), nullptr, 0);
    context->Draw(3, 0);

    viewport.Width = static_cast<float>(m_outW / 2);
    viewport.Height = static_cast<float>(m_outH / 2);
    ID3D11RenderTargetView* rtvUV[] = { m_rtvUV.Get() };
    context->OMSetRenderTargets(1, rtvUV, nullptr);
    context->RSSetViewports(1, &viewport);
    context->PSSetShader(m_psUV.Get(), nullptr, 0);
    context->Draw(3, 0);

    ID3D11RenderTargetView* none[] = { nullptr };
    context->OMSetRenderTargets(1, none, nullptr);
    ID3D11ShaderResourceView* noViews[] = { nullptr, nullptr };
    context->PSSetShaderResources(0, 2, noViews);

    context->CopyResource(m_readY.Get(), m_outY.Get());
    context->CopyResource(m_readUV.Get(), m_outUV.Get());

    D3D11_MAPPED_SUBRESOURCE y{}, uv{};
    hr = context->Map(m_readY.Get(), 0, D3D11_MAP_READ, 0, &y);
    if (FAILED(hr)) return hr;
    hr = context->Map(m_readUV.Get(), 0, D3D11_MAP_READ, 0, &uv);
    if (FAILED(hr))
    {
        context->Unmap(m_readY.Get(), 0);
        return hr;
    }
    onOutput(static_cast<const uint8_t*>(y.pData), y.RowPitch, static_cast<const uint8_t*>(uv.pData), uv.RowPitch);
    context->Unmap(m_readUV.Get(), 0);
    context->Unmap(m_readY.Get(), 0);
    return S_OK;
}
