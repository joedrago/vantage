#include "vantage.h"

#include <algorithm>
#include <shlwapi.h>

#include <dxgi1_6.h>
#include <directxmath.h>
#pragma comment(lib, "d3d11.lib")
using namespace DirectX;

static XMVECTORF32 backgroundColor = { { { 0.000000000f, 0.000000000f, 0.000000000f, 1.000000000f } } };
static const float MAX_SCALE = 32.0f;

// #define CLAMP_TO_SCREEN_BOUNDS

void Vantage::unloadImage(bool unloadColoristImage)
{
    if (image_) {
        image_->Release();
        image_ = nullptr;
    }
    imageHDR_ = false;

    if (unloadColoristImage) {
        if (coloristImage_) {
            clImageDestroy(coloristContext_, coloristImage_);
            coloristImage_ = nullptr;
        }
        if (coloristImage2_) {
            clImageDestroy(coloristContext_, coloristImage2_);
            coloristImage2_ = nullptr;
        }
        if (coloristImageDiff_) {
            clImageDiffDestroy(coloristContext_, coloristImageDiff_);
            coloristImageDiff_ = nullptr;
        }
        if (coloristImageHighlight_) {
            clImageDestroy(coloristContext_, coloristImageHighlight_);
            coloristImageHighlight_ = nullptr;
        }
        if (coloristHighlightInfo_) {
            clImageSRGBHighlightPixelInfoDestroy(coloristContext_, coloristHighlightInfo_);
            coloristHighlightInfo_ = nullptr;
        }
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
        if (!strcmp(ext, ".jpeg")) return true;
        if (!strcmp(ext, ".jp2")) return true;
        if (!strcmp(ext, ".j2k")) return true;
        if (!strcmp(ext, ".png")) return true;
        if (!strcmp(ext, ".tif")) return true;
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

void Vantage::loadDiff(const char * filename1, const char * filename2)
{
    unloadImage();

    const char * fn1 = nullptr;
    const char * fn2 = nullptr;

    if (filename1[0] && filename2[0]) {
        // Throw everything away and load these two images
        fn1 = filename1;
        fn2 = filename2;
    } else if (!imageFiles_.empty() && filename1[0]) {
        // Reload the "current" image and diff against this filename
        fn1 = imageFiles_[imageFileIndex_].c_str();
        fn2 = filename1;
    }

    clearOverlay();

    const char * failureReason = nullptr;
    coloristImageFileSize_ = clFileSize(fn1);
    coloristImageFileSize2_ = clFileSize(fn2);
    coloristImage_ = clContextRead(coloristContext_, fn1, nullptr, nullptr);
    coloristImage2_ = clContextRead(coloristContext_, fn2, nullptr, nullptr);
    if (!coloristImage_ || !coloristImage2_) {
        failureReason = "Both failed to load";
    } else if (!coloristImage_) {
        failureReason = "First failed to load";
    } else if (!coloristImage2_) {
        failureReason = "Second failed to load";
    } else if ((coloristImage_->width != coloristImage2_->width) || (coloristImage_->height != coloristImage2_->height)) {
        failureReason = "Dimensions mismatch";
    }

    if (failureReason) {
        unloadImage();
        appendOverlay("Failed to load diff (%s):", failureReason);
        appendOverlay("* %s", fn1);
        appendOverlay("* %s", fn2);
        imageFiles_.clear();
        return;
    }

    appendOverlay("Loaded diff:");
    appendOverlay("* %s", fn1);
    appendOverlay("* %s", fn2);

    diffMode_ = DIFFMODE_SHOWDIFF;
    diffIntensity_ = DIFFINTENSITY_BRIGHT;
    imageFiles_.clear();
    prepareImage();
    resetImagePos();
    render();
}

void Vantage::loadImage(int offset)
{
    if (imageFiles_.empty()) {
        return;
    }

    unloadImage();

    int loadIndex = imageFileIndex_ + offset;
    if (loadIndex < 0) {
        loadIndex = (int)imageFiles_.size() - 1;
    }
    if (loadIndex >= imageFiles_.size()) {
        loadIndex = 0;
    }
    imageFileIndex_ = loadIndex;

    const char * filename = imageFiles_[imageFileIndex_].c_str();
    coloristImageFileSize_ = clFileSize(filename);
    coloristImageFileSize2_ = 0;
    coloristImage_ = clContextRead(coloristContext_, filename, nullptr, nullptr);

    diffMode_ = DIFFMODE_SHOW1;

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

    clImage * srcImage = nullptr;

    unsigned int sdrWhite = sdrWhiteLevel();
    coloristContext_->defaultLuminance = sdrWhite;

    if (coloristImage_ && coloristImage2_) {
        if (!clProfileMatches(coloristContext_, coloristImage_->profile, coloristImage2_->profile) || (coloristImage_->depth != coloristImage2_->depth)) {
            clImage * converted = clImageConvert(coloristContext_, coloristImage2_, coloristContext_->params.jobs, coloristImage_->depth, coloristImage_->profile, CL_TONEMAP_OFF);
            clImageDestroy(coloristContext_, coloristImage2_);
            coloristImage2_ = converted;

            if (coloristImageDiff_) {
                clImageDiffDestroy(coloristContext_, coloristImageDiff_);
                coloristImageDiff_ = nullptr;
            }
        }

        if (coloristImageDiff_) {
            clImageDiffUpdate(coloristContext_, coloristImageDiff_, diffThreshold_);
        } else {
            float minIntensity = 0.0f;
            switch (diffIntensity_) {
                case DIFFINTENSITY_ORIGINAL:
                    minIntensity = 0.0f;
                    break;
                case DIFFINTENSITY_BRIGHT:
                    minIntensity = 0.1f;
                    break;
                case DIFFINTENSITY_DIFFONLY:
                    minIntensity = 1.0f;
                    break;
            }

            coloristImageDiff_ = clImageDiffCreate(coloristContext_, coloristImage_, coloristImage2_, coloristContext_->params.jobs, minIntensity, diffThreshold_);
        }

        switch (diffMode_) {
            case DIFFMODE_SHOW1:
                srcImage = coloristImage_;
                break;
            case DIFFMODE_SHOW2:
                srcImage = coloristImage2_;
                break;
            case DIFFMODE_SHOWDIFF:
                srcImage = coloristImageDiff_->image;
                break;
        }
    } else {
        // Just show an image like normal
        srcImage = coloristImage_;
    }

    if ((diffMode_ != DIFFMODE_SHOWDIFF) && srgbHighlight_) {
        if (coloristImageHighlight_) {
            clImageDestroy(coloristContext_, coloristImageHighlight_);
            coloristImageHighlight_ = nullptr;
        }
        if (coloristHighlightInfo_) {
            clImageSRGBHighlightPixelInfoDestroy(coloristContext_, coloristHighlightInfo_);
            coloristHighlightInfo_ = nullptr;
        }
        coloristHighlightInfo_ = clImageSRGBHighlightPixelInfoCreate(coloristContext_, srcImage->width * srcImage->height);
        coloristImageHighlight_ = clImageCreateSRGBHighlight(coloristContext_, srcImage, sdrWhite, &coloristHighlightStats_, coloristHighlightInfo_, nullptr);
        srcImage = coloristImageHighlight_;
    }

    if (srcImage) {
        clProfilePrimaries primaries;
        clProfileCurve curve;

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
        clImage * convertedImage = clImageConvert(coloristContext_, srcImage, coloristContext_->params.jobs, 16, profile, CL_TONEMAP_AUTO);
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
        if (coloristImage2_) {
            clImageDebugDumpPixel(coloristContext_, coloristImage2_, imageInfoX_, imageInfoY_, &pixelInfo2_);
        }
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
#ifdef CLAMP_TO_SCREEN_BOUNDS
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
#endif
}

void Vantage::adjustThreshold(int amount)
{
    int newThreshold = diffThreshold_ + amount;
    if (newThreshold < 0) {
        newThreshold = 0;
    }
    if (diffThreshold_ != newThreshold) {
        diffThreshold_ = newThreshold;
        prepareImage();
        render();
    }
}

void Vantage::setDiffMode(DiffMode diffMode)
{
    if (diffMode_ != diffMode) {
        diffMode_ = diffMode;
        if (coloristImageDiff_) {
            if (diffMode_ == DIFFMODE_SHOWDIFF) {
                srgbHighlight_ = false;
            }
        } else {
            diffMode_ = DIFFMODE_SHOW1;
        }
        prepareImage();
        render();
    }
}

void Vantage::setDiffIntensity(DiffIntensity diffIntensity)
{
    if (diffIntensity_ != diffIntensity) {
        diffIntensity_ = diffIntensity;
        if (coloristImageDiff_) {
            clImageDiffDestroy(coloristContext_, coloristImageDiff_);
            coloristImageDiff_ = nullptr;
        }
        prepareImage();
        render();
    }
}

void Vantage::toggleSrgbHighlight()
{
    if (diffMode_ == DIFFMODE_SHOWDIFF) {
        return;
    }

    srgbHighlight_ = !srgbHighlight_;
    prepareImage();
    render();
}

void Vantage::updateInfo()
{
    info_.clear();

    if (coloristImage_) {
        appendInfo("Dimensions     : %dx%d (%d bpc)", coloristImage_->width, coloristImage_->height, coloristImage_->depth);

        int fileSize = 0;
        if (coloristImageDiff_) {
            switch (diffMode_) {
                case DIFFMODE_SHOW1:
                    fileSize = coloristImageFileSize_;
                    break;
                case DIFFMODE_SHOW2:
                    fileSize = coloristImageFileSize2_;
                    break;
                case DIFFMODE_SHOWDIFF:
                    fileSize = 0;
                    break;
            }
        } else {
            fileSize = coloristImageFileSize_;
        }

        if (fileSize > 0) {
            if (fileSize > (1024 * 1024)) {
                appendInfo("File Size      : %2.2f MB", (float)fileSize / (1024.0f * 1024.0f));
            } else if (fileSize > 1024) {
                appendInfo("File Size      : %2.2f KB", (float)fileSize / 1024.0f);
            } else {
                appendInfo("File Size      : %d bytes", (float)fileSize);
            }
        } else {
            appendInfo("");
        }

        if (coloristImageDiff_) {
            const char * showing = "??";
            switch (diffMode_) {
                case DIFFMODE_SHOW1:
                    showing = "Image 1";
                    break;
                case DIFFMODE_SHOW2:
                    showing = "Image 2";
                    break;
                case DIFFMODE_SHOWDIFF:
                    showing = "Diff";
                    break;
            }
            appendInfo("Showing        : %s", showing);
        }
    }

    if ((imageInfoX_ != -1) && (imageInfoY_ != -1)) {
        clImagePixelInfo * pixelInfo = &pixelInfo_;

        appendInfo("");
        appendInfo("[%d, %d]", imageInfoX_, imageInfoY_);

        appendInfo("");
        appendInfo("Image Info:");
        appendInfo("  R Raw        : %d", pixelInfo->rawR);
        appendInfo("  G Raw        : %d", pixelInfo->rawG);
        appendInfo("  B Raw        : %d", pixelInfo->rawB);
        appendInfo("  A Raw        : %d", pixelInfo->rawA);
        appendInfo("  x            : %3.3f", pixelInfo->x);
        appendInfo("  y            : %3.3f", pixelInfo->y);
        appendInfo("  Y            : %3.3f", pixelInfo->Y);

        if (coloristImage2_) {
            pixelInfo = &pixelInfo2_;

            appendInfo("");
            appendInfo("Image Info (2):");
            appendInfo("  R Raw        : %d", pixelInfo->rawR);
            appendInfo("  G Raw        : %d", pixelInfo->rawG);
            appendInfo("  B Raw        : %d", pixelInfo->rawB);
            appendInfo("  A Raw        : %d", pixelInfo->rawA);
            appendInfo("  x            : %3.3f", pixelInfo->x);
            appendInfo("  y            : %3.3f", pixelInfo->y);
            appendInfo("  Y            : %3.3f", pixelInfo->Y);
        }
    }

    if (srgbHighlight_ && coloristHighlightInfo_) {
        if ((imageInfoX_ != -1) && (imageInfoY_ != -1)) {
            clImageSRGBHighlightPixel * highlightPixel = &coloristHighlightInfo_->pixels[imageInfoX_ + (imageInfoY_ * coloristImage_->width)];
            appendInfo("");
            appendInfo("Pixel Highlight:");
            appendInfo("  Nits         : %2.2f", highlightPixel->nits);
            appendInfo("  SRGB Max Nits: %2.2f", highlightPixel->maxNits);
            appendInfo("  Overbright   : %2.2f%%", 100.0f * highlightPixel->nits / highlightPixel->maxNits);
            appendInfo("  Out of Gamut : %2.2f%%", 100.0f * highlightPixel->outOfGamut);
        }
        appendInfo("");
        appendInfo("Highlight Stats:");
        appendInfo("Total Pixels   : %d", coloristImage_->width * coloristImage_->height);
        appendInfo("  Overbright   : %d (%.1f%%)", coloristHighlightStats_.overbrightPixelCount, 100.0f * coloristHighlightStats_.overbrightPixelCount / coloristHighlightStats_.pixelCount);
        appendInfo("  Out of Gamut : %d (%.1f%%)", coloristHighlightStats_.outOfGamutPixelCount, 100.0f * coloristHighlightStats_.outOfGamutPixelCount / coloristHighlightStats_.pixelCount);
        appendInfo("  Both         : %d (%.1f%%)", coloristHighlightStats_.bothPixelCount, 100.0f * coloristHighlightStats_.bothPixelCount / coloristHighlightStats_.pixelCount);
        appendInfo("  HDR Pixels   : %d (%.1f%%)", coloristHighlightStats_.hdrPixelCount, 100.0f * coloristHighlightStats_.hdrPixelCount / coloristHighlightStats_.pixelCount);
        appendInfo("  Brightest    : [%d, %d]", coloristHighlightStats_.brightestPixelX, coloristHighlightStats_.brightestPixelY);
        appendInfo("                 %2.2f nits", coloristHighlightStats_.brightestPixelNits);
    }

    if (coloristImageDiff_ && (diffMode_ == DIFFMODE_SHOWDIFF)) {
        appendInfo("");
        appendInfo("Threshold      : %d", diffThreshold_);
        appendInfo("Largest Diff   : %d", coloristImageDiff_->largestChannelDiff);

        if ((imageInfoX_ != -1) && (imageInfoY_ != -1)) {
            int diff = coloristImageDiff_->diffs[imageInfoX_ + (imageInfoY_ * coloristImageDiff_->image->width)];
            appendInfo("Pixel Diff     : %d", diff);
        }

        appendInfo("");
        appendInfo("Total Pixels   : %d", coloristImage_->width * coloristImage_->height);
        appendInfo("Perfect Matches: %7d (%.1f%%)", coloristImageDiff_->matchCount, (100.0f * (float)coloristImageDiff_->matchCount / coloristImageDiff_->pixelCount));
        appendInfo("Under Threshold: %7d (%.1f%%)", coloristImageDiff_->underThresholdCount, (100.0f * (float)coloristImageDiff_->underThresholdCount / coloristImageDiff_->pixelCount));
        appendInfo("Over Threshold : %7d (%.1f%%)", coloristImageDiff_->overThresholdCount, (100.0f * (float)coloristImageDiff_->overThresholdCount / coloristImageDiff_->pixelCount));
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

extern "C" {
int clTransformCalcHLGLuminance(int diffuseWhite);
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
    bool showHLG = false;

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

        clProfileCurve curve;
        clProfileQuery(coloristContext_, coloristImage_->profile, NULL, &curve, NULL);
        if (curve.type == CL_PCT_HLG) {
            showHLG = true;
        }
    }

    updateInfo();

    int64_t now = GetTickCount();
    int64_t dt = now - overlayTick_;
    if ((!overlay_.empty() || !info_.empty()) && (dt < overlayDuration_)) {

        const float infoW = 360.0f;
        float left = (clientW - infoW);
        float top = 10.0f;
        float nextLine = 20.0f;

        if (info_.size() > 3) {
            // Draw transparent black overlay
            float blackW = infoW;
            float blackH = clientH;
            ConstantBuffer cb;
            cb.transform = XMMatrixIdentity();
            cb.transform *= XMMatrixScaling(blackW, blackH, 1.0f);
            cb.transform *= XMMatrixTranslation(left, 0, 0.0f);
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
            context_->PSSetShaderResources(0, 1, &black_);
            context_->PSSetSamplers(0, 1, &sampler_);
            context_->DrawIndexed(6, 0, 0);
        }

        beginText();

        int i = 0;
        float lum = 1.0f;
        if (dt > (overlayDuration_ - overlayFade_)) {
            lum = (overlayFade_ - (dt - (overlayDuration_ - overlayFade_))) / 1000.0f;
        }

        if (hdrActive_) {
            // assume lum is SDR white level with a ~2.2 gamma, convert to PQ
            lum = powf(lum, 2.2f);
            lum *= (float)sdrWhiteLevel() / 10000.0f;
            lum = PQ_OETF(lum);
        }

        char buffer[1024];
        int sdrWhite = sdrWhiteLevel();
        if (showHLG) {
            sprintf(buffer, "SDR: %d nits, peak: %d nits", sdrWhite, clTransformCalcHLGLuminance(sdrWhite));
        } else {
            sprintf(buffer, "SDR: %d nits", sdrWhite);
        }
        drawText(buffer, 10, clientH - 25.0f, lum);

        for (auto it = overlay_.begin(); it != overlay_.end(); ++it, ++i) {
            drawText(it->c_str(), 10, 10.0f + (i * 20.0f), lum);
        }

        left += 10.0f; // margin
        for (auto it = info_.begin(); it != info_.end(); ++it, ++i) {
            drawText(it->c_str(), left, top, lum);
            top += nextLine;
        }

        endText();
    }

    swapChain_->Present(1, 0);
}
