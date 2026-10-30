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

struct DecodedFrame
{
    const uint8_t* y;
    const uint8_t* uv;
    LONG pitch;
    UINT32 width;  // visible width
    UINT32 height; // visible height
};

// Wraps the built-in Microsoft H.264 decoder MFT; input is Annex B access units, output NV12.
// Uses GPU (DXVA/D3D11) decoding when available, which is needed for 4K at 30 fps.
class H264Decoder
{
public:
    using FrameCallback = std::function<void(const DecodedFrame&)>;

    HRESULT Initialize(bool allowHardware);
    HRESULT Decode(const uint8_t* data, size_t length, LONGLONG timestamp, const FrameCallback& onFrame);
    bool IsHardware() const { return m_hardware; }
    const std::wstring& AdapterName() const { return m_adapterName; }

private:
    HRESULT EnableHardware();
    HRESULT ConfigureOutput();
    HRESULT DrainOutput(const FrameCallback& onFrame);
    HRESULT DeliverTexture(IMFMediaBuffer* buffer, const FrameCallback& onFrame);

    Microsoft::WRL::ComPtr<IMFTransform> m_decoder;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_deviceManager;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;
    bool m_hardware = false;
    std::wstring m_adapterName;
    UINT32 m_allocWidth = 0;
    UINT32 m_allocHeight = 0;
    UINT32 m_visibleWidth = 0;
    UINT32 m_visibleHeight = 0;
    LONG m_defaultStride = 0;
    DWORD m_outputSize = 0;
    bool m_providesSamples = false;
};
