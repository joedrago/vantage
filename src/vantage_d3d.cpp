#include "vantage.h"

#include "vertexShader.h"
#include "pixelShader.h"
#include "fontSmall.h"

#include <locale>
#include <codecvt>

HRESULT Vantage::createDevice()
{
    HRESULT hr = S_OK;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    UINT width = 3840;  //rc.right - rc.left;
    UINT height = 2160; //rc.bottom - rc.top;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_DRIVER_TYPE driverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT numDriverTypes = ARRAYSIZE(driverTypes);

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    UINT numFeatureLevels = ARRAYSIZE(featureLevels);

    for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++) {
        driverType_ = driverTypes[driverTypeIndex];
        hr = D3D11CreateDevice(nullptr, driverType_, nullptr, createDeviceFlags, featureLevels, numFeatureLevels, D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);

        if (hr == E_INVALIDARG) {
            // DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
            hr = D3D11CreateDevice(nullptr, driverType_, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1, D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
        }

        if (SUCCEEDED(hr))
            break;
    }
    if (FAILED(hr)) {
        return hr;
    }
    // Obtain DXGI factory from device (since we used nullptr for pAdapter above)
    IDXGIFactory1 * dxgiFactory = nullptr;
    {
        IDXGIDevice * dxgiDevice = nullptr;
        hr = device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgiDevice));
        if (SUCCEEDED(hr)) {
            IDXGIAdapter * adapter = nullptr;
            hr = dxgiDevice->GetAdapter(&adapter);
            if (SUCCEEDED(hr)) {
                hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&dxgiFactory));
                adapter->Release();
            }
            dxgiDevice->Release();
        }
    }
    if (FAILED(hr))
        return hr;

    if (FAILED(dxgiFactory->EnumAdapters1(0, &defaultAdapter_))) {
        return hr;
    }

    // Create swap chain
    IDXGIFactory2 * dxgiFactory2 = nullptr;
    hr = dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(&dxgiFactory2));
    if (dxgiFactory2) {
        // DirectX 11.1 or later
        hr = device_->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&device1_));
        if (SUCCEEDED(hr)) {
            (void)context_->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void **>(&context1_));
        }

        DXGI_SWAP_CHAIN_DESC1 sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.Width = width;
        sd.Height = height;
        sd.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;

        hr = dxgiFactory2->CreateSwapChainForHwnd(device_, hwnd_, &sd, nullptr, nullptr, &swapChain1_);
        if (SUCCEEDED(hr)) {
            hr = swapChain1_->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void **>(&swapChain_));
        }

        dxgiFactory2->Release();
    } else {
        // DirectX 11.0 systems
        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 1;
        sd.BufferDesc.Width = width;
        sd.BufferDesc.Height = height;
        sd.BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd_;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.BufferCount = 2;

        hr = dxgiFactory->CreateSwapChain(device_, &sd, &swapChain_);
    }

    if (FAILED(hr))
        return hr;

    // Note this tutorial doesn't handle full-screen swapchains so we block the ALT+ENTER shortcut
    dxgiFactory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    dxgiFactory->Release();

    resizeSwapChain(true);

    // Create the vertex shader
    hr = device_->CreateVertexShader(g_VS, sizeof(g_VS), nullptr, &vertexShader_);
    if (FAILED(hr)) {
        return hr;
    }

    // Define the input layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE(layout);

    // Create the input layout
    hr = device_->CreateInputLayout(layout, numElements, g_VS, sizeof(g_VS), &vertexLayout_);
    if (FAILED(hr)) {
        return hr;
    }

    // Create constant buffer
    {
        D3D11_BUFFER_DESC bd;
        ZeroMemory(&bd, sizeof(bd));
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(ConstantBuffer);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = 0;
        hr = device_->CreateBuffer(&bd, nullptr, &constantBuffer_);
        if (FAILED(hr))
            return hr;
    }

    // Create the pixel shader
    hr = device_->CreatePixelShader(g_PS, sizeof(g_PS), nullptr, &pixelShader_);
    if (FAILED(hr)) {
        return hr;
    }

    // Create vertex buffer
    SimpleVertex vertices[] =
    {
        { XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
    };

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( SimpleVertex ) * 4;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    ZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = vertices;
    hr = device_->CreateBuffer(&bd, &InitData, &vertexBuffer_);
    if (FAILED(hr)) {
        return hr;
    }

    // Create index buffer
    // Create vertex buffer
    WORD indices[] =
    {
        0, 1, 2,
        0, 2, 3,
    };

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( WORD ) * 6;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    InitData.pSysMem = indices;
    hr = device_->CreateBuffer(&bd, &InitData, &indexBuffer_);
    if (FAILED(hr)) {
        return hr;
    }

    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory(&sampDesc, sizeof(sampDesc));
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device_->CreateSamplerState(&sampDesc, &sampler_);
    if (FAILED(hr)) {
        return hr;
    }

    fontSmall_ = new SpriteFont(device_, fontSmallBinaryData, fontSmallBinarySize);
    spriteBatch_ = new SpriteBatch(context_);
    return S_OK;
}

