#include "vantage.h"

Vantage::Vantage(HINSTANCE hInstance)
    : hInstance_(hInstance)
    , fullscreen_(false)
    , hdrActive_(false)
    , imageHDR_(false)
    , srgbHighlight_(false)
    , imageWhiteLevel_(0)
    , diffThreshold_(0)
    , diffMode_(DIFFMODE_SHOWDIFF)
    , diffIntensity_(DIFFINTENSITY_BRIGHT)
    , dragging_(false)
    , coloristContext_(nullptr)
    , coloristImage_(nullptr)
    , coloristImage2_(nullptr)
    , coloristImageDiff_(nullptr)
    , coloristImageHighlight_(nullptr)
    , coloristHighlightInfo_(nullptr)
    , overlayTick_(0)
    , imageInfoX_(-1)
    , imageInfoY_(-1)

    , driverType_(D3D_DRIVER_TYPE_NULL)
    , featureLevel_(D3D_FEATURE_LEVEL_11_0)
    , device_(nullptr)
    , device1_(nullptr)
    , context_(nullptr)
    , context1_(nullptr)
    , defaultAdapter_(nullptr)
    , swapChain_(nullptr)
    , swapChain1_(nullptr)
    , renderTarget_(nullptr)
    , vertexShader_(nullptr)
    , pixelShader_(nullptr)
    , vertexLayout_(nullptr)
    , constantBuffer_(nullptr)
    , vertexBuffer_(nullptr)
    , indexBuffer_(nullptr)
    , sampler_(nullptr)
    , image_(nullptr)
{
    // TODO: read from / write to disk
    windowPos_.x = 0;
    windowPos_.y = 0;
    windowPos_.w = 1280;
    windowPos_.h = 720;

    coloristContext_ = clContextCreate(nullptr);
}

Vantage::~Vantage()
{
    unloadImage();
    clContextDestroy(coloristContext_);
}

void Vantage::cleanup()
{
    destroyDevice();
}

bool Vantage::init(const char * filename1, const char * filename2)
{
    if (!createWindow()) {
        return false;
    }
    if (createDevice() != S_OK) {
        return false;
    }
    checkHDR();

    if (filename1[0] && filename2[0]) {
        loadDiff(filename1, filename2);
    } else if (filename1[0]) {
        loadImage(filename1);
    }

    return true;
}

void Vantage::kickOverlay()
{
    overlayTick_ = GetTickCount();
}

void Vantage::killOverlay()
{
    overlayTick_ = GetTickCount() - overlayDuration_;
    imageInfoX_ = -1;
    imageInfoY_ = -1;
}

void Vantage::appendOverlay(const char * format, ...)
{
    const int stackBufferSize = 256;
    char stackBuffer[stackBufferSize];
    char * buffer = stackBuffer;
    va_list args;
    va_start(args, format);
    int needed = vsnprintf(NULL, 0, format, args);
    if (needed + 1 > stackBufferSize) {
        buffer = (char *)malloc(needed + 1);
    }

    va_start(args, format);
    vsnprintf(buffer, needed + 1, format, args);
    buffer[needed] = 0;

    overlay_.push_back(buffer);
    if (overlay_.size() > MAX_OVERLAY_LINES) {
        overlay_.erase(overlay_.begin());
    }

    if (buffer != stackBuffer) {
        free(buffer);
    }

    kickOverlay();
}

void Vantage::clearOverlay()
{
    overlay_.clear();
}

void Vantage::appendInfo(const char * format, ...)
{
    const int stackBufferSize = 256;
    char stackBuffer[stackBufferSize];
    char * buffer = stackBuffer;
    va_list args;
    va_start(args, format);
    int needed = vsnprintf(NULL, 0, format, args);
    if (needed + 1 > stackBufferSize) {
        buffer = (char *)malloc(needed + 1);
    }

    va_start(args, format);
    vsnprintf(buffer, needed + 1, format, args);
    buffer[needed] = 0;

    info_.push_back(buffer);

    if (buffer != stackBuffer) {
        free(buffer);
    }
}
