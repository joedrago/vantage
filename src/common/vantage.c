#include "vantage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --------------------------------------------------------------------------------------
// Colorist externs / hacks

int clTransformCalcHLGLuminance(int diffuseWhite);

// hoisting a cross platform call from colorist
static double now()
{
    Timer t;
    timerStart(&t);
    return t.start;
}

// --------------------------------------------------------------------------------------
// Font data

#include "glyph.h"
extern unsigned int monoBinarySize;
extern unsigned char monoBinaryData[];

// --------------------------------------------------------------------------------------
// Constants

static const float MAX_SCALE = 32.0f;

// --------------------------------------------------------------------------------------
// SMPTE ST.2084: https://ieeexplore.ieee.org/servlet/opac?punumber=7291450

static const float PQ_C1 = 0.8359375;       // 3424.0 / 4096.0
static const float PQ_C2 = 18.8515625;      // 2413.0 / 4096.0 * 32.0
static const float PQ_C3 = 18.6875;         // 2392.0 / 4096.0 * 32.0
static const float PQ_M1 = 0.1593017578125; // 2610.0 / 4096.0 / 4.0
static const float PQ_M2 = 78.84375;        // 2523.0 / 4096.0 * 128.0

// SMPTE ST.2084: Equation 5.2
// N = ( (c1 + (c2 * L^m1)) / (1 + (c3 * L^m1)) )^m2
static float PQ_OETF(float L)
{
    float Lm1 = powf(L, PQ_M1);
    float c2Lm1 = PQ_C2 * Lm1;
    float c3Lm1 = PQ_C3 * Lm1;
    return powf((PQ_C1 + c2Lm1) / (1 + c3Lm1), PQ_M2);
}

// --------------------------------------------------------------------------------------
// Creation / destruction

Vantage * vantageCreate(void)
{
    Vantage * V = (Vantage *)malloc(sizeof(Vantage));

    V->platformWhiteLevel_ = 1;
    V->platformW_ = 1;
    V->platformH_ = 1;
    V->platformHDRActive_ = 0;
    V->platformUsesLinear_ = 0;

    V->C = clContextCreate(NULL);
    V->filenames_ = NULL;
    V->imageFileIndex_ = 0;

    V->diffMode_ = DIFFMODE_SHOWDIFF;
    V->diffIntensity_ = DIFFINTENSITY_BRIGHT;
    V->diffThreshold_ = 0;
    V->srgbHighlight_ = 0;

    V->image_ = NULL;
    V->image2_ = NULL;
    V->imageFont_ = NULL;
    V->imageDiff_ = NULL;
    V->imageHighlight_ = NULL;
    V->preparedImage_ = NULL;
    V->highlightInfo_ = NULL;

    V->dragging_ = 0;
    V->dragStartX_ = -1;
    V->dragStartY_ = -1;

    V->imageFileSize_ = 0;
    V->imageFileSize2_ = 0;
    V->imageInfoX_ = -1;
    V->imageInfoY_ = -1;
    V->imageDirty_ = 0;

    clRaw rawFont;
    rawFont.ptr = monoBinaryData;
    rawFont.size = monoBinarySize;
    clFormat * format = clContextFindFormat(V->C, "png");
    V->imageFont_ = format->readFunc(V->C, "png", NULL, &rawFont);

    V->blits_ = NULL;
    daCreate(&V->blits_, sizeof(Blit));

    V->glyphs_ = dmCreate(DKF_INTEGER, 0);
    int glyphCount = sizeof(monoGlyphs) / sizeof(monoGlyphs[0]);
    for (int i = 0; i < glyphCount; ++i) {
        Glyph * g = &monoGlyphs[i];
        dmGetI2P(V->glyphs_, g->id) = g;
    }

    V->overlay_ = NULL;
    V->overlayStart_ = now();
    V->overlayDuration_ = 30.0f;
    V->overlayFade_ = 1.0f;
    V->info_ = NULL;
    return V;
}

void vantageDestroy(Vantage * V)
{
    vantageUnload(V);
    clContextDestroy(V->C);
    daDestroy(&V->filenames_, dsDestroyIndirect);
    daDestroy(&V->overlay_, dsDestroyIndirect);
    daDestroy(&V->info_, dsDestroyIndirect);
    daDestroy(&V->blits_, NULL);
    free(V);
}

// --------------------------------------------------------------------------------------
// Overlay

static void clearOverlay(Vantage * V)
{
    daClear(&V->overlay_, dsDestroyIndirect);
}

