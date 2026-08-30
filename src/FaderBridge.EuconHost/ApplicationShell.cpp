#include "ApplicationShell.h"

#include "DiagnosticLog.h"
#include "resource.h"
#include "WindowsCommandExecutor.h"

#include <CommCtrl.h>
#include <Dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
constexpr wchar_t kWindowClass[] = L"WindowsFaderBridge.MainWindow";
constexpr wchar_t kProductName[] = L"Windows Fader Bridge for EUCON";
constexpr wchar_t kProductVersion[] = L"0.2.0";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"WindowsFaderBridge";
constexpr UINT kTrayCallbackMessage = WM_APP + 100U;
constexpr UINT kTrayIconId = 1U;
constexpr int kStartupCheckId = 1001;
constexpr int kHideButtonId = 1002;
constexpr int kPreviousPageButtonId = 1003;
constexpr int kNextPageButtonId = 1004;
constexpr int kTrayOpenId = 1101;
constexpr int kTrayStatusId = 1102;
constexpr int kTrayStartupId = 1103;
constexpr int kTrayRestartId = 1104;
constexpr int kTrayDiagnosticsId = 1105;
constexpr int kTrayAboutId = 1106;
constexpr int kTrayExitId = 1107;

constexpr COLORREF kBackground = RGB(12, 15, 21);
constexpr COLORREF kCard = RGB(23, 28, 37);
constexpr COLORREF kCardBorder = RGB(42, 50, 64);
constexpr COLORREF kText = RGB(239, 244, 248);
constexpr COLORREF kMutedText = RGB(151, 162, 179);
constexpr COLORREF kCyan = RGB(39, 216, 226);
constexpr COLORREF kGreen = RGB(65, 211, 140);
constexpr COLORREF kRed = RGB(247, 101, 112);

int Scale(const HWND window, const int value)
{
    return MulDiv(value, static_cast<int>(GetDpiForWindow(window)), 96);
}

std::wstring ExecutablePath()
{
    std::wstring path(32768U, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(),
        static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

std::wstring StartupCommand()
{
    return L"\"" + ExecutablePath() + L"\" --background";
}

bool IsStartupEnabled()
{
    wchar_t value[32768]{};
    DWORD size = sizeof(value);
    const auto result = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue,
        RRF_RT_REG_SZ, nullptr, value, &size);
    return result == ERROR_SUCCESS && StartupCommand() == value;
}

bool SetStartupEnabled(const bool enabled)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        return false;
    }
    const auto closeKey = [key] { RegCloseKey(key); };
    if (!enabled)
    {
        const auto result = RegDeleteValueW(key, kRunValue);
        closeKey();
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    const auto command = StartupCommand();
    const auto result = RegSetValueExW(key, kRunValue, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1U) * sizeof(wchar_t)));
    closeKey();
    return result == ERROR_SUCCESS;
}

void FillRoundedRect(const HDC dc, const RECT& rect, const COLORREF fill,
    const COLORREF border, const int radius)
{
    const auto fillBrush = CreateSolidBrush(fill);
    const auto borderPen = CreatePen(PS_SOLID, 1, border);
    const auto previousBrush = SelectObject(dc, fillBrush);
    const auto previousPen = SelectObject(dc, borderPen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);
}

void DrawTextLine(const HDC dc, const std::wstring& value, RECT rect,
    const HFONT font, const COLORREF color, const UINT format = DT_LEFT)
{
    const auto previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect,
        format | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
}

const wchar_t* ChangeKindName(const int kind)
{
    switch (kind)
    {
    case 0: return L"Fader";
    case 1: return L"Volume knob";
    case 2: return L"Mute";
    case 3: return L"Default device";
    case 4: return L"Solo";
    case 5: return L"Select";
    case 6: return L"Attention";
    case 7: return L"Pan";
    case 8: return L"Pan center";
    default: return L"Control";
    }
}
}

ApplicationShell::ApplicationShell(const HINSTANCE instance) : instance_(instance)
{
}

