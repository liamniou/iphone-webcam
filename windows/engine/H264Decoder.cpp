#include "H264Decoder.h"
#include "GpuDevice.h"

#include <mferror.h>
#include <wmcodecdsp.h>
#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

HRESULT H264Decoder::Initialize(GpuDevice* gpu, bool allowHardware)
{
    m_decoder.Reset();
    m_gpu = gpu;
    HRESULT hr = CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_decoder));
    if (FAILED(hr)) return hr;

    ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(m_decoder->GetAttributes(&attributes)))
    {
        attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    }

    // The D3D manager must be set before the media types.
    m_hardware = allowHardware && m_gpu && SUCCEEDED(EnableHardware());

    ComPtr<IMFMediaType> input;
    hr = MFCreateMediaType(&input);
    if (FAILED(hr)) return hr;
    input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = m_decoder->SetInputType(0, input.Get(), 0);
    if (FAILED(hr)) return hr;

    hr = ConfigureOutput();
    if (FAILED(hr)) return hr;

    m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return S_OK;
}

HRESULT H264Decoder::EnableHardware()
{
    ComPtr<IMFAttributes> attributes;
    UINT32 d3d11Aware = FALSE;
    if (FAILED(m_decoder->GetAttributes(&attributes)) ||
        FAILED(attributes->GetUINT32(MF_SA_D3D11_AWARE, &d3d11Aware)) || !d3d11Aware)
    {
        return E_NOTIMPL;
    }

    HRESULT hr = m_decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(m_gpu->Manager()));
    if (FAILED(hr))
    {
        m_decoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, 0);
    }
    return hr;
}

HRESULT H264Decoder::ConfigureOutput()
{
    for (DWORD i = 0;; ++i)
    {
        ComPtr<IMFMediaType> type;
        HRESULT hr = m_decoder->GetOutputAvailableType(0, i, &type);
        if (FAILED(hr)) return hr;

        GUID subtype{};
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (subtype != MFVideoFormat_NV12) continue;

        hr = m_decoder->SetOutputType(0, type.Get(), 0);
        if (FAILED(hr)) return hr;

        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &m_allocWidth, &m_allocHeight);
        m_visibleWidth = m_allocWidth;
        m_visibleHeight = m_allocHeight;

        MFVideoArea aperture{};
        if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&aperture), sizeof(aperture), nullptr)) &&
            aperture.Area.cx > 0 && aperture.Area.cy > 0)
        {
            m_visibleWidth = static_cast<UINT32>(aperture.Area.cx);
            m_visibleHeight = static_cast<UINT32>(aperture.Area.cy);
        }

        UINT32 stride = 0;
        m_defaultStride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))
            ? static_cast<LONG>(stride) : static_cast<LONG>(m_allocWidth);

        MFT_OUTPUT_STREAM_INFO info{};
        hr = m_decoder->GetOutputStreamInfo(0, &info);
        if (FAILED(hr)) return hr;
        m_outputSize = info.cbSize;
        m_providesSamples = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        return S_OK;
    }
}

HRESULT H264Decoder::Decode(const uint8_t* data, size_t length, LONGLONG timestamp, const FrameCallback& onFrame)
{
    if (!m_decoder) return E_UNEXPECTED;

    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(length), &buffer);
    if (FAILED(hr)) return hr;
    BYTE* dst = nullptr;
    hr = buffer->Lock(&dst, nullptr, nullptr);
    if (FAILED(hr)) return hr;
    memcpy(dst, data, length);
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(length));

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) return hr;
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(timestamp);
    sample->SetSampleDuration(333'333);

    hr = m_decoder->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING)
    {
        DrainOutput(onFrame);
        hr = m_decoder->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr)) return hr;
    return DrainOutput(onFrame);
}

