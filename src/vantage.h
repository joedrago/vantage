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

class Vantage
{
public:
    Vantage(HINSTANCE hInstance);
    ~Vantage();

    bool init();
    void loop();
    void cleanup();

    // Event handlers
    void onToggleFullscreen();
    void onWindowPosChanged(int x, int y, int w, int h);
    void onFileOpen();
    void loadImage(const char * filename);
    void loadImage(int offset);
    void unloadImage(bool unloadColoristImage = true);
    void kickOverlay();
    void resetImagePos();

    void mouseLeftDown(int x, int y);
    void mouseLeftUp(int x, int y);
    void mouseLeftDoubleClick(int x, int y);
    void mouseMove(int x, int y);
    void mouseWheel(int x, int y, int delta);
    void mouseSetPos(int x, int y);

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
    void drawText(const char * text, float x, float y, float r, float g, float b, float a);
    void endText();

    void setMenuVisible(bool visible);

    void clearOverlay();
    void appendOverlay(const char * format, ...);

    void calcImageSize();
    void calcCenteredImagePos(float & posX, float & posY);
    void clampImagePos();

    unsigned int sdrWhiteLevel();

protected:
    HINSTANCE hInstance_;
    HWND hwnd_;
    WindowPosition windowPos_;
    HMENU menu_;

    bool fullscreen_;
    bool hdrActive_;
    bool imageHDR_;
    unsigned int imageWhiteLevel_;
    clContext * coloristContext_;
    clImage * coloristImage_;

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

    // Fonts
    SpriteFont * fontSmall_;
    SpriteBatch * spriteBatch_;

    // Overlay
    std::vector<std::string> overlay_;
    DWORD overlayTick_;
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
};

#endif // ifndef VANTAGE_H