ApplicationShell::~ApplicationShell()
{
    RemoveTrayIcon();
    host_.reset();
    DestroyFonts();
    if (largeIcon_) DestroyIcon(largeIcon_);
    if (smallIcon_) DestroyIcon(smallIcon_);
}

int ApplicationShell::Run(const HINSTANCE instance, const int showCommand,
    const bool startInBackground)
{
    ApplicationShell shell(instance);
    if (!shell.CreateMainWindow(showCommand, startInBackground)) return 2;

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool ApplicationShell::CreateMainWindow(const int showCommand,
    const bool startInBackground)
{
    largeIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR));
    smallIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON, 20, 20, LR_DEFAULTCOLOR));

    WNDCLASSEXW windowClass{ sizeof(windowClass) };
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = largeIcon_;
    windowClass.hIconSm = smallIcon_;
    windowClass.hbrBackground = nullptr;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return false;
    }

    window_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, kProductName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        900, 680, nullptr, nullptr, instance_, this);
    if (!window_) return false;

    if (!startInBackground)
    {
        ShowWindow(window_, showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
        UpdateWindow(window_);
    }
    return true;
}

LRESULT CALLBACK ApplicationShell::WindowProcedure(const HWND window,
    const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    auto* shell = reinterpret_cast<ApplicationShell*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lParam);
        shell = static_cast<ApplicationShell*>(creation->lpCreateParams);
        shell->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(shell));
    }
    return shell ? shell->HandleMessage(message, wParam, lParam) :
        DefWindowProcW(window, message, wParam, lParam);
}

bool ApplicationShell::OnCreate()
{
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    CreateFonts();

    startupCheck_ = CreateWindowExW(0, L"BUTTON",
        L"Start with Windows", WS_CHILD | WS_VISIBLE |
        WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartupCheckId)), instance_, nullptr);
    hideButton_ = CreateWindowExW(0, L"BUTTON", L"Hide to tray",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHideButtonId)), instance_, nullptr);
    previousPageButton_ = CreateWindowExW(0, L"BUTTON", L"\u2039",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviousPageButtonId)), instance_, nullptr);
    nextPageButton_ = CreateWindowExW(0, L"BUTTON", L"\u203a",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNextPageButtonId)), instance_, nullptr);
    for (const auto control : { startupCheck_, hideButton_, previousPageButton_,
        nextPageButton_ })
    {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
    // BS_OWNERDRAW does not retain the checkbox state reported by BM_SETCHECK.
    // Keep the rendered state synchronized with the actual Run registry value.
    startupEnabled_ = IsStartupEnabled();

    host_ = std::make_unique<EuconHost>(window_);
    AddTrayIcon();
    LayoutControls();
    return true;
}

