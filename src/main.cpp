// This began life as Tutorial07 from MSDN.
// MS has copyrights on whatever subset of this code still somewhat looks like theirs.

#define _CRT_SECURE_NO_WARNINGS

#include "vantage.h"

#include <locale>
#include <codecvt>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    std::wstring cmdLine = lpCmdLine;
    std::wstring_convert<std::codecvt_utf8<wchar_t> > converter;
    std::string output = converter.to_bytes(cmdLine);

    Vantage vantage(hInstance);
    if (!vantage.init(output.c_str())) {
        return -1;
    }

    vantage.loop();
    vantage.cleanup();
    return 0;
}
