#ifndef VANTAGE_H
#define VANTAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "colorist/colorist.h"
#include "dyn.h"

#include "colorist/version.h"
#include "version.h"
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define VANTAGE_WINDOW_TITLE "Vantage " STR(VANTAGE_VERSION) " (colorist " COLORIST_VERSION_STRING ")"

typedef enum DiffMode
{
    DIFFMODE_SHOW1 = 0,
    DIFFMODE_SHOW2,
    DIFFMODE_SHOWDIFF
} DiffMode;

typedef enum DiffIntensity
{
    DIFFINTENSITY_ORIGINAL = 0,
    DIFFINTENSITY_BRIGHT,
    DIFFINTENSITY_DIFFONLY
} DiffIntensity;

typedef enum BlitMode
{
    BM_IMAGE = 0,
    BM_CIE_BACKGROUND,
    BM_CIE_CROSSHAIR,
    BM_FILL,
    BM_TEXT
} BlitMode;

typedef struct Color
{
    float r;
    float g;
    float b;
    float a;
} Color;

typedef struct Blit
{
    float sx, sy, sw, sh;
    float dx, dy, dw, dh;
    Color color;
    BlitMode mode;
} Blit;

typedef enum ControlType
{
    CONTROLTYPE_SLIDER = 0
} ControlType;

typedef enum ControlFlags
{
    CONTROLFLAG_PREPARE = (1 << 0),
    CONTROLFLAG_RELOAD = (1 << 1),
    CONTROLFLAG_FLOAT = (1 << 2), // value is actually a float, slider is in thousandths
} ControlFlags;

typedef struct Control
{
    ControlType type;
    int x, y, w, h;
    int min, max, step;
    int * value;
    uint32_t flags;
} Control;

typedef struct Vantage
{
    // Owned/set by platform
    int platformW_;
    int platformH_;
    int platformHDRActive_;
    int platformHDRAvailable_;
    int platformLinear_;
    float platformMaxEDR_;

    // List of filenames to cycle through (arrow keys)
    char ** filenames_;
    int imageFileIndex_;

    // Special mode state (diff, highlight)
    char * diffFilename1_;
    char * diffFilename2_;
    DiffMode diffMode_;
    DiffIntensity diffIntensity_;
    int diffThreshold_;
    int srgbHighlight_;
    int srgbLuminance_;
    int unspecLuminance_;

    // Colorist objects
    clContext * C;
    clImage * image_;
    clImage * image2_;
    clProfile * forcedProfile_;
    clImage * imageFont_;
    clImage * imageCIEBackground_;
    clImage * imageCIECrosshair_;
    clImageDiff * imageDiff_;
    clImage * imageHighlight_;
    clImage * preparedImage_;
    clImageHDRPixelInfo * highlightInfo_;
    clImageHDRStats highlightStats_;
    clImagePixelInfo pixelInfo_;
    clImagePixelInfo pixelInfo2_;

    // Image state
    int imageFileSize_;
    int imageFileSize2_;
    int imageInfoX_;
    int imageInfoY_;
    int imageHDR_;
    int imageLuminance_;
    int imageDirty_;
    int imageVideoFrameNextIndex_;
    int imageVideoFrameIndex_;
    int imageVideoFrameCount_;
    float imagePosX_; // x
    float imagePosY_; // y
    float imagePosW_; // width
    float imagePosH_; // height
    float imagePosS_; // scale

    // Prepared image tonemapping
    clTonemapParams preparedTonemap_;
    int preparedTonemapLuminance_;
    int preparedMaxEDRClip_; // bool
    Control preparedTonemapContrastSlider_;
    Control preparedTonemapClipPointSlider_;
    Control preparedTonemapSpeedSlider_;
    Control preparedTonemapPowerSlider_;
    Control preparedTonemapLuminanceSlider_;
    int tonemapSlidersEnabled_;

    // Mouse tracking
    int dragging_;
    int dragLastX_;
    int dragLastY_;
    Control * dragControl_;

    // Sliders
    Control srgbLuminanceSlider_;
    Control unspecLuminanceSlider_;
    Control imageVideoFrameIndexSlider_;

    // Loading state (rendering hack)
    int loadWaitFrames_;
    int inReload_;

    // Text information
    double overlayDuration_;
    double overlayStart_;
    double overlayFade_;
    char ** overlay_;

    // Temp buffers
    char * tempTextBuffer_;

    // Rendering state
    Blit * blits_;
    Control ** activeControls_;
    float nextLineX_;
    float nextLineY_;
    float nextLineFontSize_;
    float nextLineHeight_;
    Color nextLineColor_;
    int wantsHDR_;
    int wantedHDR_;
    float lastMaxEDR_;

    // Glyph information
    dynMap * glyphs_;
} Vantage;

// Creation / destruction
Vantage * vantageCreate(void);
void vantageDestroy(Vantage * V);

// Overlay
void vantageKickOverlay(Vantage * V);
void vantageKillOverlay(Vantage * V);

// Load
void vantageFileListClear(Vantage * V);
void vantageFileListAppend(Vantage * V, const char * filename);
void vantageLoad(Vantage * V, int offset);
void vantageLoadDiff(Vantage * V, const char * filename1, const char * filename2);
void vantageUnload(Vantage * V);
void vantageRefresh(Vantage * V);

// Forced profile
void vantageForceProfile(Vantage * V, const char * filename);

// Special mode state
void vantageAdjustThreshold(Vantage * V, int amount);
void vantageSetDiffIntensity(Vantage * V, DiffIntensity diffIntensity);
void vantageSetDiffMode(Vantage * V, DiffMode diffMode);
void vantageToggleSrgbHighlight(Vantage * V);
void vantageSetVideoFrameIndex(Vantage * V, int videoFrameIndex);
void vantageSetVideoFrameIndexPercentOffset(Vantage * V, int percentOffset);
void vantageToggleTonemapSliders(Vantage * V);
void vantageToggleMaxEDRClip(Vantage * V);
void vantageSetUnspecLuminance(Vantage * V, int unspecLuminance);

// Positioning
void vantageCalcCenteredImagePos(Vantage * V, float * posX, float * posY);
void vantageCalcImageSize(Vantage * V);
void vantageResetImagePos(Vantage * V);

// Mouse handling
void vantageMouseLeftDoubleClick(Vantage * V, int x, int y);
void vantageMouseLeftDown(Vantage * V, int x, int y);
void vantageMouseLeftUp(Vantage * V, int x, int y);
void vantageMouseMove(Vantage * V, int x, int y);
void vantageMouseSetPos(Vantage * V, int x, int y);
void vantageMouseWheel(Vantage * V, int x, int y, float delta);

// Platform hooks
void vantagePlatformSetHDRActive(Vantage * V, int hdrActive);
void vantagePlatformSetHDRAvailable(Vantage * V, int hdrAvailable);
void vantagePlatformSetSize(Vantage * V, int width, int height);
void vantagePlatformSetLinear(Vantage * V, int linear); // 0=PQ, 1=Linear
void vantagePlatformSetMaxEDR(Vantage * V, float maxEDR);

// Rendering
void vantagePrepareImage(Vantage * V);
void vantageRender(Vantage * V);
int vantageImageUsesLinearSampling(Vantage * V); // Returns nonzero if images should render with linear sampling
float vantageClipCeiling(Vantage * V);

// Helpers
int vantageIsImageFile(const char * filename);

#ifdef __cplusplus
}
#endif

#endif