HRESULT H264Decoder::ReadBack(const DecodedFrame& source, const FrameCallback& onFrame)
{
    if (!source.texture) return E_INVALIDARG;
    ID3D11Device* device = m_gpu->Device();
    ID3D11DeviceContext* context = m_gpu->Context();

    D3D11_TEXTURE2D_DESC desc{};
    source.texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc{};
    if (m_staging) m_staging->GetDesc(&stagingDesc);
    if (!m_staging || stagingDesc.Width != desc.Width || stagingDesc.Height != desc.Height || stagingDesc.Format != desc.Format)
    {
        stagingDesc = {};
        stagingDesc.Width = desc.Width;
        stagingDesc.Height = desc.Height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = desc.Format;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        m_staging.Reset();
        HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &m_staging);
        if (FAILED(hr)) return hr;
    }

    // Planar NV12 is a single subresource per array slice, so this copies both planes.
    context->CopySubresourceRegion(m_staging.Get(), 0, 0, 0, 0, source.texture, source.subresource, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;

    DecodedFrame frame{};
    frame.y = static_cast<const uint8_t*>(mapped.pData);
    frame.uv = frame.y + static_cast<size_t>(mapped.RowPitch) * desc.Height;
    frame.pitch = static_cast<LONG>(mapped.RowPitch);
    frame.width = std::min<UINT32>(source.width, desc.Width);
    frame.height = std::min<UINT32>(source.height, desc.Height);
    onFrame(frame);

    context->Unmap(m_staging.Get(), 0);
    return S_OK;
}

HRESULT H264Decoder::DrainOutput(const FrameCallback& onFrame)
{
    for (;;)
    {
        MFT_OUTPUT_DATA_BUFFER output{};
        ComPtr<IMFSample> ownSample;
        if (!m_providesSamples)
        {
            ComPtr<IMFMediaBuffer> buffer;
            HRESULT hr = MFCreateMemoryBuffer(m_outputSize, &buffer);
            if (FAILED(hr)) return hr;
            hr = MFCreateSample(&ownSample);
            if (FAILED(hr)) return hr;
            ownSample->AddBuffer(buffer.Get());
            output.pSample = ownSample.Get();
        }

        DWORD status = 0;
        HRESULT hr = m_decoder->ProcessOutput(0, 1, &output, &status);
        if (output.pEvents) output.pEvents->Release();

        ComPtr<IMFSample> result;
        if (m_providesSamples) result.Attach(output.pSample);
        else result = ownSample;

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return S_OK;
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            hr = ConfigureOutput();
            if (FAILED(hr)) return hr;
            continue;
        }
        if (FAILED(hr)) return hr;
        if (!result) continue;

        ComPtr<IMFMediaBuffer> buffer;
        hr = result->GetBufferByIndex(0, &buffer);
        if (FAILED(hr)) return hr;

        if (m_hardware)
        {
            ComPtr<IMFDXGIBuffer> dxgiBuffer;
            ComPtr<ID3D11Texture2D> texture;
            if (SUCCEEDED(buffer.As(&dxgiBuffer)) && SUCCEEDED(dxgiBuffer->GetResource(IID_PPV_ARGS(&texture))))
            {
                DecodedFrame frame{};
                frame.texture = texture.Get();
                dxgiBuffer->GetSubresourceIndex(&frame.subresource);
                frame.width = m_visibleWidth;
                frame.height = m_visibleHeight;
                onFrame(frame);
                continue;
            }
        }

        buffer.Reset();
        hr = result->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) return hr;

        ComPtr<IMF2DBuffer> buffer2d;
        BYTE* scanline0 = nullptr;
        LONG pitch = 0;
        bool locked2d = SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scanline0, &pitch));
        if (!locked2d)
        {
            hr = buffer->Lock(&scanline0, nullptr, nullptr);
            if (FAILED(hr)) return hr;
            pitch = m_defaultStride;
        }

        DecodedFrame frame{};
        frame.y = scanline0;
        frame.uv = scanline0 + static_cast<LONG_PTR>(pitch) * m_allocHeight;
        frame.pitch = pitch;
        frame.width = m_visibleWidth;
        frame.height = m_visibleHeight;
        onFrame(frame);

        if (locked2d) buffer2d->Unlock2D();
        else buffer->Unlock();
    }
}
