#include "MackieApplication.h"
#include "DiagnosticLog.h"
#include "../BridgeGlobalShortcut.h"
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
        bridge::RequestSummon(bridge::Application::MackieControl);
        CloseHandle(mutex); return 0;
    }
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    DiagnosticLog::Instance().Start();
    FB_TRACE("MACKIE_HOST_START smoke=%d protocol=MCU model=14 saved_ports_only=1", smoke);
    int result = 2;
    try { MackieApplication application(smoke,std::wstring_view(arguments).find(L"--diagnostics")!=std::wstring_view::npos); result = application.Run(instance,
        std::wstring_view(arguments).find(L"--background") != std::wstring_view::npos ? SW_HIDE : show); }
    catch (const std::exception& error) { FB_TRACE("MACKIE_FATAL %s", error.what()); }
    DiagnosticLog::Instance().Stop();
    if (SUCCEEDED(com)) CoUninitialize();
    CloseHandle(mutex);
    return result;
}