static void appendOverlay(Vantage * V, const char * format, ...)
{
    va_list args;
    va_start(args, format);

    char * appendMe = NULL;
    dsConcatv(&appendMe, format, args);
    va_end(args);

    daPush(&V->overlay_, appendMe);

    vantageKickOverlay(V);
}

static void appendInfo(Vantage * V, const char * format, ...)
{
    va_list args;
    va_start(args, format);

    char * appendMe = NULL;
    dsConcatv(&appendMe, format, args);
    va_end(args);

    daPush(&V->info_, appendMe);
}

void vantageKickOverlay(Vantage * V)
{
    V->overlayStart_ = now();
}

void vantageKillOverlay(Vantage * V)
{
    V->overlayStart_ = now() - V->overlayDuration_;
    V->imageInfoX_ = -1;
    V->imageInfoY_ = -1;
}

// --------------------------------------------------------------------------------------
// Load

void vantageFileListClear(Vantage * V)
{
    daClear(&V->filenames_, dsDestroyIndirect);
}

void vantageFileListAppend(Vantage * V, const char * filename)
{
    char * s = NULL;
    dsCopy(&s, filename);
    daPush(&V->filenames_, s);
}

void vantageLoad(Vantage * V, int offset)
{
    if (daSize(&V->filenames_) < 1) {
        return;
    }

    clearOverlay(V);

    int loadIndex = V->imageFileIndex_ + offset;
    if (loadIndex < 0) {
        loadIndex = (int)daSize(&V->filenames_) - 1;
    }
    if (loadIndex >= daSize(&V->filenames_)) {
        loadIndex = 0;
    }
    V->imageFileIndex_ = loadIndex;

    char * filename = V->filenames_[V->imageFileIndex_];

    V->imageFileSize_ = clFileSize(filename);
    V->imageFileSize2_ = 0;
    V->image_ = clContextRead(V->C, filename, NULL, NULL);

    V->diffMode_ = DIFFMODE_SHOW1;

    const char * shortFilename = strrchr(filename, '\\');
    if (!shortFilename) {
        shortFilename = strrchr(filename, '/');
    }
    if (shortFilename) {
        ++shortFilename;
    } else {
        shortFilename = filename;
    }

    vantagePrepareImage(V);
    vantageResetImagePos(V);
    clearOverlay(V);
    if (V->image_) {
        appendOverlay(V, "[%d/%d] Loaded: %s", V->imageFileIndex_ + 1, daSize(&V->filenames_), shortFilename);
    } else {
        appendOverlay(V, "[%d/%d] Failed to load: %s", V->imageFileIndex_ + 1, daSize(&V->filenames_), shortFilename);
    }
}

void vantageLoadDiff(Vantage * V, const char * filename1, const char * filename2)
{
    vantageFileListClear(V);
    clearOverlay(V);

    const char * failureReason = NULL;
    V->imageFileSize_ = clFileSize(filename1);
    V->imageFileSize2_ = clFileSize(filename2);
    V->image_ = clContextRead(V->C, filename1, NULL, NULL);
    V->image2_ = clContextRead(V->C, filename2, NULL, NULL);
    if (!V->image_ || !V->image2_) {
        failureReason = "Both failed to load";
    } else if (!V->image_) {
        failureReason = "First failed to load";
    } else if (!V->image2_) {
        failureReason = "Second failed to load";
    } else if ((V->image_->width != V->image2_->width) || (V->image_->height != V->image2_->height)) {
        failureReason = "Dimensions mismatch";
    }

    // Trim all matching portions from the front of the names
    const char * short1 = filename1;
    const char * short2 = filename2;
    while (*short1 && (*short1 == *short2)) {
        ++short1;
        ++short2;
    }
    // now walk back the pointers to the nearest path separator, if any
    while (short1 != filename1) {
        --short1;
        if (*short1 == '\\') {
            ++short1;
            break;
        }
    }
    while (short2 != filename2) {
        --short2;
        if (*short2 == '\\') {
            ++short2;
            break;
        }
    }

    if (failureReason) {
        vantageUnload(V);
        appendOverlay(V, "Failed to load diff (%s):", failureReason);
        appendOverlay(V, "* %s", short1);
        appendOverlay(V, "* %s", short2);
        return;
    }

    appendOverlay(V, "Loaded diff: (in 1st color volume)");
    appendOverlay(V, "* 1: %s", short1);
    appendOverlay(V, "* 2: %s", short2);

    V->diffMode_ = DIFFMODE_SHOWDIFF;
    V->diffIntensity_ = DIFFINTENSITY_BRIGHT;
    vantagePrepareImage(V);
    vantageResetImagePos(V);
}

