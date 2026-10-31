#pragma once

#include <windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <cstdint>
#include <functional>
#include <string>

class GpuDevice;

// A decoded NV12 frame, either in system memory (y/uv/pitch) or on the GPU (texture/subresource).
struct DecodedFrame
{
    const uint8_t* y = nullptr;
    const uint8_t* uv = nullptr;
    LONG pitch = 0;
    ID3D11Texture2D* texture = nullptr; // NV12 texture (array) owned by the decoder; valid during the callback
    UINT subresource = 0;
    UINT32 width = 0;  // visible width
    UINT32 height = 0; // visible height
};

// Wraps the built-in Microsoft H.264 decoder MFT; input is Annex B access units, output NV12.
// Uses GPU (DXVA/D3D11) decoding on the shared GpuDevice when available, which is needed for 4K at 30 fps.
class H264Decoder
{
public:
    using FrameCallback = std::function<void(const DecodedFrame&)>;

    HRESULT Initialize(GpuDevice* gpu, bool allowHardware);
    HRESULT Decode(const uint8_t* data, size_t length, LONGLONG timestamp, const FrameCallback& onFrame);
    bool IsHardware() const { return m_hardware; }

    // Copies a GPU frame to system memory (used when the effects pipeline is unavailable).
    HRESULT ReadBack(const DecodedFrame& frame, const FrameCallback& onFrame);

private:
    HRESULT EnableHardware();
    HRESULT ConfigureOutput();
    HRESULT DrainOutput(const FrameCallback& onFrame);

    Microsoft::WRL::ComPtr<IMFTransform> m_decoder;
    GpuDevice* m_gpu = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;
    bool m_hardware = false;
    UINT32 m_allocWidth = 0;
    UINT32 m_allocHeight = 0;
    UINT32 m_visibleWidth = 0;
    UINT32 m_visibleHeight = 0;
    LONG m_defaultStride = 0;
    DWORD m_outputSize = 0;
    bool m_providesSamples = false;
};
