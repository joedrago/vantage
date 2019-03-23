#include "vantage.h"

#define VANTAGE_STYLE_WINDOWED   (WS_OVERLAPPEDWINDOW | WS_VISIBLE)
#define VANTAGE_STYLE_FULLSCREEN (WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE)

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

bool Vantage::createWindow() //(HINSTANCE hInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof( WNDCLASSEX );
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance_;
    wcex.hIcon = LoadIcon(hInstance_, (LPCTSTR)IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)( COLOR_WINDOW + 1 );
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = "vantageWindowClass";
    wcex.hIconSm = LoadIcon(wcex.hInstance, (LPCTSTR)IDI_APPLICATION);
    if (!RegisterClassEx(&wcex))
        return false;

    hwnd_ = CreateWindow("vantageWindowClass", "Vantage", VANTAGE_STYLE_WINDOWED, CW_USEDEFAULT, CW_USEDEFAULT, windowPos_.w, windowPos_.h, nullptr, nullptr, hInstance_, nullptr);
    if (!hwnd_)
        return false;

    SetWindowLongPtr(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    ShowWindow(hwnd_, SW_SHOW);
    updateWindowPos();

    DragAcceptFiles(hwnd_, TRUE);
    return true;
}

void Vantage::updateWindowPos()
{
    if (fullscreen_) {
        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(monitor, &mi);
        SetWindowLongPtr(hwnd_, GWL_STYLE, VANTAGE_STYLE_FULLSCREEN);
        MoveWindow(hwnd_, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, TRUE);
    } else {
        SetWindowLongPtr(hwnd_, GWL_STYLE, VANTAGE_STYLE_WINDOWED);
        MoveWindow(hwnd_, windowPos_.x, windowPos_.y, windowPos_.w, windowPos_.h, TRUE);
    }
}

void Vantage::onToggleFullscreen()
{
    fullscreen_ = !fullscreen_;
    updateWindowPos();
}

void Vantage::onWindowPosChanged(int x, int y, int w, int h)
{
    if (!fullscreen_) {
        windowPos_.x = x;
        windowPos_.y = y;
        windowPos_.w = w;
        windowPos_.h = h;
    }

    resizeSwapChain();
    checkHDR();
}

void Vantage::loop()
{
    MSG msg = { 0 };
    while (WM_QUIT != msg.message) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            render();
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hdc;

    Vantage * v = (Vantage *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (message) {
        case WM_CHAR:
        {
            unsigned int key = (unsigned int)wParam;
            switch (key) {
                case 102: // F
                    if (v)
                        v->onToggleFullscreen();
                    break;
#if 0
                case 32: // Space
                    forceSDR_ = !forceSDR_;
                    updateWindowTitle();
                    break;

                case 110: // N
                    currentTextureIndex_ = (currentTextureIndex_ + 1) % TEXTURE_COUNT;
                    updateWindowTitle();
                    break;

                case 112: // P
                    --currentTextureIndex_;
                    if (currentTextureIndex_ < 0) {
                        currentTextureIndex_ = TEXTURE_COUNT - 1;
                    }
                    updateWindowTitle();
                    break;

                case 116: // T
                    tonemap_ = !tonemap_;
                    updateWindowTitle();
                    break;
#endif
            }
            break;
        }

        case WM_DROPFILES:
        {
            HDROP drop = (HDROP)wParam;
            char filename[MAX_PATH + 1];
            filename[0] = 0;
            DragQueryFile(drop, 0, filename, MAX_PATH);
            DragFinish(drop);
            if (v && filename[0]) {
                v->loadImage(filename);
            }
            break;
        }

        case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS * pos = (WINDOWPOS *)lParam;
            if (v)
                v->onWindowPosChanged(pos->x, pos->y, pos->cx, pos->cy);
            break;
        }

        case WM_PAINT:
            hdc = BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}
