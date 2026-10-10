#pragma once
#include <wincodec.h>
#include <vector>
#pragma comment(lib, "windowscodecs.lib")

// Embedded atlases keep the standalone executable independent of asset paths.
class News
{
    ComPtr<ID3D11ShaderResourceView> pictures_[2];
    ComPtr<ID3D11SamplerState> sampler_;
public:
    void Init(ID3D11Device* device)
    {
        ComPtr<IWICImagingFactory> factory;
        CheckHR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)), "News WIC factory failed");
        for (int i = 0; i < 2; ++i) {
            auto data = LoadBinaryResource(GetModuleHandle(nullptr), i == 0 ? IDR_NEWS_ILLUSTRATED : IDR_NEWS_REALISTIC);
            ComPtr<IWICStream> stream;
            CheckHR(factory->CreateStream(&stream), "News stream failed");
            CheckHR(stream->InitializeFromMemory((BYTE*)data.data, data.size), "News memory stream failed");
            ComPtr<IWICBitmapDecoder> decoder;
            CheckHR(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder), "News decode failed");
            ComPtr<IWICBitmapFrameDecode> frame;
            CheckHR(decoder->GetFrame(0, &frame), "News frame failed");
            ComPtr<IWICFormatConverter> converter;
            CheckHR(factory->CreateFormatConverter(&converter), "News converter failed");
            CheckHR(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "News format failed");
            UINT w, h;
            CheckHR(converter->GetSize(&w, &h), "News size failed");
            std::vector<BYTE> pixels(size_t(w) * h * 4);
            CheckHR(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data()), "News pixels failed");
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = w; desc.Height = h; desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial = {pixels.data(), w * 4, 0};
            ComPtr<ID3D11Texture2D> texture;
            CheckHR(device->CreateTexture2D(&desc, &initial, &texture), "News texture failed");
            CheckHR(device->CreateShaderResourceView(texture.Get(), nullptr, &pictures_[i]), "News view failed");
        }
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        CheckHR(device->CreateSamplerState(&desc, &sampler_), "News sampler failed");
    }
    void Bind(ID3D11DeviceContext* context, bool realistic)
    {
        context->PSSetShaderResources(0, 1, pictures_[realistic ? 1 : 0].GetAddressOf());
        context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    }
};
