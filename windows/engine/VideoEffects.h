#pragma once

#include "Effects.h"
#include "H264Decoder.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <functional>

class GpuDevice;

// GPU pass that turns a decoded NV12 frame into the virtual camera's NV12 output (format chosen by the
// user): framing crop + scaling, letterboxing, mirror/rotate and face retouching (skin smoothing on
// luma, redness reduction on chroma), all in two pixel-shader passes.
class VideoEffects
{
public:
    // Planes of the final output, valid during the callback.
    using OutputCallback = std::function<void(const uint8_t* y, UINT yPitch, const uint8_t* uv, UINT uvPitch)>;

    HRESULT Initialize(GpuDevice* gpu, UINT outputWidth, UINT outputHeight);
    // Changes the output size (call from the processing thread).
    HRESULT SetOutputSize(UINT width, UINT height);
    bool IsReady() const { return m_ready; }
    HRESULT Process(const DecodedFrame& frame, const FrameParams& params, const OutputCallback& onOutput);

private:
    HRESULT CreateOutputs(UINT width, UINT height);
    HRESULT EnsureInput(UINT width, UINT height);
    HRESULT Upload(const DecodedFrame& frame);

    GpuDevice* m_gpu = nullptr;
    bool m_ready = false;
    UINT m_outW = 0, m_outH = 0;
    UINT m_inW = 0, m_inH = 0;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psY;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_psUV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constants;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_input;       // NV12, shader-readable copy of the decoded frame
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_inputUpload; // NV12 staging for software-decoded frames
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_inputY;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_inputUV;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_outY;  // R8, full resolution
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_outUV; // R8G8, half resolution
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtvY;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtvUV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_readY;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_readUV;
};
