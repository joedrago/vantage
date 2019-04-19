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

static const char * imageFileFilter = "All Image Files (*.apg, *.avif, *.bmp, *.jpg, *.jpeg, *.jp2, *.j2k, *.png, *.tif, *.tiff, *.webp)\0*.apg;*.avif;*.bmp;*.jpg;*.jpeg;*.jp2;*.j2k;*.png;*.tif;*.tiff;*.webp\0All Files (*.*)\0*.*\0";

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
    ofn.lpstrFilter = imageFileFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        loadImage(filename);
    }
}

void Vantage::onDiffCurrentImage()
{
    if (imageFiles_.empty()) {
        return;
    }

    char filename[MAX_PATH];

    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = filename;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(filename);
    ofn.lpstrFilter = imageFileFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        loadDiff(filename, "");
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
    char filename1[MAX_PATH + 1];
    char filename2[MAX_PATH + 1];

    Vantage * v = (Vantage *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (message) {
        case WM_CHAR:
        {
            unsigned int key = (unsigned int)wParam;
            switch (key) {
                case 49: // 1
                    if (v)
                        v->setDiffMode(DIFFMODE_SHOW1);
                    break;
                case 50: // 2
                    if (v)
                        v->setDiffMode(DIFFMODE_SHOW2);
                    break;
                case 51: // 3
                    if (v)
                        v->setDiffMode(DIFFMODE_SHOWDIFF);
                    break;
                case 100: // D
                    if (v)
                        v->onDiffCurrentImage();
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
                case 115: // S
                    if (v)
                        v->toggleSrgbHighlight();
                    break;

                case 122: // Z
                    if (v)
                        v->setDiffIntensity(DIFFINTENSITY_ORIGINAL);
                    break;
                case 120: // X
                    if (v)
                        v->setDiffIntensity(DIFFINTENSITY_BRIGHT);
                    break;
                case 99: // C
                    if (v)
                        v->setDiffIntensity(DIFFINTENSITY_DIFFONLY);
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
                    v->killOverlay();
                    break;
                case VK_LEFT:
                    if (v)
                        v->loadImage(-1);
                    break;
                case VK_RIGHT:
                    if (v)
                        v->loadImage(1);
                    break;
                case VK_F11:
                    if (v)
                        v->onToggleFullscreen();
                    break;

                case VK_UP:
                    if (v)
                        v->adjustThreshold(1);
                    break;
                case VK_DOWN:
                    if (v)
                        v->adjustThreshold(-1);
                    break;
                case VK_DELETE:
                    if (v)
                        v->adjustThreshold(-5);
                    break;
                case VK_END:
                    if (v)
                        v->adjustThreshold(-50);
                    break;
                case VK_NEXT:
                    if (v)
                        v->adjustThreshold(-500);
                    break;
                case VK_INSERT:
                    if (v)
                        v->adjustThreshold(5);
                    break;
                case VK_HOME:
                    if (v)
                        v->adjustThreshold(50);
                    break;
                case VK_PRIOR:
                    if (v)
                        v->adjustThreshold(500);
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
                case ID_VIEW_RESETIMAGEPOSITION:
                    if (v)
                        v->resetImagePos();
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
                case ID_DIFF_DIFFCURRENTIMAGEAGAINST:
                    if (v)
                        v->onDiffCurrentImage();
                    break;

                case ID_VIEW_TOGGLESRGBHIGHLIGHT:
                    if (v)
                        v->toggleSrgbHighlight();
                    break;

                case ID_DIFF_SHOWIMAGE1:
                    if (v)
                        v->setDiffMode(DIFFMODE_SHOW1);
                    break;
                case ID_DIFF_SHOWIMAGE2:
                    if (v)
                        v->setDiffMode(DIFFMODE_SHOW2);
                    break;
                case ID_DIFF_SHOWDIFF:
                    if (v)
                        v->setDiffMode(DIFFMODE_SHOWDIFF);
                    break;

                case ID_DIFF_DIFFINTENSITY_ORIGINAL:
                    if (v)
                        v->setDiffIntensity(DIFFINTENSITY_ORIGINAL);
                    break;
                case ID_DIFF_DIFFINTENSITY_BRIGHT:
                    if (v)
                        v->setDiffIntensity(DIFFINTENSITY_BRIGHT);
                    break;
                case ID_DIFF_DIFFINTENSITY_DIFFONLY:
                    if (v)
                        v->setDiffIntensity(DIFFINTENSITY_DIFFONLY);
                    break;

                case ID_DIFF_ADJUSTTHRESHOLDM1:
                    if (v)
                        v->adjustThreshold(-1);
                    break;
                case ID_DIFF_ADJUSTTHRESHOLDM5:
                    if (v)
                        v->adjustThreshold(-5);
                    break;
                case ID_DIFF_ADJUSTTHRESHOLDM50:
                    if (v)
                        v->adjustThreshold(-50);
                    break;
                case ID_DIFF_ADJUSTTHRESHOLDM500:
                    if (v)
                        v->adjustThreshold(-500);
                    break;

                case ID_DIFF_ADJUSTTHRESHOLD1:
                    if (v)
                        v->adjustThreshold(1);
                    break;
                case ID_DIFF_ADJUSTTHRESHOLD5:
                    if (v)
                        v->adjustThreshold(5);
                    break;
                case ID_DIFF_ADJUSTTHRESHOLD50:
                    if (v)
                        v->adjustThreshold(50);
                    break;
                case ID_DIFF_ADJUSTTHRESHOLD500:
                    if (v)
                        v->adjustThreshold(500);
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
                POINT p;
                p.x = GET_X_LPARAM(lParam);
                p.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &p);
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                v->mouseWheel(p.x, p.y, delta);
            }
            break;

        case WM_DROPFILES:
        {
            filename1[0] = 0;
            filename2[0] = 0;

            HDROP drop = (HDROP)wParam;
            int dropCount = DragQueryFile(drop, 0xFFFFFFFF, filename1, MAX_PATH);
            if (dropCount > 1) {
                DragQueryFile(drop, 0, filename1, MAX_PATH);
                DragQueryFile(drop, 1, filename2, MAX_PATH);
                DragFinish(drop);
                if (v && filename1[0] && filename2[0]) {
                    v->loadDiff(filename1, filename2);
                }
            } else {
                DragQueryFile(drop, 0, filename1, MAX_PATH);
                DragFinish(drop);
                if (v && filename1[0]) {
                    v->loadImage(filename1);
                }
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
