#include <Windows.h>

#include "EuconHost.h"
#include "DiagnosticLog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr wchar_t kWindowClass[] = L"FaderBridgeEuconWindow";
std::unique_ptr<EuconHost> g_host;
std::array<AudioStripState, EuconHost::MaxChannelCount> g_strips{};
int g_activeCount = -1;
int g_lastChannel = -1;
int g_lastKind = 0;
float g_lastValue = 0.0F;
NEuCon::uint16 g_lastRawIndex = 0U;
float g_lastRawTableValue = 0.0F;
bool g_lastCommandSent = false;
bool g_monoAudioEnabled = false;

const wchar_t* ChangeKindName(const int kind)
{
    switch (kind)
    {
    case 0: return L"Fader";
    case 1: return L"Knob";
    case 2: return L"Mute";
    case 3: return L"Default device";
    case 4: return L"Solo";
    case 5: return L"Select";
    case 6: return L"Attention";
    default: return L"Unknown";
    }
}

void PaintWindow(const HWND window)
{
    PAINTSTRUCT paint{};
    const auto dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);

    std::wostringstream text;
    text << L"Windows audio channels (linear 0–100% mapping)\r\n\r\n";
    text << L"Mono audio: " << (g_monoAudioEnabled ? L"ON" : L"off") << L"\r\n\r\n";
    text << std::fixed << std::setprecision(0);
    std::vector<const AudioStripState*> visibleStrips;
    for (const auto& strip : g_strips)
    {
        if (strip.active)
        {
            visibleStrips.push_back(&strip);
        }
    }
    std::stable_sort(visibleStrips.begin(), visibleStrips.end(), [](const auto* left,
        const auto* right)
    {
        return left->sortGroup != right->sortGroup
            ? left->sortGroup < right->sortGroup
            : left->name < right->name;
    });
    for (size_t index = 0; index < visibleStrips.size(); ++index)
    {
        const auto& strip = *visibleStrips[index];
        text << L"CH" << std::setw(2) << (index + 1U) << L"  "
             << std::left << std::setw(30) << strip.name.substr(0, 29) << std::right
             << std::setw(4) << (strip.volume * 100.0F) << L"%  "
             << (strip.muted ? L"MUTE" : L"    ");
        if (strip.defaultSelectable)
        {
            text << (strip.isDefault ? L"  REC/default" : L"  REC/select");
        }
        text << L"\r\n";
    }
    if (visibleStrips.empty())
    {
        text << L"No active Windows audio applications.\r\n";
    }

    text << L"\r\n";
    if (g_lastChannel >= 0)
    {
        text << L"Last EUCON surface: CH" << (g_lastChannel + 1) << L" "
             << ChangeKindName(g_lastKind) << L"    raw index " << g_lastRawIndex
             << L"    table value ";
        if (g_lastKind >= 2 && g_lastKind <= 6)
        {
            text << (g_lastRawTableValue == 0.0F ? L"Off" : L"On");
        }
        else
        {
            text << static_cast<int>(std::lround(g_lastRawTableValue * 100.0F));
        }
        text << L"\r\nMapped Windows command: "
             << static_cast<int>(std::lround(g_lastValue * 100.0F)) << L"%"
             << L"    write: " << (g_lastCommandSent ? L"OK" : L"not connected");
    }

    auto value = text.str();
    InflateRect(&client, -16, -14);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &client,
        DT_LEFT | DT_TOP | DT_NOPREFIX);
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
            SetWindowTextW(window, L"FaderBridge EUCON — connecting Windows audio…");
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
        if (!g_host || !change)
        {
            return 0;
        }
        g_lastChannel = change->channel;
        g_lastKind = change->kind;
        g_lastValue = change->value;
        g_lastRawIndex = change->rawIndex;
        g_lastRawTableValue = change->rawTableValue;
        if (g_lastKind == 0)
        {
            const auto percent = static_cast<int>(std::lround(g_lastValue * 100.0F));
            const auto raw = static_cast<int>(std::lround(g_lastRawTableValue * 100.0F));
            const auto title = L"FaderBridge EUCON — CH " +
                std::to_wstring(g_lastChannel + 1) + L" HW " +
                std::to_wstring(raw) + L" → Windows " +
                std::to_wstring(percent) + L"%";
            SetWindowTextW(window, title.c_str());
        }
        // Update the title before the command write so raw hardware feedback is
        // never delayed by Windows audio-session work.
        g_lastCommandSent = g_host->HandleSurfaceChange(*change);
        if (g_lastKind >= 2 && g_lastKind <= 6)
        {
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    case kAudioFrameMessage:
    {
        std::unique_ptr<AudioFrame> frame(reinterpret_cast<AudioFrame*>(lParam));
        if (!frame || !g_host)
        {
            return 0;
        }

        bool visibleStateChanged = false;
        visibleStateChanged = g_monoAudioEnabled != frame->monoAudioEnabled;
        g_monoAudioEnabled = frame->monoAudioEnabled;
        for (const auto& strip : frame->strips)
        {
            if (strip.slot < 0 || strip.slot >= EuconHost::MaxChannelCount)
            {
                continue;
            }
            const auto& previous = g_strips[strip.slot];
            visibleStateChanged = visibleStateChanged || previous.active != strip.active ||
                previous.muted != strip.muted || previous.isDefault != strip.isDefault ||
                previous.name != strip.name ||
                std::fabs(previous.volume - strip.volume) > 0.0005F;
            g_strips[strip.slot] = strip;
        }

        const auto activeCount = g_host->ApplyAudioFrame(*frame);
        if (activeCount != g_activeCount)
        {
            g_activeCount = activeCount;
            const auto title = L"FaderBridge EUCON — " + std::to_wstring(activeCount) +
                L" Windows audio channels";
            SetWindowTextW(window, title.c_str());
            visibleStateChanged = true;
        }
        if (visibleStateChanged)
        {
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == kMotorFlushTimerId && g_host)
        {
            g_host->FlushPendingMotors();
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);

    case WM_PAINT:
        PaintWindow(window);
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
    DiagnosticLog::Instance().Start();
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
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 620,
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
    DiagnosticLog::Instance().Stop();
    return static_cast<int>(message.wParam);
}
