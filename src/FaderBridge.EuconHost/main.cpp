#include <Windows.h>

#include "EuconHost.h"

#include <memory>
#include <string>

namespace
{
constexpr wchar_t kWindowClass[] = L"FaderBridgeEuconProbeWindow";
std::unique_ptr<EuconHost> g_host;
int g_activeCount = -1;

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        g_host = std::make_unique<EuconHost>(window);
        if (g_host->IsReady())
        {
            SetWindowTextW(window, L"FaderBridge EUCON — waiting for Windows audio…");
        }
        else
        {
            const auto title = L"FaderBridge EUCON initialization failed: " +
                std::to_wstring(g_host->InitializationError());
            SetWindowTextW(window, title.c_str());
        }
        return 0;
    case kSurfaceChangeMessage:
    {
        std::unique_ptr<SurfaceChange> change(reinterpret_cast<SurfaceChange*>(lParam));
        if (!change || !g_host)
        {
            return 0;
        }
        g_host->HandleSurfaceChange(*change);
        return 0;
    }
    case kAudioFrameMessage:
    {
        std::unique_ptr<AudioFrame> frame(reinterpret_cast<AudioFrame*>(lParam));
        if (!frame || !g_host)
        {
            return 0;
        }
        const auto activeCount = g_host->ApplyAudioFrame(*frame);
        if (activeCount != g_activeCount)
        {
            g_activeCount = activeCount;
            const auto title = L"FaderBridge EUCON — " + std::to_wstring(activeCount) +
                L" Windows audio apps";
            SetWindowTextW(window, title.c_str());
        }
        return 0;
    }
    case WM_DESTROY:
        g_host.reset();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&windowClass))
    {
        return 1;
    }

    const auto window = CreateWindowExW(0, kWindowClass, L"FaderBridge EUCON starting…",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 180,
        nullptr, nullptr, instance, nullptr);
    if (!window)
    {
        return 2;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
