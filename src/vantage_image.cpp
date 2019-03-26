#include "vantage.h"

#include <algorithm>
#include <shlwapi.h>

#include <dxgi1_6.h>
#include <directxmath.h>
#pragma comment(lib, "d3d11.lib")
using namespace DirectX;

static XMVECTORF32 backgroundColor = { { { 0.000000000f, 0.000000000f, 0.000000000f, 1.000000000f } } };
static const float MAX_SCALE = 32.0f;

void Vantage::unloadImage(bool unloadColoristImage)
{
    if (image_) {
        image_->Release();
        image_ = nullptr;
    }
    imageHDR_ = false;

    if (unloadColoristImage && coloristImage_) {
        clImageDestroy(coloristContext_, coloristImage_);
        coloristImage_ = nullptr;
    }

    imageInfoX_ = -1;
    imageInfoY_ = -1;
}

// This could potentially use clFormatDetect() instead, but that'd cause a lot of header reads.
static bool isImageFile(const char * filename)
{
    const char * ext = strrchr(filename, '.');
    if (ext) {
        if (!strcmp(ext, ".apg")) return true;
        if (!strcmp(ext, ".avif")) return true;
        if (!strcmp(ext, ".bmp")) return true;
        if (!strcmp(ext, ".jpg")) return true;
        if (!strcmp(ext, ".jp2")) return true;
        if (!strcmp(ext, ".j2k")) return true;
        if (!strcmp(ext, ".png")) return true;
        if (!strcmp(ext, ".tiff")) return true;
        if (!strcmp(ext, ".webp")) return true;
    }
    return false;
}

static bool sortAlphabeticallyIgnoringCase(const std::string & a, const std::string & b) { return _stricmp(a.c_str(), b.c_str()) < 0; }

void Vantage::loadImage(const char * filename)
{
    char fullFilename[MAX_PATH];
    GetFullPathNameA(filename, MAX_PATH, fullFilename, nullptr);
    char directory[MAX_PATH];
    memcpy(directory, fullFilename, MAX_PATH);
    PathRemoveFileSpec(directory);
    std::string wildcard = directory;
    wildcard += "\\*";

    imageFiles_.clear();

    WIN32_FIND_DATA wfd;
    HANDLE hFind = FindFirstFile(wildcard.c_str(), &wfd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wfd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) {
                continue;
            }
            std::string foundFilenameStr = directory;
            foundFilenameStr += "\\";
            foundFilenameStr += wfd.cFileName;
            char foundFilename[MAX_PATH];
            GetFullPathNameA(foundFilenameStr.c_str(), MAX_PATH, foundFilename, nullptr);
            if (isImageFile(foundFilename)) {
                imageFiles_.push_back(foundFilename);
            }
        } while (FindNextFile(hFind, &wfd));
    }

    std::sort(imageFiles_.begin(), imageFiles_.end(), sortAlphabeticallyIgnoringCase);

    int requestedFilenameIndex = -1;
    int index = 0;
    for (auto it = imageFiles_.begin(); it != imageFiles_.end(); ++it, ++index) {
        if (!strcmp(it->c_str(), fullFilename)) {
            requestedFilenameIndex = index;
        }
    }

    if (requestedFilenameIndex == -1) {
        // Somehow the original file wasn't found, tack it onto the end of the list
        imageFiles_.push_back(fullFilename);
        requestedFilenameIndex = (int)imageFiles_.size() - 1;
    }

    imageFileIndex_ = requestedFilenameIndex;
    loadImage(0);
}

void Vantage::loadImage(int offset)
{
    unloadImage();

    if (imageFiles_.empty()) {
        return;
    }
    int loadIndex = imageFileIndex_ + offset;
    if (loadIndex < 0) {
        loadIndex = (int)imageFiles_.size() - 1;
    }
    if (loadIndex >= imageFiles_.size()) {
        loadIndex = 0;
    }
    imageFileIndex_ = loadIndex;

    const char * filename = imageFiles_[imageFileIndex_].c_str();
    coloristImage_ = clContextRead(coloristContext_, filename, nullptr, nullptr);
    prepareImage();

    resetImagePos();

    clearOverlay();
    if (coloristImage_) {
        appendOverlay("[%d/%d] Loaded: %s", imageFileIndex_ + 1, imageFiles_.size(), filename);
    } else {
        appendOverlay("[%d/%d] Failed to load: %s", imageFileIndex_ + 1, imageFiles_.size(), filename);
    }

    render();
}

