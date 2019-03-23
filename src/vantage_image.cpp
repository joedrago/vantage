#include "vantage.h"

#include <dxgi1_6.h>
#include <directxmath.h>
#pragma comment(lib, "d3d11.lib")
using namespace DirectX;

static XMVECTORF32 backgroundColor = { { { 0.000000000f, 0.000000000f, 0.000000000f, 1.000000000f } } };

void Vantage::unloadImage(bool unloadColoristImage)
{
    if (image_) {
        image_->Release();
        image_ = nullptr;
    }
    hdrImage_ = false;

    if (unloadColoristImage && coloristImage_) {
        clImageDestroy(coloristContext_, coloristImage_);
        coloristImage_ = nullptr;
    }
}

void Vantage::loadImage(const char * filename)
{
    unloadImage();

    coloristImage_ = clContextRead(coloristContext_, filename, nullptr, nullptr);
    prepareImage();
}

void Vantage::prepareImage()
{
    unloadImage(false);

    if (coloristImage_) {
        clProfilePrimaries primaries;
        clProfileCurve curve;
        int maxLuminance;

        if (hdrActive_) {
            clContextGetStockPrimaries(coloristContext_, "bt2020", &primaries);
            curve.type = CL_PCT_PQ;
            curve.implicitScale = 1.0f;
            curve.gamma = 1.0f;
            maxLuminance = 10000;
            hdrImage_ = true;
        } else {
            clContextGetStockPrimaries(coloristContext_, "bt709", &primaries);
            curve.type = CL_PCT_GAMMA;
            curve.implicitScale = 1.0f;
            curve.gamma = 2.2f;
            maxLuminance = 100;
            hdrImage_ = false;
        }
        clProfile * profile = clProfileCreate(coloristContext_, &primaries, &curve, maxLuminance, NULL);
        clImage * convertedImage = clImageConvert(coloristContext_, coloristImage_, coloristContext_->params.jobs, 16, profile, CL_TONEMAP_AUTO);
        clProfileDestroy(coloristContext_, profile);

        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = static_cast <UINT> (convertedImage->width);
        desc.Height = static_cast <UINT> (convertedImage->height);
        desc.MipLevels = static_cast <UINT> (1);
        desc.ArraySize = static_cast <UINT> (1);
        desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA initData;
        ZeroMemory(&initData, sizeof(initData));
        initData.pSysMem = (const void *)convertedImage->pixels;
        initData.SysMemPitch = convertedImage->width * 4 * 2;
        initData.SysMemSlicePitch = static_cast <UINT> (convertedImage->width * convertedImage->height * 4 * 2);

        ID3D11Texture2D * tex = nullptr;
        HRESULT hr = device_->CreateTexture2D(&desc, &initData, &tex);
        if (SUCCEEDED(hr) && (tex != nullptr)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
            memset(&SRVDesc, 0, sizeof(SRVDesc));
            SRVDesc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
            SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            SRVDesc.Texture2D.MipLevels = 1;

            hr = device_->CreateShaderResourceView(tex, &SRVDesc, &image_);
            if (FAILED(hr)) {
                tex->Release();
                image_ = nullptr;
                unloadImage();
                return;
            }
        }
        tex->Release();
        clImageDestroy(coloristContext_, convertedImage);
    }
}

void Vantage::render()
{
    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);
    context_->ClearRenderTargetView(renderTarget_, backgroundColor);

    if (image_ && (hdrActive_ != hdrImage_)) {
        prepareImage();
    }

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    float clientW = (float)clientRect.right;
    float clientH = (float)clientRect.bottom;

    if (coloristImage_ && image_ && (clientW > 0.0f) && (clientH > 0.0f)) {
        float imageW = (float)coloristImage_->width;
        float imageH = (float)coloristImage_->height;

        // TODO: Move math into mouse handling code somewhere, and only
        //       record/advertise posX/posY/reqW/reqH to this
        float clientRatio = clientW / clientH;
        float imageRatio = imageW / imageH;
        float posX, posY, reqW, reqH;
        if (clientRatio < imageRatio) {
            reqW = clientW;
            reqH = clientW / imageW * imageH;
            posX = 0.0f;
            posY = (clientH - reqH) / 2;
        } else {
            reqH = clientH;
            reqW = clientH / imageH * imageW;
            posX = (clientW - reqW) / 2;
            posY = 0.0f;
        }

        float scaleX = reqW / imageW;
        float scaleY = reqH / imageH;
        ConstantBuffer cb;
        cb.transform = XMMatrixIdentity();
        cb.transform *= XMMatrixScaling(imageW * scaleX, imageH * scaleY, 1.0f);
        cb.transform *= XMMatrixTranslation(posX, posY, 0.0f);
        cb.transform *= XMMatrixOrthographicOffCenterRH(0.0f, clientW, clientH, 0.0f, -1.0f, 1.0f);
        cb.params = XMFLOAT4(
            1.0f, // hdrActive_ ? 1.0f : 0.0f,
            0.0f, // forceSDR_ ? 1.0f : 0.0f,
            0.0f, // tonemap_ ? 1.0f : 0.0f,
            0);
        context_->UpdateSubresource(constantBuffer_, 0, nullptr, &cb, 0, 0);

        context_->VSSetShader(vertexShader_, nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, &constantBuffer_);
        context_->PSSetShader(pixelShader_, nullptr, 0);
        context_->PSSetConstantBuffers(0, 1, &constantBuffer_);
        context_->PSSetShaderResources(0, 1, &image_);
        context_->PSSetSamplers(0, 1, &sampler_);
        context_->DrawIndexed(6, 0, 0);
    }
    swapChain_->Present(1, 0);
}