void vantageUnload(Vantage * V)
{
    if (V->image_) {
        clImageDestroy(V->C, V->image_);
        V->image_ = NULL;
    }
    if (V->image2_) {
        clImageDestroy(V->C, V->image2_);
        V->image2_ = NULL;
    }
    if (V->imageFont_) {
        clImageDestroy(V->C, V->imageFont_);
        V->imageFont_ = NULL;
    }
    if (V->imageDiff_) {
        clImageDiffDestroy(V->C, V->imageDiff_);
        V->imageDiff_ = NULL;
    }
    if (V->imageHighlight_) {
        clImageDestroy(V->C, V->imageHighlight_);
        V->imageHighlight_ = NULL;
    }
    if (V->preparedImage_) {
        clImageDestroy(V->C, V->preparedImage_);
        V->preparedImage_ = NULL;
    }
    if (V->highlightInfo_) {
        clImageSRGBHighlightPixelInfoDestroy(V->C, V->highlightInfo_);
        V->highlightInfo_ = NULL;
    }

    V->imageFileSize_ = 0;
    V->imageFileSize2_ = 0;
    V->imageInfoX_ = -1;
    V->imageInfoY_ = -1;
    V->imageDirty_ = 1;
}

// --------------------------------------------------------------------------------------
// Special mode state

void vantageAdjustThreshold(Vantage * V, int amount)
{
    int newThreshold = V->diffThreshold_ + amount;
    if (newThreshold < 0) {
        newThreshold = 0;
    }
    if (V->diffThreshold_ != newThreshold) {
        V->diffThreshold_ = newThreshold;
        vantagePrepareImage(V);
    }
}

void vantageSetDiffIntensity(Vantage * V, DiffIntensity diffIntensity)
{
    if (V->diffIntensity_ != diffIntensity) {
        V->diffIntensity_ = diffIntensity;
        if (V->imageDiff_) {
            clImageDiffDestroy(V->C, V->imageDiff_);
            V->imageDiff_ = NULL;
        }
        vantagePrepareImage(V);
    }
}

void vantageSetDiffMode(Vantage * V, DiffMode diffMode)
{
    if (V->diffMode_ != diffMode) {
        V->diffMode_ = diffMode;
        if (V->imageDiff_) {
            if (V->diffMode_ == DIFFMODE_SHOWDIFF) {
                V->srgbHighlight_ = 0;
            }
        } else {
            V->diffMode_ = DIFFMODE_SHOW1;
        }
        vantagePrepareImage(V);
    }
}

void vantageToggleSrgbHighlight(Vantage * V)
{
    if (V->diffMode_ == DIFFMODE_SHOWDIFF) {
        return;
    }

    V->srgbHighlight_ = V->srgbHighlight_ ? 0 : 1;
    vantagePrepareImage(V);
}

// --------------------------------------------------------------------------------------
// Positioning

void vantageCalcCenteredImagePos(Vantage * V, float * posX, float * posY)
{
    if (!V->image_) {
        *posX = 0;
        *posY = 0;
        return;
    }

    float clientW = (float)V->platformW_;
    float clientH = (float)V->platformH_;
    *posX = (clientW - V->imagePosW_) / 2.0f;
    *posY = (clientH - V->imagePosH_) / 2.0f;
}

void vantageCalcImageSize(Vantage * V)
{
    if (!V->image_) {
        V->imagePosW_ = 1;
        V->imagePosH_ = 1;
        return;
    }

    float clientW = (float)V->platformW_;
    float clientH = (float)V->platformH_;
    float imageW = (float)V->image_->width;
    float imageH = (float)V->image_->height;
    float clientRatio = clientW / clientH;
    float imageRatio = imageW / imageH;

    if (clientRatio < imageRatio) {
        V->imagePosW_ = clientW;
        V->imagePosH_ = clientW / imageW * imageH;
    } else {
        V->imagePosH_ = clientH;
        V->imagePosW_ = clientH / imageH * imageW;
    }

    V->imagePosW_ *= V->imagePosS_;
    V->imagePosH_ *= V->imagePosS_;
}

void vantageResetImagePos(Vantage * V)
{
    V->imagePosS_ = 1.0f;
    vantageCalcImageSize(V);
    vantageCalcCenteredImagePos(V, &V->imagePosX_, &V->imagePosY_);
}