LRESULT ApplicationShell::HandleMessage(const UINT message, const WPARAM wParam,
    const LPARAM lParam)
{
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0U)
    {
        trayAdded_ = false;
        AddTrayIcon();
        return 0;
    }
    switch (message)
    {
    case WM_CREATE:
        return OnCreate() ? 0 : -1;
    case kSurfaceChangeMessage:
    {
        std::unique_ptr<SurfaceChange> change(reinterpret_cast<SurfaceChange*>(lParam));
        if (change) OnSurfaceChange(*change);
        return 0;
    }
    case kAudioFrameMessage:
    {
        std::unique_ptr<AudioFrame> frame(reinterpret_cast<AudioFrame*>(lParam));
        if (frame) OnAudioFrame(*frame);
        return 0;
    }
    case kWindowsCommandMessage:
        WindowsCommandExecutor::Execute(static_cast<WindowsCommand>(wParam));
        return 0;
    case WM_TIMER:
        if (wParam == kMotorFlushTimerId && host_)
        {
            host_->FlushPendingMotors();
            return 0;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case kStartupCheckId:
        {
            const auto enabled = !startupEnabled_;
            if (!SetStartupEnabled(enabled))
            {
                MessageBoxW(window_, L"Windows could not update the startup setting.",
                    kProductName, MB_OK | MB_ICONERROR);
            }
            startupEnabled_ = IsStartupEnabled();
            InvalidateRect(startupCheck_, nullptr, TRUE);
            return 0;
        }
        case kHideButtonId: HideMainWindow(); return 0;
        case kPreviousPageButtonId:
            if (channelPage_ > 0) --channelPage_;
            UpdateChannelPagination();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case kNextPageButtonId:
            ++channelPage_;
            UpdateChannelPagination();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case kTrayOpenId: ShowMainWindow(); return 0;
        case kTrayStartupId:
            if (!SetStartupEnabled(!startupEnabled_))
            {
                MessageBoxW(window_, L"Windows could not update the startup setting.",
                    kProductName, MB_OK | MB_ICONERROR);
            }
            startupEnabled_ = IsStartupEnabled();
            InvalidateRect(startupCheck_, nullptr, TRUE);
            return 0;
        case kTrayRestartId: RestartApplication(); return 0;
        case kTrayDiagnosticsId: OpenDiagnosticsFolder(); return 0;
        case kTrayAboutId: ShowAboutDialog(); return 0;
        case kTrayExitId: ExitApplication(); return 0;
        default: break;
        }
        break;
    case WM_DRAWITEM:
        if (wParam == kStartupCheckId)
        {
            DrawStartupControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        if (const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            item && item->CtlType == ODT_MENU && item->itemID == kTrayStatusId)
        {
            DrawTrayStatus(*item);
            return TRUE;
        }
        break;
    case WM_MEASUREITEM:
        if (auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            item && item->CtlType == ODT_MENU && item->itemID == kTrayStatusId)
        {
            item->itemWidth = static_cast<UINT>(Scale(window_, 250));
            item->itemHeight = static_cast<UINT>(Scale(window_, 24));
            return TRUE;
        }
        break;
    case kTrayCallbackMessage:
        if (lParam == WM_LBUTTONDBLCLK)
        {
            ShowMainWindow();
        }
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
        {
            POINT point{};
            GetCursorPos(&point);
            ShowTrayMenu(point);
        }
        return 0;
    case WM_SIZE:
        LayoutControls();
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = { Scale(window_, 760), Scale(window_, 580) };
        return 0;
    }
    case WM_DPICHANGED:
    {
        const auto suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        DestroyFonts();
        CreateFonts();
        for (const auto control : { startupCheck_, hideButton_, previousPageButton_,
            nextPageButton_ })
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
        LayoutControls();
        return 0;
    }
    case WM_CTLCOLORBTN:
    {
        const auto dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkColor(dc, kCard);
        SetDCBrushColor(dc, kCard);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_CLOSE:
        if (!exitRequested_)
        {
            HideMainWindow();
            return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0U) == SC_MINIMIZE)
        {
            HideMainWindow();
            return 0;
        }
        break;
    case WM_DESTROY:
        RemoveTrayIcon();
        host_.reset();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void ApplicationShell::OnSurfaceChange(SurfaceChange& change)
{
    if (!host_) return;
    lastChannel_ = change.channel;
    lastKind_ = change.kind;
    lastValue_ = change.value;
    lastCommandSent_ = host_->HandleSurfaceChange(change);
    InvalidateRect(window_, nullptr, FALSE);
}

void ApplicationShell::OnAudioFrame(AudioFrame& frame)
{
    if (!host_) return;
    monoAudioEnabled_ = frame.monoAudioEnabled;
    for (const auto& strip : frame.strips)
    {
        if (strip.slot >= 0 && strip.slot < EuconHost::MaxChannelCount)
            strips_[strip.slot] = strip;
    }
    activeCount_ = host_->ApplyAudioFrame(frame);
    UpdateChannelPagination();
    UpdateTrayTooltip();
    InvalidateRect(window_, nullptr, FALSE);
}

void ApplicationShell::CreateFonts()
{
    const auto dpi = static_cast<int>(GetDpiForWindow(window_));
    const auto font = [dpi](const int points, const int weight)
    {
        return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    titleFont_ = font(22, FW_SEMIBOLD);
    headingFont_ = font(13, FW_SEMIBOLD);
    bodyFont_ = font(10, FW_NORMAL);
    smallFont_ = font(9, FW_NORMAL);
}

void ApplicationShell::DestroyFonts()
{
    for (auto** font : { &titleFont_, &headingFont_, &bodyFont_, &smallFont_ })
    {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
}

void ApplicationShell::LayoutControls()
{
    if (!window_ || !startupCheck_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const auto margin = Scale(window_, 28);
    const auto cardInset = Scale(window_, 18);
    const auto settingsTop = client.bottom - Scale(window_, 116);
    const auto settingsBottom = client.bottom - margin;
    const auto hideButtonHeight = Scale(window_, 36);
    SetWindowPos(startupCheck_, nullptr, margin + cardInset,
        settingsTop + Scale(window_, 48), Scale(window_, 330), Scale(window_, 32),
        SWP_NOACTIVATE | SWP_NOZORDER);
    SetWindowPos(hideButton_, nullptr,
        client.right - margin - cardInset - Scale(window_, 140),
        settingsBottom - cardInset - hideButtonHeight,
        Scale(window_, 140), hideButtonHeight,
        SWP_NOACTIVATE | SWP_NOZORDER);
    const auto pageButtonSize = Scale(window_, 30);
    const auto pageButtonTop = Scale(window_, 208) + Scale(window_, 10);
    const auto nextPageLeft = client.right - margin - cardInset - pageButtonSize;
    SetWindowPos(nextPageButton_, nullptr, nextPageLeft, pageButtonTop,
        pageButtonSize, pageButtonSize, SWP_NOACTIVATE | SWP_NOZORDER);
    SetWindowPos(previousPageButton_, nullptr,
        nextPageLeft - pageButtonSize - Scale(window_, 6), pageButtonTop,
        pageButtonSize, pageButtonSize, SWP_NOACTIVATE | SWP_NOZORDER);
    UpdateChannelPagination();
}

int ApplicationShell::ChannelRowsPerPage() const
{
    if (!window_) return 1;
    RECT client{};
    GetClientRect(window_, &client);
    const auto channelsTop = Scale(window_, 208);
    const auto channelsBottom = client.bottom - Scale(window_, 132);
    const auto rowHeight = Scale(window_, 31);
    return std::max(1, static_cast<int>((channelsBottom - channelsTop -
        Scale(window_, 54)) / rowHeight));
}

void ApplicationShell::UpdateChannelPagination()
{
    const auto rowsPerPage = ChannelRowsPerPage();
    const auto pageCount = std::max(1, (activeCount_ + rowsPerPage - 1) / rowsPerPage);
    channelPage_ = std::clamp(channelPage_, 0, pageCount - 1);
    const auto multiplePages = pageCount > 1;
    if (previousPageButton_)
    {
        ShowWindow(previousPageButton_, multiplePages ? SW_SHOWNA : SW_HIDE);
        EnableWindow(previousPageButton_, channelPage_ > 0);
    }
    if (nextPageButton_)
    {
        ShowWindow(nextPageButton_, multiplePages ? SW_SHOWNA : SW_HIDE);
        EnableWindow(nextPageButton_, channelPage_ + 1 < pageCount);
    }
}

void ApplicationShell::Paint()
{
    PAINTSTRUCT paint{};
    const auto target = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    const auto memory = CreateCompatibleDC(target);
    const auto bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    const auto previousBitmap = SelectObject(memory, bitmap);
    const auto background = CreateSolidBrush(kBackground);
    FillRect(memory, &client, background);
    DeleteObject(background);

    const auto margin = Scale(window_, 28);
    const auto iconSize = Scale(window_, 58);
    DrawIconEx(memory, margin, Scale(window_, 24), largeIcon_, iconSize, iconSize,
        0, nullptr, DI_NORMAL);
    RECT titleRect{ margin + iconSize + Scale(window_, 18), Scale(window_, 18),
        client.right - margin, Scale(window_, 60) };
    DrawTextLine(memory, kProductName, titleRect, titleFont_, kText);
    RECT subtitleRect{ titleRect.left, Scale(window_, 57), titleRect.right,
        Scale(window_, 84) };
    DrawTextLine(memory, L"Native EUCON bridge for the Windows audio mixer",
        subtitleRect, bodyFont_, kMutedText);

    RECT statusCard{ margin, Scale(window_, 104), client.right - margin,
        Scale(window_, 190) };
    FillRoundedRect(memory, statusCard, kCard, kCardBorder, Scale(window_, 14));
    const auto ready = host_ && host_->IsReady();
    const auto statusColor = ready ? kGreen : kRed;
    const auto statusBrush = CreateSolidBrush(statusColor);
    SelectObject(memory, statusBrush);
    SelectObject(memory, GetStockObject(NULL_PEN));
    Ellipse(memory, statusCard.left + Scale(window_, 20), statusCard.top + Scale(window_, 27),
        statusCard.left + Scale(window_, 32), statusCard.top + Scale(window_, 39));
    DeleteObject(statusBrush);
    RECT statusTitle{ statusCard.left + Scale(window_, 46), statusCard.top + Scale(window_, 15),
        statusCard.right - Scale(window_, 180), statusCard.top + Scale(window_, 48) };
    DrawTextLine(memory, ready ? L"Bridge is active" : L"EUCON initialization failed",
        statusTitle, headingFont_, kText);
    RECT statusDetail{ statusTitle.left, statusCard.top + Scale(window_, 45),
        statusCard.right - Scale(window_, 180), statusCard.bottom - Scale(window_, 10) };
    DrawTextLine(memory, ready ? L"Running in the background · EuControl owns surface assignment" :
        L"Open diagnostics for the SDK return code and startup trace",
        statusDetail, smallFont_, kMutedText);
    RECT countRect{ statusCard.right - Scale(window_, 170), statusCard.top + Scale(window_, 12),
        statusCard.right - Scale(window_, 22), statusCard.top + Scale(window_, 49) };
    DrawTextLine(memory, std::to_wstring(activeCount_), countRect, titleFont_, kCyan,
        DT_RIGHT);
    RECT countLabel{ countRect.left, statusCard.top + Scale(window_, 47), countRect.right,
        statusCard.bottom - Scale(window_, 10) };
    DrawTextLine(memory, L"active channels", countLabel, smallFont_, kMutedText, DT_RIGHT);

    const auto settingsTop = client.bottom - Scale(window_, 116);
    RECT channelsCard{ margin, Scale(window_, 208), client.right - margin,
        settingsTop - Scale(window_, 16) };
    FillRoundedRect(memory, channelsCard, kCard, kCardBorder, Scale(window_, 14));
    RECT channelsTitle{ channelsCard.left + Scale(window_, 18), channelsCard.top + Scale(window_, 8),
        channelsCard.right - Scale(window_, 18), channelsCard.top + Scale(window_, 43) };
    DrawTextLine(memory, L"Windows audio channels", channelsTitle, headingFont_, kText);
    const auto rowsAvailable = ChannelRowsPerPage();
    const auto pageCount = std::max(1, (activeCount_ + rowsAvailable - 1) / rowsAvailable);
    const auto paged = pageCount > 1;
    RECT monoRect{ channelsCard.right - Scale(window_, paged ? 360 : 190),
        channelsCard.top + Scale(window_, 8),
        channelsCard.right - Scale(window_, paged ? 200 : 18),
        channelsCard.top + Scale(window_, 43) };
    DrawTextLine(memory, monoAudioEnabled_ ? L"MONO OUTPUT" : L"STEREO OUTPUT", monoRect,
        smallFont_, monoAudioEnabled_ ? kCyan : kMutedText, DT_RIGHT);
    if (paged)
    {
        RECT pageRect{ channelsCard.right - Scale(window_, 190),
            channelsCard.top + Scale(window_, 8), channelsCard.right - Scale(window_, 92),
            channelsCard.top + Scale(window_, 43) };
        DrawTextLine(memory, L"PAGE " + std::to_wstring(channelPage_ + 1) + L" / " +
            std::to_wstring(pageCount), pageRect, smallFont_, kMutedText, DT_RIGHT);
    }

    std::vector<const AudioStripState*> visible;
    for (const auto& strip : strips_) if (strip.active) visible.push_back(&strip);
    std::stable_sort(visible.begin(), visible.end(), [](const auto* left, const auto* right)
    {
        return left->sortGroup != right->sortGroup ? left->sortGroup < right->sortGroup :
            left->name < right->name;
    });
    const auto rowHeight = Scale(window_, 31);
    const auto pageStart = channelPage_ * rowsAvailable;
    const auto pageSize = std::min(rowsAvailable,
        std::max(0, static_cast<int>(visible.size()) - pageStart));
    for (int index = 0; index < pageSize; ++index)
    {
        const auto visibleIndex = pageStart + index;
        const auto* strip = visible[static_cast<std::size_t>(visibleIndex)];
        const auto top = channelsCard.top + Scale(window_, 44) + index * rowHeight;
        if (index > 0)
        {
            const auto pen = CreatePen(PS_SOLID, 1, RGB(34, 41, 52));
            const auto previousPen = SelectObject(memory, pen);
            MoveToEx(memory, channelsCard.left + Scale(window_, 18), top, nullptr);
            LineTo(memory, channelsCard.right - Scale(window_, 18), top);
            SelectObject(memory, previousPen);
            DeleteObject(pen);
        }
        RECT channelNumber{ channelsCard.left + Scale(window_, 18), top,
            channelsCard.left + Scale(window_, 62), top + rowHeight };
        DrawTextLine(memory, L"CH " + std::to_wstring(visibleIndex + 1), channelNumber,
            smallFont_, kMutedText);
        RECT channelName{ channelNumber.right, top, channelsCard.right - Scale(window_, 190),
            top + rowHeight };
        DrawTextLine(memory, strip->name, channelName, bodyFont_, kText);
        std::wstring flags;
        if (strip->muted) flags += L"MUTE  ";
        if (strip->isDefault) flags += L"DEFAULT  ";
        RECT channelFlags{ channelsCard.right - Scale(window_, 260), top,
            channelsCard.right - Scale(window_, 92), top + rowHeight };
        DrawTextLine(memory, flags, channelFlags, smallFont_, strip->muted ? kRed : kCyan,
            DT_RIGHT);
        RECT channelVolume{ channelsCard.right - Scale(window_, 82), top,
            channelsCard.right - Scale(window_, 18), top + rowHeight };
        DrawTextLine(memory, std::to_wstring(static_cast<int>(std::lround(
            strip->volume * 100.0F))) + L"%", channelVolume, bodyFont_, kText, DT_RIGHT);
    }
    if (visible.empty())
    {
        RECT empty{ channelsCard.left + Scale(window_, 18), channelsCard.top + Scale(window_, 50),
            channelsCard.right - Scale(window_, 18), channelsCard.bottom - Scale(window_, 12) };
        DrawTextLine(memory, L"Waiting for an active Windows audio session…", empty,
            bodyFont_, kMutedText);
    }

    RECT settingsCard{ margin, settingsTop, client.right - margin, client.bottom - margin };
    FillRoundedRect(memory, settingsCard, kCard, kCardBorder, Scale(window_, 14));
    RECT settingsTitle{ settingsCard.left + Scale(window_, 18), settingsCard.top + Scale(window_, 8),
        settingsCard.right - Scale(window_, 18), settingsCard.top + Scale(window_, 40) };
    DrawTextLine(memory, L"Background & startup", settingsTitle, headingFont_, kText);
    if (lastChannel_ >= 0)
    {
        RECT activity{ settingsCard.left + Scale(window_, 360), settingsCard.top + Scale(window_, 8),
            settingsCard.right - Scale(window_, 18), settingsCard.top + Scale(window_, 39) };
        const auto summary = L"Last: CH " + std::to_wstring(lastChannel_ + 1) + L" · " +
            ChangeKindName(lastKind_) + L" · " +
            std::to_wstring(static_cast<int>(std::lround(lastValue_ * 100.0F))) + L"% · " +
            (lastCommandSent_ ? L"OK" : L"not applied");
        DrawTextLine(memory, summary, activity, smallFont_, kMutedText, DT_RIGHT);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(window_, &paint);
}

void ApplicationShell::DrawStartupControl(const DRAWITEMSTRUCT& item) const
{
    const auto dc = item.hDC;
    SetDCBrushColor(dc, kCard);
    FillRect(dc, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const auto boxSize = Scale(window_, 18);
    RECT box{ item.rcItem.left, item.rcItem.top + (item.rcItem.bottom - item.rcItem.top -
        boxSize) / 2, item.rcItem.left + boxSize,
        item.rcItem.top + (item.rcItem.bottom - item.rcItem.top + boxSize) / 2 };
    const auto checked = startupEnabled_;
    const auto fill = CreateSolidBrush(checked ? kCyan : RGB(14, 18, 25));
    const auto pen = CreatePen(PS_SOLID, 1, checked ? kCyan : RGB(88, 99, 116));
    const auto oldBrush = SelectObject(dc, fill);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, box.left, box.top, box.right, box.bottom, Scale(window_, 4),
        Scale(window_, 4));
    if (checked)
    {
        const auto checkPen = CreatePen(PS_SOLID, Scale(window_, 2), RGB(8, 32, 36));
        SelectObject(dc, checkPen);
        MoveToEx(dc, box.left + Scale(window_, 4), box.top + Scale(window_, 9), nullptr);
        LineTo(dc, box.left + Scale(window_, 8), box.top + Scale(window_, 13));
        LineTo(dc, box.left + Scale(window_, 15), box.top + Scale(window_, 5));
        SelectObject(dc, oldPen);
        DeleteObject(checkPen);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(fill);
    RECT textRect{ box.right + Scale(window_, 10), item.rcItem.top, item.rcItem.right,
        item.rcItem.bottom };
    DrawTextLine(dc, L"Start with Windows", textRect,
        bodyFont_, kText);
    if ((item.itemState & ODS_FOCUS) != 0)
    {
        RECT focus = item.rcItem;
        focus.left = textRect.left - Scale(window_, 3);
        DrawFocusRect(dc, &focus);
    }
}

void ApplicationShell::DrawTrayStatus(const DRAWITEMSTRUCT& item) const
{
    const auto dc = item.hDC;
    const auto background = CreateSolidBrush(GetSysColor(COLOR_MENU));
    FillRect(dc, &item.rcItem, background);
    DeleteObject(background);

    const auto ready = host_ && host_->IsReady();
    const auto dotSize = Scale(window_, 8);
    const auto dotLeft = item.rcItem.left + Scale(window_, 12);
    const auto dotTop = item.rcItem.top + (item.rcItem.bottom - item.rcItem.top - dotSize) / 2;
    const auto dotBrush = CreateSolidBrush(ready ? kGreen : kRed);
    const auto previousBrush = SelectObject(dc, dotBrush);
    const auto previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, dotLeft, dotTop, dotLeft + dotSize, dotTop + dotSize);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(dotBrush);

    const auto* text = reinterpret_cast<const wchar_t*>(item.itemData);
    RECT textRect{ item.rcItem.left + Scale(window_, 30), item.rcItem.top,
        item.rcItem.right - Scale(window_, 8), item.rcItem.bottom };
    DrawTextLine(dc, text ? text : L"", textRect, bodyFont_,
        GetSysColor(COLOR_MENUTEXT));
}

void ApplicationShell::ShowMainWindow()
{
    startupEnabled_ = IsStartupEnabled();
    InvalidateRect(startupCheck_, nullptr, TRUE);
    ShowWindow(window_, SW_RESTORE);
    SetForegroundWindow(window_);
}

void ApplicationShell::HideMainWindow()
{
    ShowWindow(window_, SW_HIDE);
}

void ApplicationShell::AddTrayIcon()
{
    if (trayAdded_ || !window_) return;
    NOTIFYICONDATAW data{ sizeof(data) };
    data.hWnd = window_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayCallbackMessage;
    data.hIcon = smallIcon_;
    wcscpy_s(data.szTip, kProductName);
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void ApplicationShell::RemoveTrayIcon()
{
    if (!trayAdded_ || !window_) return;
    NOTIFYICONDATAW data{ sizeof(data) };
    data.hWnd = window_;
    data.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    trayAdded_ = false;
}

void ApplicationShell::UpdateTrayTooltip()
{
    if (!trayAdded_) return;
    NOTIFYICONDATAW data{ sizeof(data) };
    data.hWnd = window_;
    data.uID = kTrayIconId;
    data.uFlags = NIF_TIP;
    const auto text = std::wstring(kProductName) + L" · " +
        std::to_wstring(activeCount_) + L" active channels";
    wcsncpy_s(data.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void ApplicationShell::ShowTrayMenu(const POINT location)
{
    startupEnabled_ = IsStartupEnabled();
    InvalidateRect(startupCheck_, nullptr, TRUE);
    const auto menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, kTrayOpenId, L"Open Windows Fader Bridge for EUCON");
    const auto ready = host_ && host_->IsReady();
    const auto status = std::wstring(ready ? L"Bridge is active · " :
        L"EUCON initialization failed · ") + std::to_wstring(activeCount_) +
        (activeCount_ == 1 ? L" channel" : L" channels");
    AppendMenuW(menu, MF_OWNERDRAW | MF_DISABLED, kTrayStatusId,
        reinterpret_cast<LPCWSTR>(status.c_str()));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (startupEnabled_ ? MF_CHECKED : 0),
        kTrayStartupId, L"Start with Windows");
    AppendMenuW(menu, MF_STRING, kTrayRestartId, L"Restart");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayDiagnosticsId, L"Open diagnostics folder");
    AppendMenuW(menu, MF_STRING, kTrayAboutId, L"About Windows Fader Bridge for EUCON...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitId, L"Exit");
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        location.x, location.y, 0, window_, nullptr);
    DestroyMenu(menu);
}

void ApplicationShell::ShowAboutDialog() const
{
    const auto message = std::wstring(kProductName) + L" " + kProductVersion +
        L"\n\nA native, device-independent bridge between Windows Core Audio and Avid EUCON."
        L"\n\nEUCON surface discovery, assignment, banking, and hardware communication "
        L"are managed by EuControl or WSControl."
        L"\n\nCopyright 2026 Lindelea";
    MessageBoxW(window_, message.c_str(), L"About Windows Fader Bridge for EUCON",
        MB_OK | MB_ICONINFORMATION);
}

void ApplicationShell::RestartApplication()
{
    const auto parameters = std::wstring(L"--background --restart-from ") +
        std::to_wstring(GetCurrentProcessId());
    const auto result = ShellExecuteW(window_, L"open", ExecutablePath().c_str(),
        parameters.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        MessageBoxW(window_, L"Windows could not restart the application.",
            kProductName, MB_OK | MB_ICONERROR);
        return;
    }
    ExitApplication();
}

void ApplicationShell::ExitApplication()
{
    exitRequested_ = true;
    DestroyWindow(window_);
}

void ApplicationShell::OpenDiagnosticsFolder() const
{
    const auto directory = std::filesystem::path(ExecutablePath()).parent_path();
    ShellExecuteW(window_, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
