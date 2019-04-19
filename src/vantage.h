#ifndef VANTAGE_H
#define VANTAGE_H

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <directxmath.h>
#pragma comment(lib, "d3d11.lib")
using namespace DirectX;

#include "SpriteBatch.h"
#include "SpriteFont.h"

#include <string>
#include <vector>

extern "C" {
#include "colorist/colorist.h"
}

struct ConstantBuffer
{
    XMMATRIX transform;
    XMFLOAT4 params;
};

struct SimpleVertex
{
    XMFLOAT3 Pos;
    XMFLOAT2 Tex;
};

struct WindowPosition
{
    int x;
    int y;
    int w;
    int h;
};

enum DiffMode
{
    DIFFMODE_SHOW1 = 0,
    DIFFMODE_SHOW2,
    DIFFMODE_SHOWDIFF
};

enum DiffIntensity
{
    DIFFINTENSITY_ORIGINAL = 0,
    DIFFINTENSITY_BRIGHT,
    DIFFINTENSITY_DIFFONLY
};

class Vantage
{
public:
    Vantage(HINSTANCE hInstance);
    ~Vantage();

    bool init(const char * initialFilename1, const char * initialFilename2);
    void loop();
    void cleanup();

    // Event handlers
    void onToggleFullscreen();
    void onWindowPosChanged(int x, int y, int w, int h);
    void onFileOpen();
    void onDiffCurrentImage();
    void loadImage(const char * filename);
    void loadDiff(const char * filename1, const char * filename2 = "");
    void loadImage(int offset);
    void unloadImage(bool unloadColoristImage = true);
    void kickOverlay();
    void killOverlay();
    void resetImagePos();

    void mouseLeftDown(int x, int y);
    void mouseLeftUp(int x, int y);
    void mouseLeftDoubleClick(int x, int y);
    void mouseMove(int x, int y);
    void mouseWheel(int x, int y, int delta);
    void mouseSetPos(int x, int y);
    void adjustThreshold(int amount);
    void setDiffMode(DiffMode diffMode);
    void setDiffIntensity(DiffIntensity diffIntensity);
    void toggleSrgbHighlight();

protected:
    bool createWindow();
    HRESULT createDevice();
    void destroyDevice();
    void updateWindowPos();
    void render();
    void resizeSwapChain(bool initializing = false);
    void checkHDR(); // updates hdrActive_
    void prepareImage();
    void beginText();
    void drawText(const char * text, float x, float y, float lum, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    void endText();

    void setMenuVisible(bool visible);

    void clearOverlay();
    void appendOverlay(const char * format, ...);

    void updateInfo();
    void appendInfo(const char * format, ...);

    void calcImageSize();
    void calcCenteredImagePos(float & posX, float & posY);
    void clampImagePos();

    unsigned int sdrWhiteLevel();

    static const unsigned int overlayDuration_ = 20000;
    static const unsigned int overlayFade_ = 1000;

protected:
    HINSTANCE hInstance_;
    HWND hwnd_;
    WindowPosition windowPos_;
    HMENU menu_;

    bool fullscreen_;
    bool hdrActive_;
    bool imageHDR_;
    bool srgbHighlight_;
    unsigned int imageWhiteLevel_;
    int diffThreshold_;
    DiffMode diffMode_;
    DiffIntensity diffIntensity_;
    clContext * coloristContext_;
    clImage * coloristImage_;
    clImage * coloristImage2_;
    clImageDiff * coloristImageDiff_;
    clImage * coloristImageHighlight_;
    clImageSRGBHighlightPixelInfo * coloristHighlightInfo_;
    clImageSRGBHighlightStats coloristHighlightStats_;

    // D3D
    D3D_DRIVER_TYPE driverType_;
    D3D_FEATURE_LEVEL featureLevel_;
    ID3D11Device * device_;
    ID3D11Device1 * device1_;
    ID3D11DeviceContext * context_;
    ID3D11DeviceContext1 * context1_;
    IDXGIAdapter1 * defaultAdapter_;
    IDXGISwapChain * swapChain_;
    IDXGISwapChain1 * swapChain1_;
    ID3D11RenderTargetView * renderTarget_;
    ID3D11VertexShader * vertexShader_;
    ID3D11PixelShader * pixelShader_;
    ID3D11InputLayout * vertexLayout_;
    ID3D11Buffer * constantBuffer_;
    ID3D11Buffer * vertexBuffer_;
    ID3D11Buffer * indexBuffer_;
    ID3D11SamplerState * sampler_;
    ID3D11ShaderResourceView * image_;
    ID3D11ShaderResourceView * black_;

    // Fonts
    SpriteFont * fontSmall_;
    SpriteBatch * spriteBatch_;

    // Overlay
    std::vector<std::string> overlay_;
    std::vector<std::string> info_;
    int64_t overlayTick_;
    static const unsigned int MAX_OVERLAY_LINES = 4;

    // Image list
    std::vector<std::string> imageFiles_;
    int imageFileIndex_;

    // Mouse tracking
    bool dragging_;
    int dragStartX_;
    int dragStartY_;

    // Image positioning
    float imagePosX_; // x
    float imagePosY_; // y
    float imagePosW_; // width
    float imagePosH_; // height
    float imagePosS_; // scale

    // Image info
    int imageInfoX_;
    int imageInfoY_;
    clImagePixelInfo pixelInfo_;
    clImagePixelInfo pixelInfo2_;
};

#endif // ifndef VANTAGE_H