// --------------------------------------------------------------------------------------
// Mouse handling

void vantageMouseLeftDoubleClick(Vantage * V, int x, int y)
{
    const float scaleTiers[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
    const int scaleTierCount = sizeof(scaleTiers) / sizeof(scaleTiers[0]);
    int scaleIndex = 0;
    for (int i = 0; i < scaleTierCount; ++i) {
        if (V->imagePosS_ < scaleTiers[i]) {
            break;
        }
        scaleIndex = i;
    }
    scaleIndex = (scaleIndex + 1) % scaleTierCount;
    V->imagePosS_ = scaleTiers[scaleIndex];

    float normalizedImagePosX = (x - V->imagePosX_) / V->imagePosW_;
    float normalizedImagePosY = (y - V->imagePosY_) / V->imagePosH_;
    vantageCalcImageSize(V);
    V->imagePosX_ = (float)x - normalizedImagePosX * V->imagePosW_;
    V->imagePosY_ = (float)y - normalizedImagePosY * V->imagePosH_;
}

void vantageMouseLeftDown(Vantage * V, int x, int y)
{
    V->dragging_ = 1;
    V->dragStartX_ = x;
    V->dragStartY_ = y;
}

void vantageMouseLeftUp(Vantage * V, int x, int y)
{
    V->dragging_ = 0;
}

void vantageMouseMove(Vantage * V, int x, int y)
{
    if (V->dragging_) {
        float dx = (float)(x - V->dragStartX_);
        float dy = (float)(y - V->dragStartY_);
        V->imagePosX_ += dx;
        V->imagePosY_ += dy;

        V->dragStartX_ = x;
        V->dragStartY_ = y;
    }
}

void vantageMouseSetPos(Vantage * V, int x, int y)
{
    int right = (int)(V->imagePosX_ + V->imagePosW_);
    int bottom = (int)(V->imagePosY_ + V->imagePosH_);

    vantageKickOverlay(V);

    if (!V->image_ || (x < V->imagePosX_) || (y < V->imagePosY_) || (x >= right) || (y >= bottom)) {
        V->imageInfoX_ = -1;
        V->imageInfoY_ = -1;
    } else {
        V->imageInfoX_ = (int)(((float)x - V->imagePosX_) * ((float)V->image_->width / V->imagePosW_));
        V->imageInfoY_ = (int)(((float)y - V->imagePosY_) * ((float)V->image_->height / V->imagePosH_));
        clImageDebugDumpPixel(V->C, V->image_, V->imageInfoX_, V->imageInfoY_, &V->pixelInfo_);
        if (V->image2_) {
            clImageDebugDumpPixel(V->C, V->image2_, V->imageInfoX_, V->imageInfoY_, &V->pixelInfo2_);
        }
    }
}

void vantageMouseWheel(Vantage * V, int x, int y, float delta)
{
    V->imagePosS_ += delta;
    if (V->imagePosS_ < 1.0f) {
        V->imagePosS_ = 1.0f;
    }
    if (V->imagePosS_ > MAX_SCALE) {
        V->imagePosS_ = MAX_SCALE;
    }

    float normalizedImagePosX = (x - V->imagePosX_) / V->imagePosW_;
    float normalizedImagePosY = (y - V->imagePosY_) / V->imagePosH_;
    vantageCalcImageSize(V);
    V->imagePosX_ = (float)x - normalizedImagePosX * V->imagePosW_;
    V->imagePosY_ = (float)y - normalizedImagePosY * V->imagePosH_;
}

// --------------------------------------------------------------------------------------
// Platform hooks

void vantagePlatformSetHDRActive(Vantage * V, int hdrActive)
{
    if (V->platformHDRActive_ != hdrActive) {
        V->platformHDRActive_ = hdrActive;
        vantagePrepareImage(V);
    }
}

void vantagePlatformSetSize(Vantage * V, int width, int height)
{
    V->platformW_ = width;
    V->platformH_ = height;

    vantageResetImagePos(V);
}

void vantagePlatformSetWhiteLevel(Vantage * V, int whiteLevel)
{
    if (V->platformWhiteLevel_ != whiteLevel) {
        V->platformWhiteLevel_ = whiteLevel;
        vantagePrepareImage(V);
    }
}

void vantagePlatformSetUsesLinear(Vantage * V, int usesLinear)
{
    if (V->platformUsesLinear_ != usesLinear) {
        V->platformUsesLinear_ = usesLinear;
        vantagePrepareImage(V);
    }
}

// --------------------------------------------------------------------------------------
// Rendering

void vantagePrepareImage(Vantage * V)
{
    if (V->preparedImage_) {
        clImageDestroy(V->C, V->preparedImage_);
        V->preparedImage_ = NULL;
    }

    clImage * srcImage = NULL;

    unsigned int sdrWhite = V->platformWhiteLevel_;
    V->C->defaultLuminance = sdrWhite;

    if (V->image_ && V->image2_) {
        if (!clProfileMatches(V->C, V->image_->profile, V->image2_->profile) || (V->image_->depth != V->image2_->depth)) {
            if (V->imageDiff_) {
                clImageDiffDestroy(V->C, V->imageDiff_);
                V->imageDiff_ = NULL;
            }
        }

        if (V->imageDiff_) {
            clImageDiffUpdate(V->C, V->imageDiff_, V->diffThreshold_);
        } else {
            clImage * secondImage = V->image2_;
            if (!clProfileMatches(V->C, V->image_->profile, V->image2_->profile) || (V->image_->depth != V->image2_->depth)) {
                secondImage = clImageConvert(V->C, V->image2_, V->C->params.jobs, V->image_->depth, V->image_->profile, CL_TONEMAP_OFF);
            }

            float minIntensity = 0.0f;
            switch (V->diffIntensity_) {
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

            V->imageDiff_ = clImageDiffCreate(V->C, V->image_, secondImage, V->C->params.jobs, minIntensity, V->diffThreshold_);

            if (V->image2_ != secondImage) {
                clImageDestroy(V->C, secondImage);
            }
        }

        switch (V->diffMode_) {
            case DIFFMODE_SHOW1:
                srcImage = V->image_;
                break;
            case DIFFMODE_SHOW2:
                srcImage = V->image2_;
                break;
            case DIFFMODE_SHOWDIFF:
                srcImage = V->imageDiff_->image;
                break;
        }
    } else {
        // Just show an image like normal
        srcImage = V->image_;
    }

    if ((V->diffMode_ != DIFFMODE_SHOWDIFF) && V->srgbHighlight_) {
        if (V->imageHighlight_) {
            clImageDestroy(V->C, V->imageHighlight_);
            V->imageHighlight_ = NULL;
        }
        if (V->highlightInfo_) {
            clImageSRGBHighlightPixelInfoDestroy(V->C, V->highlightInfo_);
            V->highlightInfo_ = NULL;
        }
        V->highlightInfo_ = clImageSRGBHighlightPixelInfoCreate(V->C, srcImage->width * srcImage->height);
        V->imageHighlight_ = clImageCreateSRGBHighlight(V->C, srcImage, sdrWhite, &V->highlightStats_, V->highlightInfo_, NULL);
        srcImage = V->imageHighlight_;
    }

    if (srcImage) {
        clProfilePrimaries primaries;
        clProfileCurve curve;

        int dstLuminance = 10000;
        if (V->platformHDRActive_) {
            clContextGetStockPrimaries(V->C, "bt2020", &primaries);
            if (V->platformUsesLinear_) {
                curve.type = CL_PCT_GAMMA;
            } else {
                curve.type = CL_PCT_PQ;
            }
            dstLuminance = 10000;
            curve.implicitScale = 1.0f;
            curve.gamma = 1.0f;
            V->imageHDR_ = 1;
        } else {
            clContextGetStockPrimaries(V->C, "bt709", &primaries);
            curve.type = CL_PCT_GAMMA;
            curve.gamma = 2.2f;
            dstLuminance = sdrWhite;
            V->imageHDR_ = 0;
        }
        curve.implicitScale = 1.0f;
        V->imageWhiteLevel_ = sdrWhite;

        clProfile * profile = clProfileCreate(V->C, &primaries, &curve, dstLuminance, NULL);
        V->preparedImage_ = clImageConvert(V->C, srcImage, V->C->params.jobs, 16, profile, CL_TONEMAP_AUTO);
        clProfileDestroy(V->C, profile);
    }

    V->imageDirty_ = 1;
}

static void vantageBlitImage(Vantage * V, float dx, float dy, float dw, float dh)
{
    if (!V->preparedImage_) {
        return;
    }

    Blit blit;
    blit.sx = 0.0f;
    blit.sy = 0.0f;
    blit.sw = 1.0f;
    blit.sh = 1.0f;
    blit.dx = dx / V->platformW_;
    blit.dy = dy / V->platformH_;
    blit.dw = dw / V->platformW_;
    blit.dh = dh / V->platformH_;
    blit.color.r = 1.0f;
    blit.color.g = 1.0f;
    blit.color.b = 1.0f;
    blit.color.a = 1.0f;
    blit.mode = BM_IMAGE;
    daPush(&V->blits_, blit);
}

static void vantageFill(Vantage * V, float dx, float dy, float dw, float dh, Color * color)
{
    Blit blit;
    blit.sx = 0.0f;
    blit.sy = 0.0f;
    blit.sw = 1.0f;
    blit.sh = 1.0f;
    blit.dx = dx / V->platformW_;
    blit.dy = dy / V->platformH_;
    blit.dw = dw / V->platformW_;
    blit.dh = dh / V->platformH_;
    blit.color = *color;
    blit.mode = BM_FILL;
    daPush(&V->blits_, blit);
}

void vantageBlitString(Vantage * V, const char * str, float x, float y, float height, Color * startingColor)
{
    Color color = *startingColor;

    float scaleX = height / (float)GLYPH_LINEHEIGHT;
    float scaleY = height / (float)GLYPH_LINEHEIGHT;
    float currX = x;
    float currY = y;

    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        int c = (int)str[i];
        if (c > 255) {
            continue;
        }

        if (c == '`') {
            if ((i + 1 < len) && (str[i + 1] == '`')) {
                // Reset overrides
                i += 1; // skip the reset tag
                color = *startingColor;
            } else if ((i + 7 < len) && (str[i + 1] == '#')) {
                // Override color
                char hexString[8];
                for (unsigned int j = 0; j < 7; ++j) {
                    hexString[j] = (char)str[i + j + 1];
                }
                hexString[7] = '\0';
                i += 7; // skip the color ("#ffffff")

                unsigned int r = 0, g = 0, b = 0;
                int count = sscanf(hexString, "#%02x%02x%02x", &r, &g, &b);
                if (count == 3) {
                    color.r = r / 255.0f;
                    color.g = g / 255.0f;
                    color.b = b / 255.0f;
                }
            }
            continue;
        }

        if (!dmHasI(V->glyphs_, c)) {
            continue;
        }

        Glyph * glyph = (Glyph *)dmGetI2P(V->glyphs_, c);

        float dstX = currX + glyph->xoffset * scaleX;
        float dstY = currY + glyph->yoffset * scaleY;
        float dstW = glyph->width * scaleX;
        float dstH = glyph->height * scaleY;

        Blit blit;
        blit.sx = (float)glyph->x / GLYPH_SCALEW;
        blit.sy = (float)glyph->y / GLYPH_SCALEH;
        blit.sw = (float)glyph->width / GLYPH_SCALEH;
        blit.sh = (float)glyph->height / GLYPH_SCALEH;
        blit.dx = dstX / V->platformW_;
        blit.dy = dstY / V->platformH_;
        blit.dw = dstW / V->platformW_;
        blit.dh = dstH / V->platformH_;
        blit.color = color;
        blit.mode = BM_TEXT;
        daPush(&V->blits_, blit);

        currX += scaleX * glyph->advance;
    }
}

static void vantageUpdateInfo(Vantage * V)
{
    daClear(&V->info_, dsDestroyIndirect);

    if (V->image_) {
        int width = V->image_->width;
        int height = V->image_->height;
        int depth = V->image_->depth;

        int fileSize = 0;
        if (V->imageDiff_) {
            switch (V->diffMode_) {
                case DIFFMODE_SHOW1:
                    fileSize = V->imageFileSize_;
                    break;
                case DIFFMODE_SHOW2:
                    fileSize = V->imageFileSize2_;
                    width = V->image2_->width;
                    height = V->image2_->height;
                    depth = V->image2_->depth;
                    break;
                case DIFFMODE_SHOWDIFF:
                    fileSize = 0;
                    break;
            }
        } else {
            fileSize = V->imageFileSize_;
        }

        appendInfo(V, "Dimensions     : %dx%d (%d bpc)", width, height, depth);

        if (fileSize > 0) {
            if (fileSize > (1024 * 1024)) {
                appendInfo(V, "File Size      : %2.2f MB", (float)fileSize / (1024.0f * 1024.0f));
            } else if (fileSize > 1024) {
                appendInfo(V, "File Size      : %2.2f KB", (float)fileSize / 1024.0f);
            } else {
                appendInfo(V, "File Size      : %d bytes", (float)fileSize);
            }
        } else {
            appendInfo(V, "");
        }

        if (V->imageDiff_) {
            const char * showing = "??";
            switch (V->diffMode_) {
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
            appendInfo(V, "Showing        : %s", showing);
        }
    }

    if ((V->imageInfoX_ != -1) && (V->imageInfoY_ != -1)) {
        clImagePixelInfo * pixelInfo = &V->pixelInfo_;

        appendInfo(V, "");
        appendInfo(V, "[%d, %d]", V->imageInfoX_, V->imageInfoY_);

        appendInfo(V, "");
        appendInfo(V, "Image Info:");
        appendInfo(V, "  R Raw        : %d", pixelInfo->rawR);
        appendInfo(V, "  G Raw        : %d", pixelInfo->rawG);
        appendInfo(V, "  B Raw        : %d", pixelInfo->rawB);
        appendInfo(V, "  A Raw        : %d", pixelInfo->rawA);
        appendInfo(V, "  x            : %3.3f", pixelInfo->x);
        appendInfo(V, "  y            : %3.3f", pixelInfo->y);
        appendInfo(V, "  Y            : %3.3f", pixelInfo->Y);

        if (V->image2_) {
            pixelInfo = &V->pixelInfo2_;

            appendInfo(V, "");
            appendInfo(V, "Image Info (2):");
            appendInfo(V, "  R Raw        : %d", pixelInfo->rawR);
            appendInfo(V, "  G Raw        : %d", pixelInfo->rawG);
            appendInfo(V, "  B Raw        : %d", pixelInfo->rawB);
            appendInfo(V, "  A Raw        : %d", pixelInfo->rawA);
            appendInfo(V, "  x            : %3.3f", pixelInfo->x);
            appendInfo(V, "  y            : %3.3f", pixelInfo->y);
            appendInfo(V, "  Y            : %3.3f", pixelInfo->Y);
        }
    }

    if (V->srgbHighlight_ && V->highlightInfo_) {
        if ((V->imageInfoX_ != -1) && (V->imageInfoY_ != -1)) {
            clImageSRGBHighlightPixel * highlightPixel =
                &V->highlightInfo_->pixels[V->imageInfoX_ + (V->imageInfoY_ * V->image_->width)];
            appendInfo(V, "");
            appendInfo(V, "Pixel Highlight:");
            appendInfo(V, "  Nits         : %2.2f", highlightPixel->nits);
            appendInfo(V, "  SRGB Max Nits: %2.2f", highlightPixel->maxNits);
            appendInfo(V, "  Overbright   : %2.2f%%", 100.0f * highlightPixel->nits / highlightPixel->maxNits);
            appendInfo(V, "  Out of Gamut : %2.2f%%", 100.0f * highlightPixel->outOfGamut);
        }
        appendInfo(V, "");
        appendInfo(V, "Highlight Stats:");
        appendInfo(V, "Total Pixels   : %d", V->image_->width * V->image_->height);
        appendInfo(V,
                   "  Overbright   : %d (%.1f%%)",
                   V->highlightStats_.overbrightPixelCount,
                   100.0f * V->highlightStats_.overbrightPixelCount / V->highlightStats_.pixelCount);
        appendInfo(V,
                   "  Out of Gamut : %d (%.1f%%)",
                   V->highlightStats_.outOfGamutPixelCount,
                   100.0f * V->highlightStats_.outOfGamutPixelCount / V->highlightStats_.pixelCount);
        appendInfo(V,
                   "  Both         : %d (%.1f%%)",
                   V->highlightStats_.bothPixelCount,
                   100.0f * V->highlightStats_.bothPixelCount / V->highlightStats_.pixelCount);
        appendInfo(V,
                   "  HDR Pixels   : %d (%.1f%%)",
                   V->highlightStats_.hdrPixelCount,
                   100.0f * V->highlightStats_.hdrPixelCount / V->highlightStats_.pixelCount);
        appendInfo(V, "  Brightest    : [%d, %d]", V->highlightStats_.brightestPixelX, V->highlightStats_.brightestPixelY);
        appendInfo(V, "                 %2.2f nits", V->highlightStats_.brightestPixelNits);
    }

    if (V->imageDiff_ && (V->diffMode_ == DIFFMODE_SHOWDIFF)) {
        appendInfo(V, "");
        appendInfo(V, "Threshold      : %d", V->diffThreshold_);
        appendInfo(V, "Largest Diff   : %d", V->imageDiff_->largestChannelDiff);

        if ((V->imageInfoX_ != -1) && (V->imageInfoY_ != -1)) {
            int diff = V->imageDiff_->diffs[V->imageInfoX_ + (V->imageInfoY_ * V->imageDiff_->image->width)];
            appendInfo(V, "Pixel Diff     : %d", diff);
        }

        appendInfo(V, "");
        appendInfo(V, "Total Pixels   : %d", V->image_->width * V->image_->height);
        appendInfo(V,
                   "Perfect Matches: %7d (%.1f%%)",
                   V->imageDiff_->matchCount,
                   (100.0f * (float)V->imageDiff_->matchCount / V->imageDiff_->pixelCount));
        appendInfo(V,
                   "Under Threshold: %7d (%.1f%%)",
                   V->imageDiff_->underThresholdCount,
                   (100.0f * (float)V->imageDiff_->underThresholdCount / V->imageDiff_->pixelCount));
        appendInfo(V,
                   "Over Threshold : %7d (%.1f%%)",
                   V->imageDiff_->overThresholdCount,
                   (100.0f * (float)V->imageDiff_->overThresholdCount / V->imageDiff_->pixelCount));
    }
}

void vantageRender(Vantage * V)
{
    daClear(&V->blits_, NULL);

    if ((V->platformHDRActive_ != V->imageHDR_) || (V->imageWhiteLevel_ != V->platformWhiteLevel_)) {
        vantagePrepareImage(V);
    }

    vantageBlitImage(V, V->imagePosX_, V->imagePosY_, V->imagePosW_, V->imagePosH_);

    int showHLG = 0;
    if (V->image_) {
        clProfileCurve curve;
        clProfileQuery(V->C, V->image_->profile, NULL, &curve, NULL);
        if (curve.type == CL_PCT_HLG) {
            showHLG = 1;
        } else if (V->image2_) {
            clProfileQuery(V->C, V->image2_->profile, NULL, &curve, NULL);
            if (curve.type == CL_PCT_HLG) {
                showHLG = 1;
            }
        }
    }

    vantageUpdateInfo(V);

    double dt = now() - V->overlayStart_;
    if (((daSize(&V->overlay_) > 0) || (daSize(&V->info_) > 0)) && (dt < V->overlayDuration_)) {
        float clientW = (float)V->platformW_;
        float clientH = (float)V->platformH_;

        const float infoW = 360.0f;
        float left = (clientW - infoW);
        float top = 10.0f;
        float fontHeight = 18.0f;
        float nextLine = fontHeight + 2.0f;

        if (daSize(&V->info_) > 3) {
            float blackW = infoW;
            float blackH = clientH;
            Color black = { 0.0f, 0.0f, 0.0f, 0.8f };
            vantageFill(V, left, 0, blackW, blackH, &black);
        }

        float lum = 1.0f;
        if (dt > (V->overlayDuration_ - V->overlayFade_)) {
            lum = (float)(V->overlayFade_ - (dt - (V->overlayDuration_ - V->overlayFade_)));
        }

        if (V->platformHDRActive_) {
            // assume lum is SDR white level with a ~2.2 gamma, convert to PQ
            lum = powf(lum, 2.2f);
            lum *= (float)V->platformWhiteLevel_ / 10000.0f;
            lum = PQ_OETF(lum);
        }

        char buffer[1024];
        int sdrWhite = V->platformWhiteLevel_;
        if (showHLG) {
            sprintf(buffer,
                    "SDR: %d nits, HLG peak: %d nits, HDR: %s",
                    sdrWhite,
                    clTransformCalcHLGLuminance(sdrWhite),
                    V->platformHDRActive_ ? "Active" : "Inactive");
        } else {
            sprintf(buffer, "SDR: %d nits, HDR: %s", sdrWhite, V->platformHDRActive_ ? "Active" : "Inactive");
        }

        Color color = { 1.0f, 1.0f, 1.0f, 1.0f };
        color.a = lum;
        vantageBlitString(V, buffer, 10, clientH - 25.0f, fontHeight, &color);

        for (int i = 0; i < daSize(&V->overlay_); ++i) {
            vantageBlitString(V, V->overlay_[i], 10, top + (i * nextLine), fontHeight, &color);
        }

        left += 10.0f; // margin
        for (int i = 0; i < daSize(&V->info_); ++i) {
            vantageBlitString(V, V->info_[i], left, top, fontHeight, &color);
            top += nextLine;
        }
    }
}
