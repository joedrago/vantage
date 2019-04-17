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

    int numArgs = 0;
    LPWSTR * argv = NULL;
    if (lpCmdLine[0]) {
        argv = CommandLineToArgvW(lpCmdLine, &numArgs);
    }

    std::wstring cmdLine = lpCmdLine;
    std::string filename1, filename2;
    std::wstring_convert<std::codecvt_utf8<wchar_t> > converter;

    if (numArgs > 1) {
        // Diff!
        filename1 = converter.to_bytes(argv[0]);
        filename2 = converter.to_bytes(argv[1]);
    } else if (numArgs > 0) {
        // Single filename
        filename1 = converter.to_bytes(argv[0]);
    }

    Vantage vantage(hInstance);
    if (!vantage.init(filename1.c_str(), filename2.c_str())) {
        return -1;
    }

    vantage.loop();
    vantage.cleanup();
    return 0;
}
