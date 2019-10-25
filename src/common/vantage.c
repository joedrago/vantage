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

// How many frames to render "Loading..." before actually loading
static const int LOADING_FRAMES = 3;

// How bright to draw the UI (text & sliders)
static const int TEXT_LUMINANCE = 300;

// SRGB luminance slider
static const int SRGB_LUMINANCE_MIN = 1;
static const int SRGB_LUMINANCE_DEF = 80;
static const int SRGB_LUMINANCE_MAX = 1000;
static const int SRGB_LUMINANCE_STEP = 5;

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
// Control helpers

static void controlInitSlider(Control * control, int * value, int min, int max, int step, uint32_t flags)
{
    memset(control, 0, sizeof(Control));
    control->type = CONTROLTYPE_SLIDER;
    control->value = value;
    control->min = min;
    control->max = max;
    control->step = step;
    control->flags = flags;
}

// --------------------------------------------------------------------------------------
// Creation / destruction

Vantage * vantageCreate(void)
{
    Vantage * V = (Vantage *)malloc(sizeof(Vantage));

    V->platformW_ = 1;
    V->platformH_ = 1;
    V->platformHDRActive_ = 0;
    V->platformHDRAvailable_ = 0;
    V->platformLinear_ = 0; // PQ by default
    V->platformMaxEDR_ = 0.0f;

    V->C = clContextCreate(NULL);
    V->filenames_ = NULL;
    V->imageFileIndex_ = 0;

    V->diffFilename1_ = NULL;
    V->diffFilename2_ = NULL;
    V->diffMode_ = DIFFMODE_SHOWDIFF;
    V->diffIntensity_ = DIFFINTENSITY_BRIGHT;
    V->diffThreshold_ = 0;
    V->srgbHighlight_ = 0;
    V->srgbLuminance_ = SRGB_LUMINANCE_DEF;
    V->unspecLuminance_ = SRGB_LUMINANCE_DEF;

    V->image_ = NULL;
    V->image2_ = NULL;
    V->imageFont_ = NULL;
    V->imageDiff_ = NULL;
    V->imageHighlight_ = NULL;
    V->preparedImage_ = NULL;
    V->highlightInfo_ = NULL;

    V->dragging_ = 0;
    V->dragLastX_ = 0;
    V->dragLastY_ = 0;
    V->dragControl_ = NULL;

    V->loadWaitFrames_ = 0;
    V->inReload_ = 0;
    V->tempTextBuffer_ = NULL;

    V->imageFileSize_ = 0;
    V->imageFileSize2_ = 0;
    V->imageInfoX_ = -1;
    V->imageInfoY_ = -1;
    V->imageLuminance_ = CL_LUMINANCE_UNSPECIFIED;
    V->imageDirty_ = 0;
    V->imageVideoFrameNextIndex_ = 0;
    V->imageVideoFrameIndex_ = 0;
    V->imageVideoFrameCount_ = 0;

    // Setup tonemapping for SDR mode (default values here are completely subjective)
    clTonemapParamsSetDefaults(V->C, &V->preparedTonemap_);
    V->preparedTonemapLuminance_ = SRGB_LUMINANCE_DEF;
    controlInitSlider(&V->preparedTonemapContrastSlider_, (int *)&V->preparedTonemap_.contrast, 1, 2000, 10, CONTROLFLAG_PREPARE | CONTROLFLAG_FLOAT);
    controlInitSlider(&V->preparedTonemapClipPointSlider_, (int *)&V->preparedTonemap_.clippingPoint, 1, 2000, 10, CONTROLFLAG_PREPARE | CONTROLFLAG_FLOAT);
    controlInitSlider(&V->preparedTonemapSpeedSlider_, (int *)&V->preparedTonemap_.speed, 1, 2000, 10, CONTROLFLAG_PREPARE | CONTROLFLAG_FLOAT);
    controlInitSlider(&V->preparedTonemapPowerSlider_, (int *)&V->preparedTonemap_.power, 1, 2000, 10, CONTROLFLAG_PREPARE | CONTROLFLAG_FLOAT);
    controlInitSlider(&V->preparedTonemapLuminanceSlider_,
                      &V->preparedTonemapLuminance_,
                      SRGB_LUMINANCE_MIN,
                      SRGB_LUMINANCE_MAX,
                      SRGB_LUMINANCE_STEP,
                      CONTROLFLAG_PREPARE);
    V->tonemapSlidersEnabled_ = 0;

    clRaw rawFont;
    rawFont.ptr = monoBinaryData;
    rawFont.size = monoBinarySize;
    clFormat * format = clContextFindFormat(V->C, "png");
    V->imageFont_ = format->readFunc(V->C, "png", NULL, &rawFont);

    V->blits_ = NULL;
    daCreate(&V->blits_, sizeof(Blit));
    V->wantedHDR_ = 1;
    V->wantsHDR_ = 1;

    V->dragControl_ = NULL;
    V->activeControls_ = NULL;
    daCreate(&V->activeControls_, 0);

    controlInitSlider(&V->srgbLuminanceSlider_, &V->srgbLuminance_, SRGB_LUMINANCE_MIN, SRGB_LUMINANCE_MAX, SRGB_LUMINANCE_STEP, CONTROLFLAG_PREPARE);
    controlInitSlider(&V->unspecLuminanceSlider_, &V->unspecLuminance_, SRGB_LUMINANCE_MIN, SRGB_LUMINANCE_MAX, SRGB_LUMINANCE_STEP, CONTROLFLAG_PREPARE);
    controlInitSlider(&V->imageVideoFrameIndexSlider_, &V->imageVideoFrameIndex_, 0, 0, 1, CONTROLFLAG_RELOAD);

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
    return V;
}