void Vantage::destroyDevice()
{
    if (spriteBatch_) {
        delete spriteBatch_;
        spriteBatch_ = nullptr;
    }
    if (fontSmall_) {
        delete fontSmall_;
        fontSmall_ = nullptr;
    }

    if (context_) {
        context_->ClearState();
        context_ = nullptr;
    }

    if (image_) {
        image_->Release();
        image_ = nullptr;
    }

    if (sampler_) {
        sampler_->Release();
        sampler_ = nullptr;
    }

    if (vertexBuffer_) {
        vertexBuffer_->Release();
        vertexBuffer_ = nullptr;
    }
    if (indexBuffer_) {
        indexBuffer_->Release();
        indexBuffer_ = nullptr;
    }
    if (vertexLayout_) {
        vertexLayout_->Release();
        vertexLayout_ = nullptr;
    }
    if (vertexShader_) {
        vertexShader_->Release();
        vertexShader_ = nullptr;
    }
    if (pixelShader_) {
        pixelShader_->Release();
        pixelShader_ = nullptr;
    }
    if (renderTarget_) {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }
    if (swapChain1_) {
        swapChain1_->Release();
        swapChain1_ = nullptr;
    }
    if (swapChain_) {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    if (defaultAdapter_) {
        defaultAdapter_->Release();
        defaultAdapter_ = nullptr;
    }
    if (context1_) {
        context1_->Release();
        context1_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device1_) {
        device1_->Release();
        device1_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

void Vantage::resizeSwapChain(bool initializing)
{
    if (!swapChain_) {
        return;
    }

    if (renderTarget_) {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }
    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);
    unsigned int width = max(clientRect.right, 1);
    unsigned int height = max(clientRect.bottom, 1);

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    swapChain_->GetDesc(&swapChainDesc);
    bool resizeSwapChain = (swapChainDesc.BufferDesc.Width != width) || (swapChainDesc.BufferDesc.Height != height);
    if (resizeSwapChain) {
        // char debugString[128];
        // sprintf(debugString, "ResizeBuffers: %dx%d\n", width, height);
        // OutputDebugString(debugString);

        swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    }

    // Create a render target view
    ID3D11Texture2D * pBackBuffer = nullptr;
    HRESULT hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>( &pBackBuffer ));
    if (FAILED(hr)) {
        return;
    }

    device_->CreateRenderTargetView(pBackBuffer, nullptr, &renderTarget_);
    pBackBuffer->Release();

    // Setup the viewport
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    context_->RSSetViewports(1, &vp);

    if (!initializing && resizeSwapChain) {
        resetImagePos();
        render();
    }
}

void Vantage::checkHDR()
{
    bool hdrWasActive = hdrActive_;

    hdrActive_ = false;
    if (defaultAdapter_) {
        // Find out which monitor we're mostly overlapping
        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(monitor, &mi);

        // Now find that monitor's rect in our outputs list and check against its color space
        IDXGIOutput * output;
        for (UINT outputIndex = 0; defaultAdapter_->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND; ++outputIndex) {
            IDXGIOutput6 * output6;
            output->QueryInterface(&output6);
            DXGI_OUTPUT_DESC1 desc;
            output6->GetDesc1(&desc);
            output6->Release();
            output->Release();

            if (!memcmp(&mi.rcMonitor, &desc.DesktopCoordinates, sizeof(mi.rcMonitor))) {
                if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                    hdrActive_ = true;
                }
            }
        }

        IDXGISwapChain3 * swapChain3 = nullptr;
        HRESULT hr = swapChain_->QueryInterface(IID_PPV_ARGS(&swapChain3));
        if (SUCCEEDED(hr)) {
            DXGI_COLOR_SPACE_TYPE colorSpaceType = (hdrActive_) ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            swapChain3->SetColorSpace1(colorSpaceType);
            swapChain3->Release();
        }
    }

    if (hdrActive_ != hdrWasActive) {
        if (hdrActive_) {
            appendOverlay("HDR: Active");
        } else {
            appendOverlay("HDR: Inactive");
        }
    }
}

void Vantage::beginText()
{
    spriteBatch_->Begin();
}

void Vantage::drawText(const char * text, float x, float y, float r, float g, float b, float a)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t> > converter;
    std::wstring output = converter.from_bytes(text);

    const float shadowOffset = 2.0f;

    XMVECTORF32 shadowColor = { { { 0, 0, 0, a } } };
    XMFLOAT2 shadowPos;
    shadowPos.x = x + shadowOffset;
    shadowPos.y = y + shadowOffset;

    XMVECTORF32 fontColor = { { { r, g, b, a } } };
    XMFLOAT2 fontPos;
    fontPos.x = x;
    fontPos.y = y;

    XMFLOAT2 origin;
    origin.x = 0.0f;
    origin.y = 0.0f;

    fontSmall_->DrawString(spriteBatch_, output.c_str(), shadowPos, shadowColor, 0.f, origin);
    fontSmall_->DrawString(spriteBatch_, output.c_str(), fontPos, fontColor, 0.f, origin);
}

void Vantage::endText()
{
    spriteBatch_->End();
}
