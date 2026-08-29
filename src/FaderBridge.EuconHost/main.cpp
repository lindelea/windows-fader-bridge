#include "ApplicationShell.h"
#include "DiagnosticLog.h"

#include <Windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <winrt/base.h>

#include <cwchar>
#include <string_view>

namespace
{
constexpr wchar_t kWindowClass[] = L"WindowsFaderBridge.MainWindow";
constexpr wchar_t kSingleInstanceName[] =
    L"Local\\Lindelea.WindowsFaderBridge.2026";

bool HasArgument(const wchar_t* expected)
{
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return false;
    bool found = false;
    for (int index = 1; index < count; ++index)
    {
        if (std::wstring_view(arguments[index]) == expected)
        {
            found = true;
            break;
        }
    }
    LocalFree(arguments);
    return found;
}

DWORD RestartSourceProcessId()
{
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 0;
    DWORD processId = 0;
    for (int index = 1; index + 1 < count; ++index)
    {
        if (std::wstring_view(arguments[index]) == L"--restart-from")
        {
            wchar_t* end = nullptr;
            const auto parsed = std::wcstoul(arguments[index + 1], &end, 10);
            if (end && *end == L'\0') processId = static_cast<DWORD>(parsed);
            break;
        }
    }
    LocalFree(arguments);
    return processId;
}

void WaitForRestartSource()
{
    const auto processId = RestartSourceProcessId();
    if (processId == 0 || processId == GetCurrentProcessId()) return;
    const auto process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (!process) return;
    WaitForSingleObject(process, 15000U);
    CloseHandle(process);
}

void ActivateExistingInstance()
{
    if (const auto window = FindWindowW(kWindowClass, nullptr))
    {
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
    }
}
}

int WINAPI wWinMain(const HINSTANCE instance, HINSTANCE, PWSTR, const int showCommand)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetCurrentProcessExplicitAppUserModelID(L"Lindelea.WindowsFaderBridge");
    WaitForRestartSource();

    const auto instanceMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceName);
    if (!instanceMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        ActivateExistingInstance();
        CloseHandle(instanceMutex);
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    DiagnosticLog::Instance().Start();
    const auto result = ApplicationShell::Run(instance, showCommand,
        HasArgument(L"--background"));
    DiagnosticLog::Instance().Stop();
    winrt::uninit_apartment();
    ReleaseMutex(instanceMutex);
    CloseHandle(instanceMutex);
    return result;
}
