// This began life as Tutorial07 from MSDN.
// MS has copyrights on whatever subset of this code still somewhat looks like theirs.

#define _CRT_SECURE_NO_WARNINGS

#include "vantage.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    Vantage vantage(hInstance);
    if (!vantage.init()) {
        return -1;
    }

    vantage.loop();
    vantage.cleanup();
    return 0;
}