void vantageDestroy(Vantage * V)
{
    vantageUnload(V);
    if (V->imageFont_) {
        clImageDestroy(V->C, V->imageFont_);
        V->imageFont_ = NULL;
    }
    clContextDestroy(V->C);
    dsDestroy(&V->diffFilename1_);
    dsDestroy(&V->diffFilename2_);
    dsDestroy(&V->tempTextBuffer_);
    daDestroy(&V->filenames_, dsDestroyIndirect);
    daDestroy(&V->overlay_, dsDestroyIndirect);
    daDestroy(&V->blits_, NULL);
    daDestroy(&V->activeControls_, NULL);
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

static int vantageCanLoad(Vantage * V)
{
    if (V->inReload_) {
        return 1;
    }
    V->loadWaitFrames_ = LOADING_FRAMES;
    // if (V->loadWaitFrames_ == 0) {
    //     V->loadWaitFrames_ = LOADING_FRAMES;
    //     return 0;
    // }
    // if (V->loadWaitFrames_ > 0) {
    //     return 0;
    // }
    return 0;
}

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

    dsDestroy(&V->diffFilename1_);
    dsDestroy(&V->diffFilename2_);

    int loadIndex = V->imageFileIndex_ + offset;
    if (loadIndex < 0) {
        loadIndex = (int)daSize(&V->filenames_) - 1;
    }
    if (loadIndex >= daSize(&V->filenames_)) {
        loadIndex = 0;
    }
    V->imageFileIndex_ = loadIndex;

    if (!vantageCanLoad(V)) {
        return;
    }

    vantageUnload(V);
    clearOverlay(V);

    char * filename = V->filenames_[V->imageFileIndex_];

    // consume the next index and reset it
    V->C->params.frameIndex = V->imageVideoFrameNextIndex_;
    V->imageVideoFrameNextIndex_ = 0;

    const char * outFormatName = NULL;
    V->imageFileSize_ = clFileSize(filename);
    V->imageFileSize2_ = 0;
    V->image_ = clContextRead(V->C, filename, NULL, &outFormatName);
    V->imageVideoFrameIndex_ = V->C->readExtraInfo.frameIndex;
    V->imageVideoFrameCount_ = V->C->readExtraInfo.frameCount;
    V->imageVideoFrameIndexSlider_.min = 0;
    V->imageVideoFrameIndexSlider_.max = (V->imageVideoFrameCount_ > 0) ? V->imageVideoFrameCount_ - 1 : 0;
    if (!outFormatName) {
        outFormatName = "Unknown";
    }

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
        appendOverlay(V, "[%d/%d] Loaded (%s): %s", V->imageFileIndex_ + 1, daSize(&V->filenames_), outFormatName, shortFilename);
    } else {
        appendOverlay(V, "[%d/%d] Failed to load (%s): %s", V->imageFileIndex_ + 1, daSize(&V->filenames_), outFormatName, shortFilename);
    }
}

