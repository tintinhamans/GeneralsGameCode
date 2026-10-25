#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>

HWND ApplicationHWnd = nullptr;
const char *g_strFile = "unused-test.str";
const char *g_csfFile = "unused-test.csf";

// StackDump references this address; the console harness must never enter game startup.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    std::abort();
}
