#include "MackieApplication.h"
#include "DiagnosticLog.h"
#include <string_view>
#include <clocale>
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    std::setlocale(LC_ALL, ".UTF8");
    const bool smoke = std::wstring_view(arguments).find(L"--smoke-test") != std::wstring_view::npos;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, smoke ? L"Local\\WindowsFaderBridge.Mackie.Smoke" : L"Local\\WindowsFaderBridge.Mackie.Host");
    if (!mutex) return 2;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (const auto window = FindWindowW(L"WindowsFaderBridge.Mackie.Main", nullptr))
        { ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); }
        CloseHandle(mutex); return 0;
    }
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    DiagnosticLog::Instance().Start();
    FB_TRACE("MACKIE_HOST_START smoke=%d protocol=MCU model=14 no_auto_connect=1", smoke);
    int result = 2;
    try { MackieApplication application(smoke); result = application.Run(instance, show); }
    catch (const std::exception& error) { FB_TRACE("MACKIE_FATAL %s", error.what()); }
    DiagnosticLog::Instance().Stop();
    if (SUCCEEDED(com)) CoUninitialize();
    CloseHandle(mutex);
    return result;
}
