#include "GpuDevice.h"

#include <d3d11_4.h>
#include <dxgi1_6.h>

using Microsoft::WRL::ComPtr;

HRESULT GpuDevice::Create()
{
    // Pick the high-performance GPU explicitly; virtual display adapters may be enumerated first.
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
        {
            return E_FAIL;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, &context);
        if (FAILED(hr)) continue;

        // NV12 must be usable as a shader input for the effects pipeline.
        UINT support = 0;
        if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_NV12, &support)) ||
            !(support & D3D11_FORMAT_SUPPORT_TEXTURE2D) || !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        {
            continue;
        }

        // The decoder (Media Foundation threads) and the engine thread share the immediate context.
        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(device.As(&multithread))) multithread->SetMultithreadProtected(TRUE);

        UINT resetToken = 0;
        ComPtr<IMFDXGIDeviceManager> manager;
        hr = MFCreateDXGIDeviceManager(&resetToken, &manager);
        if (FAILED(hr)) return hr;
        hr = manager->ResetDevice(device.Get(), resetToken);
        if (FAILED(hr)) continue;

        m_device = device;
        m_context = context;
        m_manager = manager;
        m_adapterName = desc.Description;
        return S_OK;
    }
}