void vantageLoadDiff(Vantage * V, const char * filename1, const char * filename2)
{
    if (filename1 && filename2) {
        dsCopy(&V->diffFilename1_, filename1);
        dsCopy(&V->diffFilename2_, filename2);
    }
    if (!V->diffFilename1_ || !V->diffFilename2_) {
        return;
    }
    if (!vantageCanLoad(V)) {
        return;
    }

    vantageUnload(V);
    vantageFileListClear(V);
    clearOverlay(V);

    const char * failureReason = NULL;
    V->imageFileSize_ = clFileSize(V->diffFilename1_);
    V->imageFileSize2_ = clFileSize(V->diffFilename2_);
    V->image_ = clContextRead(V->C, V->diffFilename1_, NULL, NULL);
    V->image2_ = clContextRead(V->C, V->diffFilename2_, NULL, NULL);
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
    const char * short1 = V->diffFilename1_;
    const char * short2 = V->diffFilename2_;
    while (*short1 && (*short1 == *short2)) {
        ++short1;
        ++short2;
    }
    // now walk back the pointers to the nearest path separator, if any
    while (short1 != V->diffFilename1_) {
        --short1;
        if (*short1 == '\\') {
            ++short1;
            break;
        }
    }
    while (short2 != V->diffFilename2_) {
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

static void vantageReload(Vantage * V)
{
    V->inReload_ = 1;
    if ((dsLength(&V->diffFilename1_) > 0) && (dsLength(&V->diffFilename2_) > 0)) {
        vantageLoadDiff(V, NULL, NULL);
    } else if (daSize(&V->filenames_) > 0) {
        vantageLoad(V, 0);
    }
    V->inReload_ = 0;
}

void vantageRefresh(Vantage * V)
{
    V->loadWaitFrames_ = LOADING_FRAMES;
    V->imageVideoFrameNextIndex_ = V->imageVideoFrameIndex_;
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

void vantageSetVideoFrameIndex(Vantage * V, int videoFrameIndex)
{
    if (videoFrameIndex >= V->imageVideoFrameCount_) {
        videoFrameIndex = V->imageVideoFrameCount_ - 1;
    }
    if (videoFrameIndex < 0) {
        videoFrameIndex = 0;
    }

    V->imageVideoFrameNextIndex_ = videoFrameIndex;
    vantageLoad(V, 0);
}

void vantageSetVideoFrameIndexPercentOffset(Vantage * V, int percentOffset)
{
    int absPercent = abs(percentOffset);
    int sign = 1;
    if (percentOffset < 0) {
        sign = -1;
    }

    int offset = 1;
    if ((V->imageVideoFrameCount_ > 1) && (absPercent > 0)) {
        offset = V->imageVideoFrameCount_ * absPercent / 100;
    }
    if (offset < 1) {
        offset = 1;
    }

    vantageSetVideoFrameIndex(V, V->imageVideoFrameIndex_ + (sign * offset));
}

void vantageToggleTonemapSliders(Vantage * V)
{
    V->tonemapSlidersEnabled_ = !V->tonemapSlidersEnabled_;
    vantagePrepareImage(V);
    vantageKickOverlay(V);
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
// Control handling

static void vantageControlClick(Vantage * V, Control * control, int x, int y)
{
    int pos = x - control->x;
    pos = CL_CLAMP(pos, 0, control->w);

    float offset = 0.0f;
    if (control->w > 0) {
        offset = (float)pos / (float)control->w;
    }

    int controlValue;
    if (control->flags & CONTROLFLAG_FLOAT) {
        controlValue = (int)(*((float *)control->value) * 1000.0f);
    } else {
        controlValue = *control->value;
    }

    float range = (float)(control->max - control->min);
    if (range > 0) {
        controlValue = control->step * (int)floorf(0.5f + ((control->min + (int)(offset * range)) / control->step));
        controlValue = CL_CLAMP(controlValue, control->min, control->max);
    } else {
        controlValue = control->min;
    }

    if (control->flags & CONTROLFLAG_FLOAT) {
        float * floatValue = (float *)control->value;
        *floatValue = (float)controlValue / 1000.0f;
    } else {
        *control->value = controlValue;
    }

    vantageKickOverlay(V);
}

static Control * vantageControlFromPoint(Vantage * V, int x, int y)
{
    for (int i = 0; i < daSize(&V->activeControls_); ++i) {
        Control * control = V->activeControls_[i];
        if (x < control->x)
            continue;
        if (y < control->y)
            continue;
        if (x > (control->x + control->w))
            continue;
        if (y > (control->y + control->h))
            continue;

        return control;
    }
    return NULL;
}

// --------------------------------------------------------------------------------------
// Mouse handling

void vantageMouseLeftDoubleClick(Vantage * V, int x, int y)
{
    if (vantageControlFromPoint(V, x, y)) {
        return;
    }

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
    V->dragLastX_ = x;
    V->dragLastY_ = y;

    Control * control = vantageControlFromPoint(V, x, y);
    if (control) {
        V->dragControl_ = control;
        vantageControlClick(V, V->dragControl_, x, y);
        return;
    }

    V->dragging_ = 1;
}

void vantageMouseLeftUp(Vantage * V, int x, int y)
{
    if ((x == -1) && (y == -1)) {
        x = V->dragLastX_;
        y = V->dragLastY_;
    }

    if (V->dragControl_) {
        vantageControlClick(V, V->dragControl_, x, y);
        if (V->dragControl_->flags & CONTROLFLAG_RELOAD) {
            vantageSetVideoFrameIndex(V, V->imageVideoFrameIndex_);
        } else if (V->dragControl_->flags & CONTROLFLAG_PREPARE) {
            vantagePrepareImage(V);
        }

        V->dragControl_ = NULL;
    }

    V->dragging_ = 0;
}

void vantageMouseMove(Vantage * V, int x, int y)
{
    if (V->dragControl_) {
        vantageControlClick(V, V->dragControl_, x, y);
    } else if (V->dragging_) {
        float dx = (float)(x - V->dragLastX_);
        float dy = (float)(y - V->dragLastY_);
        V->imagePosX_ += dx;
        V->imagePosY_ += dy;
    }

    V->dragLastX_ = x;
    V->dragLastY_ = y;
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
    if (vantageControlFromPoint(V, x, y)) {
        return;
    }

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

void vantagePlatformSetHDRAvailable(Vantage * V, int hdrAvailable)
{
    V->platformHDRAvailable_ = hdrAvailable;
}

void vantagePlatformSetSize(Vantage * V, int width, int height)
{
    if ((V->platformW_ != width) || (V->platformH_ != height)) {
        V->platformW_ = width;
        V->platformH_ = height;

        vantageResetImagePos(V);
    }
}

void vantagePlatformSetLinear(Vantage * V, int linear)
{
    if (V->platformLinear_ != linear) {
        V->platformLinear_ = linear;
        vantagePrepareImage(V);
    }
}

void vantagePlatformSetMaxEDR(Vantage * V, float maxEDR)
{
    V->platformMaxEDR_ = maxEDR;
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

    V->C->defaultLuminance = V->unspecLuminance_;

    clTonemapParams * preparedTonemap = &V->preparedTonemap_;
    int preparedTonemapLuminance = V->preparedTonemapLuminance_;

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
                secondImage = clImageConvert(V->C, V->image2_, V->image_->depth, V->image_->profile, CL_TONEMAP_OFF, NULL);
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

            V->imageDiff_ = clImageDiffCreate(V->C, V->image_, secondImage, minIntensity, V->diffThreshold_);

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
                // Don't tonemap the diff
                preparedTonemap = NULL;
                preparedTonemapLuminance = SRGB_LUMINANCE_DEF;

                srcImage = V->imageDiff_->image;
                break;
        }
    } else {
        // Just show an image like normal
        srcImage = V->image_;
    }

    if (srcImage) {
        if (V->diffMode_ != DIFFMODE_SHOWDIFF) {
            clProfileQuery(V->C, srcImage->profile, NULL, NULL, &V->imageLuminance_);

            if (V->srgbHighlight_) {
                if (V->imageHighlight_) {
                    clImageDestroy(V->C, V->imageHighlight_);
                    V->imageHighlight_ = NULL;
                }
                if (V->highlightInfo_) {
                    clImageSRGBHighlightPixelInfoDestroy(V->C, V->highlightInfo_);
                    V->highlightInfo_ = NULL;
                }
                V->highlightInfo_ = clImageSRGBHighlightPixelInfoCreate(V->C, srcImage->width * srcImage->height);
                V->imageHighlight_ =
                    clImageCreateSRGBHighlight(V->C, srcImage, V->srgbLuminance_, &V->highlightStats_, V->highlightInfo_);
                srcImage = V->imageHighlight_;

                // Don't tonemap the SRGB highlight
                preparedTonemap = NULL;
                preparedTonemapLuminance = SRGB_LUMINANCE_DEF;
            }
        }

        clProfilePrimaries primaries;
        clProfileCurve curve;

        int dstLuminance = 10000;
        if (V->platformHDRActive_ && V->wantsHDR_) {
            clContextGetStockPrimaries(V->C, "bt2020", &primaries);
            if (V->platformLinear_) {
                curve.type = CL_PCT_GAMMA;
            } else {
                curve.type = CL_PCT_PQ;
            }
            curve.implicitScale = 1.0f;
            curve.gamma = 1.0f;
            V->imageHDR_ = 1;
        } else {
            clContextGetStockPrimaries(V->C, "bt709", &primaries);
            curve.type = CL_PCT_GAMMA;
            if (V->platformLinear_) {
                curve.gamma = 1.0f;
            } else {
                curve.gamma = 2.2f;
            }
            dstLuminance = preparedTonemapLuminance;
            V->imageHDR_ = 0;
        }
        curve.implicitScale = 1.0f;

        clProfile * profile = clProfileCreate(V->C, &primaries, &curve, dstLuminance, NULL);
        V->preparedImage_ = clImageConvert(V->C, srcImage, 16, profile, CL_TONEMAP_AUTO, preparedTonemap);
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

static void vantageFill(Vantage * V, float dx, float dy, float dw, float dh, const Color * color)
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

static float vantageScaleTextLuminance(Vantage * V, float lum)
{
    if (V->platformHDRActive_) {
        // assume lum is SDR white level with a ~2.2 gamma, convert to PQ
        lum = powf(lum, 2.2f);
        lum *= (float)TEXT_LUMINANCE / 10000.0f;
        if (!V->platformLinear_) {
            lum = PQ_OETF(lum);
        }
    }
    return lum;
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
                    color.r = vantageScaleTextLuminance(V, r / 255.0f);
                    color.g = vantageScaleTextLuminance(V, g / 255.0f);
                    color.b = vantageScaleTextLuminance(V, b / 255.0f);
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

static void vantageRenderControl(Vantage * V, Control * control, float x, float y, float w, float h)
{
    if (control->type != CONTROLTYPE_SLIDER) {
        return;
    }

    control->x = (int)x;
    control->y = (int)y;
    control->w = (int)w;
    control->h = (int)h;
    daPush(&V->activeControls_, control);

    float lum = vantageScaleTextLuminance(V, 1.0f);
    float halfLum = vantageScaleTextLuminance(V, 0.5f);
    float qLum = vantageScaleTextLuminance(V, 0.25f);
    Color barColor = { halfLum, halfLum, halfLum, 1.0f };
    Color sliderColor = { lum, halfLum, qLum, 1.0f };
    const float sideHeight = h / 2.0f;
    const float barThickness = 4.0f;

    int controlValue;
    if (control->flags & CONTROLFLAG_FLOAT) {
        controlValue = (int)(*((float *)control->value) * 1000.0f);
    } else {
        controlValue = *control->value;
    }

    float offset = 0.0f;
    float range = (float)(control->max - control->min);
    if (range > 0.0f) {
        offset = (controlValue - control->min) / range;
    }

    vantageFill(V, x, y + (h / 2) - (barThickness / 2), w, barThickness, &barColor);            // Middle
    vantageFill(V, x, y + (h / 2) - (sideHeight / 2), barThickness, sideHeight, &barColor);     // Left side
    vantageFill(V, x + w, y + (h / 2) - (sideHeight / 2), barThickness, sideHeight, &barColor); // Right side
    vantageFill(V, x + (w * offset), y, barThickness, h, &sliderColor);                         // Slider
}

static void vantageRenderNextLine(Vantage * V, const char * format, ...)
{
    va_list args;
    va_start(args, format);

    dsClear(&V->tempTextBuffer_);
    dsConcatv(&V->tempTextBuffer_, format, args);
    va_end(args);

    vantageBlitString(V, V->tempTextBuffer_, V->nextLineX_, V->nextLineY_, V->nextLineFontSize_, &V->nextLineColor_);
    V->nextLineY_ += V->nextLineHeight_;
}

static void vantageRenderInfo(Vantage * V, float left, float top, float fontHeight, float nextLine, Color * color)
{
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

        V->nextLineX_ = left;
        V->nextLineY_ = top;
        V->nextLineFontSize_ = fontHeight;
        V->nextLineHeight_ = nextLine;
        V->nextLineColor_ = *color;

        vantageRenderNextLine(V, "Dimensions     : %dx%d (%d bpc)", width, height, depth);

        if (fileSize > 0) {
            if (fileSize > (1024 * 1024)) {
                vantageRenderNextLine(V, "File Size      : %2.2f MB", (float)fileSize / (1024.0f * 1024.0f));
            } else if (fileSize > 1024) {
                vantageRenderNextLine(V, "File Size      : %2.2f KB", (float)fileSize / 1024.0f);
            } else {
                vantageRenderNextLine(V, "File Size      : %d bytes", fileSize);
            }
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
            vantageRenderNextLine(V, "Showing        : %s", showing);
        }
    }

    if ((V->imageInfoX_ != -1) && (V->imageInfoY_ != -1)) {
        clImagePixelInfo * pixelInfo = &V->pixelInfo_;

        int depthU16 = CL_CLAMP(V->image_->depth, 8, 16);
        char * space = (depthU16 < 10) ? " " : "";

        vantageRenderNextLine(V, "");
        vantageRenderNextLine(V, "[%d, %d]", V->imageInfoX_, V->imageInfoY_);
        vantageRenderNextLine(V, "");
        vantageRenderNextLine(V, "Image Info:");
        vantageRenderNextLine(V, "  R UNorm(%d)%s  : %d", depthU16, space, pixelInfo->rawR);
        vantageRenderNextLine(V, "  G UNorm(%d)%s  : %d", depthU16, space, pixelInfo->rawG);
        vantageRenderNextLine(V, "  B UNorm(%d)%s  : %d", depthU16, space, pixelInfo->rawB);
        vantageRenderNextLine(V, "  A UNorm(%d)%s  : %d", depthU16, space, pixelInfo->rawA);
        vantageRenderNextLine(V, "  R FP32       : %2.2f", pixelInfo->normR);
        vantageRenderNextLine(V, "  G FP32       : %2.2f", pixelInfo->normG);
        vantageRenderNextLine(V, "  B FP32       : %2.2f", pixelInfo->normB);
        vantageRenderNextLine(V, "  A FP32       : %2.2f", pixelInfo->normA);
        vantageRenderNextLine(V, "  x            : %4.4f", pixelInfo->x);
        vantageRenderNextLine(V, "  y            : %4.4f", pixelInfo->y);
        vantageRenderNextLine(V, "  Y            : %4.4f", pixelInfo->Y);
        if (V->image2_) {
            pixelInfo = &V->pixelInfo2_;

            depthU16 = CL_CLAMP(V->image2_->depth, 8, 16);
            space = (depthU16 < 10) ? " " : "";

            vantageRenderNextLine(V, "");
            vantageRenderNextLine(V, "Image Info (2):");
            vantageRenderNextLine(V, "  R UNorm(%d)%s  : %d", depthU16, space, pixelInfo->rawR);
            vantageRenderNextLine(V, "  G UNorm(%d)%s  : %d", depthU16, space, pixelInfo->rawG);
            vantageRenderNextLine(V, "  B UNorm(%d)%s  : %d", depthU16, space, pixelInfo->rawB);
            vantageRenderNextLine(V, "  A UNorm(%d)%s  : %d", depthU16, space, pixelInfo->rawA);
            vantageRenderNextLine(V, "  R FP32       : %2.2f", pixelInfo->normR);
            vantageRenderNextLine(V, "  G FP32       : %2.2f", pixelInfo->normG);
            vantageRenderNextLine(V, "  B FP32       : %2.2f", pixelInfo->normB);
            vantageRenderNextLine(V, "  A FP32       : %2.2f", pixelInfo->normA);
            vantageRenderNextLine(V, "  x            : %4.4f", pixelInfo->x);
            vantageRenderNextLine(V, "  y            : %4.4f", pixelInfo->y);
            vantageRenderNextLine(V, "  Y            : %4.4f", pixelInfo->Y);
        }
    }

    if (V->srgbHighlight_ && V->highlightInfo_) {
        if ((V->imageInfoX_ != -1) && (V->imageInfoY_ != -1)) {
            clImageSRGBHighlightPixel * highlightPixel =
                &V->highlightInfo_->pixels[V->imageInfoX_ + (V->imageInfoY_ * V->image_->width)];
            vantageRenderNextLine(V, "");
            vantageRenderNextLine(V, "Pixel Highlight:");
            vantageRenderNextLine(V, "  Nits         : %2.2f", highlightPixel->nits);
            vantageRenderNextLine(V, "  SRGB Max Nits: %2.2f", highlightPixel->maxNits);
            vantageRenderNextLine(V, "  Overbright   : %2.2f%%", 100.0f * highlightPixel->nits / highlightPixel->maxNits);
            vantageRenderNextLine(V, "  Out of Gamut : %2.2f%%", 100.0f * highlightPixel->outOfGamut);
        }
        vantageRenderNextLine(V, "");
        vantageRenderNextLine(V, "Highlight Stats:");
        vantageRenderNextLine(V, "Total Pixels   : %d", V->image_->width * V->image_->height);
        vantageRenderNextLine(V,
                              "  Overbright   : %d (%.1f%%)",
                              V->highlightStats_.overbrightPixelCount,
                              100.0f * V->highlightStats_.overbrightPixelCount / V->highlightStats_.pixelCount);
        vantageRenderNextLine(V,
                              "  Out of Gamut : %d (%.1f%%)",
                              V->highlightStats_.outOfGamutPixelCount,
                              100.0f * V->highlightStats_.outOfGamutPixelCount / V->highlightStats_.pixelCount);
        vantageRenderNextLine(V,
                              "  Both         : %d (%.1f%%)",
                              V->highlightStats_.bothPixelCount,
                              100.0f * V->highlightStats_.bothPixelCount / V->highlightStats_.pixelCount);
        vantageRenderNextLine(V,
                              "  HDR Pixels   : %d (%.1f%%)",
                              V->highlightStats_.hdrPixelCount,
                              100.0f * V->highlightStats_.hdrPixelCount / V->highlightStats_.pixelCount);
        vantageRenderNextLine(V, "  Brightest    : [%d, %d]", V->highlightStats_.brightestPixelX, V->highlightStats_.brightestPixelY);
        vantageRenderNextLine(V, "                 %2.2f nits", V->highlightStats_.brightestPixelNits);
    }

    if (V->imageDiff_ && (V->diffMode_ == DIFFMODE_SHOWDIFF)) {
        vantageRenderNextLine(V, "");
        vantageRenderNextLine(V, "Threshold      : %d", V->diffThreshold_);
        vantageRenderNextLine(V, "Largest Diff   : %d", V->imageDiff_->largestChannelDiff);
        if ((V->imageInfoX_ != -1) && (V->imageInfoY_ != -1)) {
            int diff = V->imageDiff_->diffs[V->imageInfoX_ + (V->imageInfoY_ * V->imageDiff_->image->width)];
            vantageRenderNextLine(V, "Pixel Diff     : %d", diff);
        }

        vantageRenderNextLine(V, "");
        vantageRenderNextLine(V, "Total Pixels   : %d", V->image_->width * V->image_->height);
        vantageRenderNextLine(V,
                              "Perfect Matches: %7d (%.1f%%)",
                              V->imageDiff_->matchCount,
                              (100.0f * (float)V->imageDiff_->matchCount / V->imageDiff_->pixelCount));
        vantageRenderNextLine(V,
                              "Under Threshold: %7d (%.1f%%)",
                              V->imageDiff_->underThresholdCount,
                              (100.0f * (float)V->imageDiff_->underThresholdCount / V->imageDiff_->pixelCount));
        vantageRenderNextLine(V,
                              "Over Threshold : %7d (%.1f%%)",
                              V->imageDiff_->overThresholdCount,
                              (100.0f * (float)V->imageDiff_->overThresholdCount / V->imageDiff_->pixelCount));
    }
}

void vantageRender(Vantage * V)
{
    static const float fontHeight = 18.0f;
    static const float nextLine = 20.0f;

    daClear(&V->blits_, NULL);
    daClear(&V->activeControls_, NULL);

    V->wantedHDR_ = V->wantsHDR_;
    V->wantsHDR_ = !V->tonemapSlidersEnabled_;

    if (V->wantedHDR_ != V->wantsHDR_) {
        vantagePrepareImage(V);
    }

    if (V->loadWaitFrames_ > 0) {
        --V->loadWaitFrames_;
        if (V->loadWaitFrames_ == 0) {
            vantageReload(V);
        }

        float lum = vantageScaleTextLuminance(V, 1.0f);
        Color loadingTextColor = { lum, lum, lum, 1.0f };
        dsClear(&V->tempTextBuffer_);
        if ((dsLength(&V->diffFilename1_) > 0) && (dsLength(&V->diffFilename2_) > 0)) {
            dsPrintf(&V->tempTextBuffer_, "Loading Diff: %s, %s", V->diffFilename1_, V->diffFilename2_);
        } else if ((daSize(&V->filenames_) > 0) && (V->imageFileIndex_ >= 0) && (V->imageFileIndex_ < (int)daSize(&V->filenames_))) {
            if (V->imageVideoFrameNextIndex_ > 0) {
                dsPrintf(&V->tempTextBuffer_,
                         "[%d/%d] Loading: %s @ Frame %d",
                         V->imageFileIndex_ + 1,
                         daSize(&V->filenames_),
                         V->filenames_[V->imageFileIndex_],
                         V->imageVideoFrameNextIndex_);
            } else {
                dsPrintf(&V->tempTextBuffer_, "[%d/%d] Loading: %s", V->imageFileIndex_ + 1, daSize(&V->filenames_), V->filenames_[V->imageFileIndex_]);
            }
        }
        vantageBlitString(V, V->tempTextBuffer_, 10, 10, fontHeight, &loadingTextColor);
        return;
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

    double dt = now() - V->overlayStart_;
    if (dt < V->overlayDuration_) {
        float clientW = (float)V->platformW_;
        float clientH = (float)V->platformH_;

        const float infoW = 360.0f;
        const float infoMargin = 10.0f;
        float left = (clientW - infoW);
        float top = 10.0f;

        if ((V->imageInfoX_ != -1) || (V->imageInfoY_ != -1) || V->srgbHighlight_ || V->tonemapSlidersEnabled_ ||
            (V->imageDiff_ && (V->diffMode_ == DIFFMODE_SHOWDIFF))) {
            float blackW = infoW;
            float blackH = clientH;
            Color black = { 0.0f, 0.0f, 0.0f, 0.8f };
            if (V->platformLinear_) {
                black.a = 0.95f;
            }
            vantageFill(V, left, 0, blackW, blackH, &black);
        }

        float alpha = 1.0f;
        if (dt > (V->overlayDuration_ - V->overlayFade_)) {
            alpha = (float)(V->overlayFade_ - (dt - (V->overlayDuration_ - V->overlayFade_)));
        }
        float lum = vantageScaleTextLuminance(V, 1.0f);
        Color color = { lum, lum, lum, alpha };

        // Draw the bottom left
        {
            float blTop = clientH - 25.0f;
            clProfile * profile = NULL;
            if (V->image_) {
                if (V->imageDiff_) {
                    switch (V->diffMode_) {
                        case DIFFMODE_SHOW1:
                            profile = V->image_->profile;
                            break;
                        case DIFFMODE_SHOW2:
                            profile = V->image2_->profile;
                            break;
                        case DIFFMODE_SHOWDIFF:
                            break;
                    }
                } else {
                    profile = V->image_->profile;
                }
            }
            if (profile) {
                char profileDescription[256];
                clProfileDescribe(V->C, profile, profileDescription, sizeof(profileDescription));
                const char * profileName = profile->description ? profile->description : "";
                dsPrintf(&V->tempTextBuffer_, "Profile: %s (%s)", profileName, profileDescription);
                vantageBlitString(V, V->tempTextBuffer_, 10, blTop, fontHeight, &color);
                blTop -= nextLine;
            }

            if (V->platformHDRAvailable_) {
                if (V->wantsHDR_) {
                    if (V->platformMaxEDR_ > 0.0f) {
                        dsPrintf(&V->tempTextBuffer_, "HDR    : Active (maxEDR: %g)", V->platformMaxEDR_);
                    } else {
                        dsPrintf(&V->tempTextBuffer_, "HDR    : Active");
                    }
                    vantageBlitString(V, V->tempTextBuffer_, 10, blTop, fontHeight, &color);
                } else {
                    // If there are reasons other than tonemapSlidersEnabled_ to forcibly disable
                    // wantsHDR_, switch here
                    dsPrintf(&V->tempTextBuffer_, "HDR    : Inactive (Tonemap Sliders)");
                    vantageBlitString(V, V->tempTextBuffer_, 10, blTop, fontHeight, &color);
                }
            } else {
                dsPrintf(&V->tempTextBuffer_, "HDR    : Unavailable");
                vantageBlitString(V, V->tempTextBuffer_, 10, blTop, fontHeight, &color);
            }
            blTop -= nextLine;
        }

        left += infoMargin; // right text margin

        // Draw the bottom right (sliders)
        {
            float blTop = clientH - 25.0f;

            // Draw tonemap sliders
            if (V->tonemapSlidersEnabled_) {
                int imageLuminance = V->imageLuminance_;
                if (imageLuminance == CL_LUMINANCE_UNSPECIFIED) {
                    imageLuminance = V->unspecLuminance_;
                }

                vantageRenderControl(V, &V->preparedTonemapContrastSlider_, left, blTop, infoW - (infoMargin * 2), fontHeight);
                blTop -= nextLine;
                dsPrintf(&V->tempTextBuffer_, "Tonemap Contrast : %3.3f", V->preparedTonemap_.contrast);
                vantageBlitString(V, V->tempTextBuffer_, left, blTop, fontHeight, &color);
                blTop -= nextLine;

                vantageRenderControl(V, &V->preparedTonemapClipPointSlider_, left, blTop, infoW - (infoMargin * 2), fontHeight);
                blTop -= nextLine;
                dsPrintf(&V->tempTextBuffer_, "Tonemap ClipPoint: %3.3f", V->preparedTonemap_.clippingPoint);
                vantageBlitString(V, V->tempTextBuffer_, left, blTop, fontHeight, &color);
                blTop -= nextLine;

                vantageRenderControl(V, &V->preparedTonemapSpeedSlider_, left, blTop, infoW - (infoMargin * 2), fontHeight);
                blTop -= nextLine;
                dsPrintf(&V->tempTextBuffer_, "Tonemap Speed    : %3.3f", V->preparedTonemap_.speed);
                vantageBlitString(V, V->tempTextBuffer_, left, blTop, fontHeight, &color);
                blTop -= nextLine;

                vantageRenderControl(V, &V->preparedTonemapPowerSlider_, left, blTop, infoW - (infoMargin * 2), fontHeight);
                blTop -= nextLine;
                dsPrintf(&V->tempTextBuffer_, "Tonemap Power    : %3.3f", V->preparedTonemap_.power);
                vantageBlitString(V, V->tempTextBuffer_, left, blTop, fontHeight, &color);
                blTop -= nextLine;

                vantageRenderControl(V, &V->preparedTonemapLuminanceSlider_, left, blTop, infoW - (infoMargin * 2), fontHeight);
                blTop -= nextLine;
                dsPrintf(&V->tempTextBuffer_,
                         "Tonemap Luminance: %d nit%s (%s)",
                         V->preparedTonemapLuminance_,
                         (V->preparedTonemapLuminance_ > 1) ? "s" : "",
                         (imageLuminance > V->preparedTonemapLuminance_) ? "On" : "Off");
                vantageBlitString(V, V->tempTextBuffer_, left, blTop, fontHeight, &color);
                blTop -= nextLine;
            }

            if (V->imageVideoFrameCount_ > 1) {
                vantageRenderControl(V, &V->imageVideoFrameIndexSlider_, left, blTop, infoW - (infoMargin * 2), fontHeight);
                blTop -= nextLine;

                dsPrintf(&V->tempTextBuffer_,
                         "Frame  : %d / %d (%d total)",
                         V->imageVideoFrameIndex_,
                         V->imageVideoFrameCount_ - 1,
                         V->imageVideoFrameCount_);
                vantageBlitString(V, V->tempTextBuffer_, left, blTop, fontHeight, &color);
                blTop -= nextLine;
            }

            if ((V->diffMode_ != DIFFMODE_SHOWDIFF) && (V->imageLuminance_ == CL_LUMINANCE_UNSPECIFIED)) {
                vantageRenderControl(V, &V->unspecLuminanceSlider_, left, blTop, infoW - (infoMargin * 2), fontHeight);
                blTop -= nextLine;

                dsPrintf(&V->tempTextBuffer_, "Unspec Luminance : %d nit%s", V->unspecLuminance_, (V->unspecLuminance_ > 1) ? "s" : "");
                vantageBlitString(V, V->tempTextBuffer_, left, blTop, fontHeight, &color);
                blTop -= nextLine;
            }

            if (V->srgbHighlight_) {
                vantageRenderControl(V, &V->srgbLuminanceSlider_, left, blTop, infoW - (infoMargin * 2), fontHeight);
                blTop -= nextLine;

                dsPrintf(&V->tempTextBuffer_, "SRGB Threshold   : %d nit%s", V->srgbLuminance_, (V->srgbLuminance_ > 1) ? "s" : "");
                vantageBlitString(V, V->tempTextBuffer_, left, blTop, fontHeight, &color);
                blTop -= nextLine;
            }
        }

        for (int i = 0; i < daSize(&V->overlay_); ++i) {
            vantageBlitString(V, V->overlay_[i], 10, top + (i * nextLine), fontHeight, &color);
        }

        vantageRenderInfo(V, left, top, fontHeight, nextLine, &color);
    }
}

int vantageImageUsesLinearSampling(Vantage * V)
{
    if (V->diffMode_ == DIFFMODE_SHOWDIFF) {
        return 0;
    }
    if (V->imageInfoX_ != -1) {
        return 0;
    }
    return 1;
}

// This could potentially use clFormatDetect() instead, but that'd cause a lot of header reads.
int vantageIsImageFile(const char * filename)
{
    const char * ext = strrchr(filename, '.');
    if (ext) {
        if (!strcmp(ext, ".avif"))
            return 1;
        if (!strcmp(ext, ".avifs"))
            return 1;
        if (!strcmp(ext, ".bmp"))
            return 1;
        if (!strcmp(ext, ".jpg"))
            return 1;
        if (!strcmp(ext, ".jpeg"))
            return 1;
        if (!strcmp(ext, ".jp2"))
            return 1;
        if (!strcmp(ext, ".j2k"))
            return 1;
        if (!strcmp(ext, ".mp4")) // I don't love this, but av01 inside a .mp4 is supported, so I'l add it
            return 1;
        if (!strcmp(ext, ".png"))
            return 1;
        if (!strcmp(ext, ".tif"))
            return 1;
        if (!strcmp(ext, ".tiff"))
            return 1;
        if (!strcmp(ext, ".webp"))
            return 1;
    }
    return 0;
}
