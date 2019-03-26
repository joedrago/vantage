#include "vantage.h"
#include "resource.h"

#include <windowsx.h>

#define VANTAGE_STYLE_WINDOWED   (WS_OVERLAPPEDWINDOW | WS_VISIBLE)
#define VANTAGE_STYLE_FULLSCREEN (WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE)

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

bool Vantage::createWindow() //(HINSTANCE hInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof( WNDCLASSEX );
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance_;
    wcex.hIcon = LoadIcon(hInstance_, (LPCTSTR)IDI_VANTAGE);
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

    menu_ = LoadMenu(hInstance_, MAKEINTRESOURCE(IDC_VANTAGE));
    setMenuVisible(true);

    DragAcceptFiles(hwnd_, TRUE);
    return true;
}

void Vantage::setMenuVisible(bool visible)
{
    HMENU menu = NULL;
    if (visible) {
        menu = menu_;
    }
    SetMenu(hwnd_, menu);
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
    setMenuVisible(!fullscreen_);
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

void Vantage::onFileOpen()
{
    char filename[MAX_PATH];

    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = filename;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(filename);
    ofn.lpstrFilter = "All Image Files (*.apg, *.avif, *.bmp, *.jpg, *.jp2, *.j2k, *.png, *.tiff, *.webp)\0*.apg;*.avif;*.bmp;*.jpg;*.jp2;*.j2k;*.png;*.tiff;*.webp\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        loadImage(filename);
    }
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
                case 97: // A
                    if (v)
                        v->loadImage(-1);
                    break;
                case 100: // D
                    if (v)
                        v->loadImage(1);
                    break;
                case 102: // F
                    if (v)
                        v->onToggleFullscreen();
                    break;
                case 111: // O
                    if (v)
                        v->onFileOpen();
                    break;
                case 113: // Q
                    PostQuitMessage(0);
                    break;
                case 114: // R
                    if (v)
                        v->resetImagePos();
                    break;

                case 32: // Space
                    if (v)
                        v->kickOverlay();
                    break;
            }
            break;
        }

        case WM_KEYDOWN:
        {
            switch (wParam) {
                case VK_ESCAPE:
                    PostQuitMessage(0);
                    break;
                case VK_LEFT:
                case VK_UP:
                    if (v)
                        v->loadImage(-1);
                    break;
                case VK_RIGHT:
                case VK_DOWN:
                    if (v)
                        v->loadImage(1);
                    break;
                case VK_F11:
                    if (v)
                        v->onToggleFullscreen();
                    break;
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_EXIT:
                    PostQuitMessage(0);
                    break;
                case ID_VIEW_FULLSCREEN:
                    if (v)
                        v->onToggleFullscreen();
                    break;
                case ID_FILE_OPEN:
                    if (v)
                        v->onFileOpen();
                    break;
                case ID_VIEW_PREVIOUSIMAGE:
                    if (v)
                        v->loadImage(-1);
                    break;
                case ID_VIEW_NEXTIMAGE:
                    if (v)
                        v->loadImage(1);
                    break;
            }
            break;

        case WM_LBUTTONDOWN:
            if (v) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                v->mouseLeftDown(x, y);
            }
            break;

        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            if (v) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                v->mouseSetPos(x, y);
            }
            break;

        case WM_LBUTTONDBLCLK:
            if (v) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                v->mouseLeftDoubleClick(x, y);
            }
            break;

        case WM_LBUTTONUP:
            if (v) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                v->mouseLeftUp(x, y);
            }
            break;

        case WM_MOUSEMOVE:
            if (v) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                v->mouseMove(x, y);
                if (wParam & MK_RBUTTON) {
                    v->mouseSetPos(x, y);
                }
            }
            break;

        case WM_MOUSEWHEEL:
            if (v) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                v->mouseWheel(x, y, delta);
            }
            break;

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
