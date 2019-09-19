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

typedef struct Vantage
{
    // Owned/set by platform
    int platformW_;
    int platformH_;
    int platformWhiteLevel_;
    int platformHDRActive_;
    int platformUsesLinear_;

    // List of filenames to cycle through (arrow keys)
    char ** filenames_;
    int imageFileIndex_;

    // Special mode state (diff, highlight)
    DiffMode diffMode_;
    DiffIntensity diffIntensity_;
    int diffThreshold_;
    int srgbHighlight_;

    // Colorist objects
    clContext * C;
    clImage * image_;
    clImage * image2_;
    clImage * imageFont_;
    clImageDiff * imageDiff_;
    clImage * imageHighlight_;
    clImage * preparedImage_;
    clImageSRGBHighlightPixelInfo * highlightInfo_;
    clImageSRGBHighlightStats highlightStats_;
    clImagePixelInfo pixelInfo_;
    clImagePixelInfo pixelInfo2_;

    // Image state
    int imageFileSize_;
    int imageFileSize2_;
    int imageInfoX_;
    int imageInfoY_;
    int imageWhiteLevel_;
    int imageHDR_;
    int imageDirty_;
    float imagePosX_; // x
    float imagePosY_; // y
    float imagePosW_; // width
    float imagePosH_; // height
    float imagePosS_; // scale

    // Mouse tracking
    int dragging_;
    int dragStartX_;
    int dragStartY_;

    // Text information
    double overlayDuration_;
    double overlayStart_;
    double overlayFade_;
    char ** overlay_;
    char ** info_;

    // Rendering state
    Blit * blits_;

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

// Special mode state
void vantageAdjustThreshold(Vantage * V, int amount);
void vantageSetDiffIntensity(Vantage * V, DiffIntensity diffIntensity);
void vantageSetDiffMode(Vantage * V, DiffMode diffMode);
void vantageToggleSrgbHighlight(Vantage * V);

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
void vantagePlatformSetSize(Vantage * V, int width, int height);
void vantagePlatformSetWhiteLevel(Vantage * V, int whiteLevel);
void vantagePlatformSetUsesLinear(Vantage * V, int usesLinear);

// Rendering
void vantagePrepareImage(Vantage * V);
void vantageRender(Vantage * V);

// Helpers
int vantageIsImageFile(const char * filename);

#ifdef __cplusplus
}
#endif

#endif
