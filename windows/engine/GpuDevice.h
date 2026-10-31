#pragma once

#include <windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <string>

// One D3D11 device on the high-performance GPU, shared by the hardware H.264 decoder (through the
// DXGI device manager) and the effects pipeline, so decoded frames never leave the GPU until the
// final 4K output is read back.
class GpuDevice
{
public:
    HRESULT Create();

    ID3D11Device* Device() const { return m_device.Get(); }
    ID3D11DeviceContext* Context() const { return m_context.Get(); }
    IMFDXGIDeviceManager* Manager() const { return m_manager.Get(); }
    const std::wstring& AdapterName() const { return m_adapterName; }

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_manager;
    std::wstring m_adapterName;
};