void Vantage::prepareImage()
{
    unloadImage(false);
    kickOverlay();

    if (coloristImage_) {
        unsigned int sdrWhite = sdrWhiteLevel();
        clProfilePrimaries primaries;
        clProfileCurve curve;

        bool defaultSrcLuminance = false;
        int srcLuminance = 0;
        clProfileQuery(coloristContext_, coloristImage_->profile, NULL, NULL, &srcLuminance);
        if (srcLuminance == 0) {
            defaultSrcLuminance = true;
            clProfileSetLuminance(coloristContext_, coloristImage_->profile, sdrWhite);
        }

        int dstLuminance = 10000;
        if (hdrActive_) {
            clContextGetStockPrimaries(coloristContext_, "bt2020", &primaries);
            curve.type = CL_PCT_PQ;
            curve.implicitScale = 1.0f;
            curve.gamma = 1.0f;
            dstLuminance = 10000;
            imageWhiteLevel_ = sdrWhite;
            imageHDR_ = true;
        } else {
            clContextGetStockPrimaries(coloristContext_, "bt709", &primaries);
            curve.type = CL_PCT_GAMMA;
            curve.implicitScale = 1.0f;
            curve.gamma = 2.2f;
            dstLuminance = sdrWhite;
            imageWhiteLevel_ = sdrWhite;
            imageHDR_ = false;
        }
        clProfile * profile = clProfileCreate(coloristContext_, &primaries, &curve, dstLuminance, NULL);
        clImage * convertedImage = clImageConvert(coloristContext_, coloristImage_, coloristContext_->params.jobs, 16, profile, CL_TONEMAP_AUTO);
        clProfileDestroy(coloristContext_, profile);

        if (defaultSrcLuminance) {
            // Set it back so we remember to use new SDR values in the future
            clProfileSetLuminance(coloristContext_, coloristImage_->profile, 0);
        }

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

void Vantage::calcImageSize()
{
    if (!coloristImage_) {
        imagePosW_ = 1;
        imagePosH_ = 1;
        return;
    }

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    float clientW = (float)clientRect.right;
    float clientH = (float)clientRect.bottom;

    float imageW = (float)coloristImage_->width;
    float imageH = (float)coloristImage_->height;
    float clientRatio = clientW / clientH;
    float imageRatio = imageW / imageH;

    if (clientRatio < imageRatio) {
        imagePosW_ = clientW;
        imagePosH_ = clientW / imageW * imageH;
    } else {
        imagePosH_ = clientH;
        imagePosW_ = clientH / imageH * imageW;
    }

    imagePosW_ *= imagePosS_;
    imagePosH_ *= imagePosS_;
}

void Vantage::calcCenteredImagePos(float & posX, float & posY)
{
    if (!coloristImage_) {
        posX = 0;
        posY = 0;
        return;
    }

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    float clientW = (float)clientRect.right;
    float clientH = (float)clientRect.bottom;
    posX = (clientW - imagePosW_) / 2.0f;
    posY = (clientH - imagePosH_) / 2.0f;
}

void Vantage::resetImagePos()
{
    imagePosS_ = 1.0f;
    calcImageSize();
    calcCenteredImagePos(imagePosX_, imagePosY_);
}

void Vantage::mouseLeftDown(int x, int y)
{
    // appendOverlay("mouseLeftDown(%d, %d)", x, y);
    dragging_ = true;
    dragStartX_ = x;
    dragStartY_ = y;
}

void Vantage::mouseLeftUp(int x, int y)
{
    // appendOverlay("mouseLeftUp(%d, %d)", x, y);
    dragging_ = false;
}

void Vantage::mouseMove(int x, int y)
{
    // appendOverlay("mouseMove(%d, %d)", x, y);
    if (dragging_) {
        float dx = (float)(x - dragStartX_);
        float dy = (float)(y - dragStartY_);
        imagePosX_ += dx;
        imagePosY_ += dy;

        clampImagePos();

        dragStartX_ = x;
        dragStartY_ = y;
    }
}

void Vantage::mouseSetPos(int x, int y)
{
    int right = (int)(imagePosX_ + imagePosW_);
    int bottom = (int)(imagePosY_ + imagePosH_);

    kickOverlay();

    if (!coloristImage_ || (x < imagePosX_) || (y < imagePosY_) || (x >= right) || (y >= bottom)) {
        imageInfoX_ = -1;
        imageInfoY_ = -1;
    } else {
        imageInfoX_ = (int)(((float)x - imagePosX_) * ((float)coloristImage_->width / imagePosW_));
        imageInfoY_ = (int)(((float)y - imagePosY_) * ((float)coloristImage_->height / imagePosH_));
        clImageDebugDumpPixel(coloristContext_, coloristImage_, imageInfoX_, imageInfoY_, &pixelInfo_);
    }
}

void Vantage::mouseLeftDoubleClick(int x, int y)
{
    // appendOverlay("mouseLeftDoubleClick(%d, %d)", x, y);

    const float scaleTiers[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
    const int scaleTierCount = sizeof(scaleTiers) / sizeof(scaleTiers[0]);
    int scaleIndex = 0;
    for (int i = 0; i < scaleTierCount; ++i) {
        if (imagePosS_ < scaleTiers[i]) {
            break;
        }
        scaleIndex = i;
    }
    scaleIndex = (scaleIndex + 1) % scaleTierCount;
    imagePosS_ = scaleTiers[scaleIndex];

    float normalizedImagePosX = (x - imagePosX_) / imagePosW_;
    float normalizedImagePosY = (y - imagePosY_) / imagePosH_;
    calcImageSize();
    imagePosX_ = (float)x - normalizedImagePosX * imagePosW_;
    imagePosY_ = (float)y - normalizedImagePosY * imagePosH_;
    clampImagePos();
}

void Vantage::mouseWheel(int x, int y, int delta)
{
    // appendOverlay("mouseWheel(%d, %d, %d)", x, y, delta);
    imagePosS_ += (float)delta / 240.0f;
    if (imagePosS_ < 1.0f) {
        imagePosS_ = 1.0f;
    }
    if (imagePosS_ > MAX_SCALE) {
        imagePosS_ = MAX_SCALE;
    }

    float normalizedImagePosX = (x - imagePosX_) / imagePosW_;
    float normalizedImagePosY = (y - imagePosY_) / imagePosH_;
    calcImageSize();
    imagePosX_ = (float)x - normalizedImagePosX * imagePosW_;
    imagePosY_ = (float)y - normalizedImagePosY * imagePosH_;
    clampImagePos();
}

void Vantage::clampImagePos()
{
    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    float clientW = (float)clientRect.right;
    float clientH = (float)clientRect.bottom;

    float centerX, centerY;
    calcCenteredImagePos(centerX, centerY);

    if (imagePosW_ < clientW) {
        imagePosX_ = centerX;
    } else {
        // clamp to fit in screen bounds
        if (imagePosX_ > 0) {
            imagePosX_ = 0;
        }
        if ((imagePosX_ + imagePosW_) < clientW) {
            imagePosX_ = clientW - imagePosW_;
        }
    }

    if (imagePosH_ < clientH) {
        imagePosY_ = centerY;
    } else {
        if (imagePosY_ > 0) {
            imagePosY_ = 0;
        }
        if ((imagePosY_ + imagePosH_) < clientH) {
            imagePosY_ = clientH - imagePosH_;
        }
    }
}

// SMPTE ST.2084: https://ieeexplore.ieee.org/servlet/opac?punumber=7291450

static const float PQ_C1 = 0.8359375;       // 3424.0 / 4096.0
static const float PQ_C2 = 18.8515625;      // 2413.0 / 4096.0 * 32.0
static const float PQ_C3 = 18.6875;         // 2392.0 / 4096.0 * 32.0
static const float PQ_M1 = 0.1593017578125; // 2610.0 / 4096.0 / 4.0
static const float PQ_M2 = 78.84375;        // 2523.0 / 4096.0 * 128.0

#if 0
// SMPTE ST.2084: Equation 4.1
// L = ( (max(N^(1/m2) - c1, 0)) / (c2 - c3*N^(1/m2)) )^(1/m1)
static float PQ_EOTF(float N)
{
    float N1m2 = powf(N, 1 / PQ_M2);
    float N1m2c1 = N1m2 - PQ_C1;
    if (N1m2c1 < 0.0f)
        N1m2c1 = 0.0f;
    float c2c3N1m2 = PQ_C2 - (PQ_C3 * N1m2);
    return powf(N1m2c1 / c2c3N1m2, 1 / PQ_M1);
}
#endif

// SMPTE ST.2084: Equation 5.2
// N = ( (c1 + (c2 * L^m1)) / (1 + (c3 * L^m1)) )^m2
static float PQ_OETF(float L)
{
    float Lm1 = powf(L, PQ_M1);
    float c2Lm1 = PQ_C2 * Lm1;
    float c3Lm1 = PQ_C3 * Lm1;
    return powf((PQ_C1 + c2Lm1) / (1 + c3Lm1), PQ_M2);
}

void Vantage::render()
{
    unsigned int sdrWhite = sdrWhiteLevel();

    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);
    context_->ClearRenderTargetView(renderTarget_, backgroundColor);

    if (image_ && ((hdrActive_ != imageHDR_) || (imageWhiteLevel_ != sdrWhite))) {
        prepareImage();
    }

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    float clientW = (float)clientRect.right;
    float clientH = (float)clientRect.bottom;

    if (coloristImage_ && image_ && (clientW > 0.0f) && (clientH > 0.0f)) {
        float imageW = (float)coloristImage_->width;
        float imageH = (float)coloristImage_->height;

        float scaleX = imagePosW_ / imageW;
        float scaleY = imagePosH_ / imageH;
        ConstantBuffer cb;
        cb.transform = XMMatrixIdentity();
        cb.transform *= XMMatrixScaling(imageW * scaleX, imageH * scaleY, 1.0f);
        cb.transform *= XMMatrixTranslation(imagePosX_, imagePosY_, 0.0f);
        cb.transform *= XMMatrixOrthographicOffCenterRH(0.0f, clientW, clientH, 0.0f, -1.0f, 1.0f);
        cb.params = XMFLOAT4(
            1.0f, // hdrActive_ ? 1.0f : 0.0f,
            0.0f, // forceSDR_ ? 1.0f : 0.0f,
            0.0f, // tonemap_ ? 1.0f : 0.0f,
            0);
        context_->UpdateSubresource(constantBuffer_, 0, nullptr, &cb, 0, 0);

        UINT stride = sizeof( SimpleVertex );
        UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
        context_->IASetInputLayout(vertexLayout_);
        context_->IASetIndexBuffer(indexBuffer_, DXGI_FORMAT_R16_UINT, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context_->VSSetShader(vertexShader_, nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, &constantBuffer_);
        context_->PSSetShader(pixelShader_, nullptr, 0);
        context_->PSSetConstantBuffers(0, 1, &constantBuffer_);
        context_->PSSetShaderResources(0, 1, &image_);
        context_->PSSetSamplers(0, 1, &sampler_);
        context_->DrawIndexed(6, 0, 0);
    }

    static const unsigned int overlayDuration = 5000;
    static const unsigned int overlayFade = 1000;
    DWORD now = GetTickCount();
    DWORD dt = now - overlayTick_;
    if (!overlay_.empty() && (dt < overlayDuration)) {
        beginText();

        int i = 0;
        float lum = 1.0f;
        if (dt > (overlayDuration - overlayFade)) {
            lum = (overlayFade - (dt - (overlayDuration - overlayFade))) / 1000.0f;
        }

        if (hdrActive_) {
            // assume lum is SDR white level with a ~2.2 gamma, convert to PQ
            lum = powf(lum, 2.2f);
            lum *= (float)sdrWhiteLevel() / 10000.0f;
            lum = PQ_OETF(lum);
        }

        char buffer[128];
        sprintf(buffer, "SDR: %d nits", sdrWhiteLevel());
        drawText(buffer, 10, clientH - 40.0f, lum, lum, lum, lum);

        if (coloristImage_ && (imageInfoX_ != -1) && (imageInfoY_ != -1)) {
            sprintf(buffer, "RAW (%d,%d): (%d,%d,%d,%d)", imageInfoX_, imageInfoY_, (int)pixelInfo_.rawR, (int)pixelInfo_.rawG, (int)pixelInfo_.rawB, (int)pixelInfo_.rawA);
            drawText(buffer, 10, clientH - 20.0f, lum, lum, lum, lum);
        }

        for (auto it = overlay_.begin(); it != overlay_.end(); ++it, ++i) {
            drawText(it->c_str(), 10, 10.0f + (i * 20.0f), lum, lum, lum, lum);
        }

        endText();
    }

    swapChain_->Present(1, 0);
}
