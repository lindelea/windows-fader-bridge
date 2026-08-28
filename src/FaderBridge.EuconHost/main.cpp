#include <Windows.h>

#include "EuconHost.h"

#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

namespace
{
constexpr wchar_t kWindowClass[] = L"FaderBridgeEuconProbeWindow";
std::unique_ptr<EuconHost> g_host;
int g_activeCount = -1;

struct DebugState
{
    bool hasSurfaceInput = false;
    int channel = 0;
    int kind = 0;
    NEuCon::uint16 rawIndex = 0U;
    float rawTableValue = 0.0F;
    float commandVolume = 0.0F;
    bool commandSent = false;
    bool observedActive = false;
    float observedVolume = 0.0F;
    float observedPeakDb = -120.0F;
    std::wstring observedName;
};

DebugState g_debug;

const wchar_t* ChangeKindName(const int kind)
{
    switch (kind)
    {
    case 0: return L"Fader";
    case 1: return L"Knob";
    case 2: return L"Mute";
    default: return L"Unknown";
    }
}

void PaintDebugWindow(const HWND window)
{
    PAINTSTRUCT paint{};
    const auto dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);

    std::wostringstream text;
    text << std::fixed << std::setprecision(2);
    if (!g_debug.hasSurfaceInput)
    {
        text << L"Move an S3 fader to capture raw EUCON data.\r\n\r\n";
    }
    else
    {
        text << L"Last S3 input: CH" << (g_debug.channel + 1) << L" "
             << ChangeKindName(g_debug.kind) << L"\r\n";
        text << L"Raw index: " << g_debug.rawIndex << L" / 1023"
             << L"    SDK table value: " << g_debug.rawTableValue << L" dB\r\n";
        text << L"Mapped Windows command: " << (g_debug.commandVolume * 100.0F) << L"%"
             << L"    Pipe write: " << (g_debug.commandSent ? L"OK" : L"FAILED") << L"\r\n";
    }

    if (g_debug.observedActive)
    {
        text << L"Windows feedback CH" << (g_debug.channel + 1) << L": "
             << g_debug.observedName << L"    " << (g_debug.observedVolume * 100.0F)
             << L"%    peak " << g_debug.observedPeakDb << L" dB\r\n";
        if (g_debug.hasSurfaceInput && g_debug.kind != 2)
        {
            text << L"Command/feedback delta: "
                 << ((g_debug.commandVolume - g_debug.observedVolume) * 100.0F)
                 << L" percentage points\r\n";
        }
    }
    else
    {
        text << L"Windows feedback CH" << (g_debug.channel + 1) << L": inactive / empty\r\n";
    }

    text << L"\r\nAvid ExFaderTable: bottom index 0 (Mute/-144.5 dB) | unity index 728 (0 dB) | "
            L"hardware top index 1023 (+12 dB)\r\n"
            L"Windows mapping: scalar <-> 20*log10(value); maximum is S3 unity 0 dB";

    auto value = text.str();
    InflateRect(&client, -16, -14);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &client,
        DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
    EndPaint(window, &paint);
}

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
        g_debug.hasSurfaceInput = true;
        g_debug.channel = change->channel;
        g_debug.kind = change->kind;
        g_debug.rawIndex = change->rawIndex;
        g_debug.rawTableValue = change->rawTableValue;
        g_debug.commandVolume = change->value;
        g_debug.commandSent = g_host->HandleSurfaceChange(*change);
        InvalidateRect(window, nullptr, FALSE);
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
        for (const auto& strip : frame->strips)
        {
            if (strip.slot != g_debug.channel)
            {
                continue;
            }
            const auto changed = g_debug.observedActive != strip.active ||
                std::fabs(g_debug.observedVolume - strip.volume) > 0.0005F ||
                g_debug.observedName != strip.name;
            g_debug.observedActive = strip.active;
            g_debug.observedVolume = strip.volume;
            g_debug.observedPeakDb = strip.peakDb;
            g_debug.observedName = strip.name;
            if (changed)
            {
                InvalidateRect(window, nullptr, FALSE);
            }
            break;
        }
        if (activeCount != g_activeCount)
        {
            g_activeCount = activeCount;
            const auto title = L"FaderBridge EUCON — " + std::to_wstring(activeCount) +
                L" Windows audio apps";
            SetWindowTextW(window, title.c_str());
        }
        return 0;
    }
    case WM_PAINT:
        PaintDebugWindow(window);
        return 0;
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
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 860, 300,
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
