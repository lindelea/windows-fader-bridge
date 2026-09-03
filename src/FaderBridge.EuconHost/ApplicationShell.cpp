#include "ApplicationShell.h"

#include "../BridgeProductVersion.h"
#include "DiagnosticLog.h"
#include "resource.h"
#include "WindowsCommandExecutor.h"
#include "WindowsMediaObserver.h"
#include "WindowsMediaTimeline.h"

#include <CommCtrl.h>
#include <Dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
constexpr wchar_t kWindowClass[] = L"WindowsFaderBridge.MainWindow";
constexpr wchar_t kProductName[] = L"Windows Fader Bridge for EUCON";
constexpr wchar_t kProductVersion[] = BRIDGE_PRODUCT_VERSION_DISPLAY_W;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"WindowsFaderBridge";
constexpr UINT kTrayCallbackMessage = WM_APP + 100U;
constexpr UINT kTrayIconId = 1U;
constexpr int kStartupCheckId = 1001;
constexpr int kHideButtonId = 1002;
constexpr int kPreviousPageButtonId = 1003;
constexpr int kNextPageButtonId = 1004;
constexpr int kShortcutEnabledId = 1005;
constexpr int kShortcutBackgroundId = 1006;
constexpr int kShortcutButtonId = 1007;
constexpr int kLanguageComboId = 1008;
constexpr int kLicenseLinkId = 1009;
constexpr int kVersionLinkId = 1010;
constexpr int kIssuesLinkId = 1011;
constexpr int kNavigationBaseId = 1201;
constexpr int kGlobalShortcutId = 1;
constexpr UINT_PTR kReturnToBackgroundTimerId = 0x4653U;
constexpr wchar_t kShortcutSettingsKey[] =
    L"Software\\Lindelea\\Windows Fader Bridge\\EUCON";
constexpr int kTrayOpenId = 1101;
constexpr int kTrayStatusId = 1102;
constexpr int kTrayStartupId = 1103;
constexpr int kTrayRestartId = 1104;
constexpr int kTrayDiagnosticsId = 1105;
constexpr int kTrayAboutId = 1106;
constexpr int kTrayExitId = 1107;

constexpr COLORREF kBackground = RGB(16, 18, 22);
constexpr COLORREF kSidebar = RGB(20, 23, 28);
constexpr COLORREF kCard = RGB(25, 29, 36);
constexpr COLORREF kCardBorder = RGB(43, 49, 59);
constexpr COLORREF kSelection = RGB(37, 31, 51);
constexpr COLORREF kText = RGB(235, 238, 243);
constexpr COLORREF kMutedText = RGB(147, 157, 173);
// EUCON edition accent, shared visually with its purple three-fader icon.
constexpr COLORREF kAccent = RGB(155, 108, 255);
constexpr COLORREF kGreen = RGB(81, 204, 156);
constexpr COLORREF kRed = RGB(246, 126, 126);
constexpr wchar_t kProjectUrl[] = L"https://github.com/lindelea/windows-fader-bridge";
constexpr wchar_t kLicenseUrl[] =
    L"https://github.com/lindelea/windows-fader-bridge/blob/main/LICENSE";
constexpr wchar_t kIssuesUrl[] =
    L"https://github.com/lindelea/windows-fader-bridge/issues";

std::wstring WindowText(const HWND window)
{
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(window)) + 1U, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(window, text.data(),
        static_cast<int>(text.size()))));
    return text;
}

std::wstring TimeText(const double seconds)
{
    const auto total = static_cast<unsigned long long>(std::max(0.0, seconds));
    const auto hours = total / 3600ULL;
    const auto minutes = (total / 60ULL) % 60ULL;
    const auto remaining = total % 60ULL;
    std::wostringstream text;
    text << std::setfill(L'0');
    text << std::setw(2) << hours << L":" << std::setw(2) << minutes << L":"
        << std::setw(2) << remaining;
    return text.str();
}

std::wstring ClockText()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t value[16]{};
    swprintf_s(value, L"%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    return value;
}

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

DWORD ReadShortcutValue(HKEY key, const wchar_t* name, DWORD fallback)
{
    DWORD value = fallback;
    DWORD size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
        ERROR_SUCCESS ? value : fallback;
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

const wchar_t* ChangeKindName(const int kind, const bool chinese)
{
    switch (kind)
    {
    case 0: return chinese ? L"推子" : L"Fader";
    case 1: return chinese ? L"音量旋钮" : L"Volume knob";
    case 2: return chinese ? L"静音" : L"Mute";
    case 3: return chinese ? L"默认设备" : L"Default device";
    case 4: return chinese ? L"独奏" : L"Solo";
    case 5: return chinese ? L"选择" : L"Select";
    case 6: return chinese ? L"关注" : L"Attention";
    case 7: return L"Pan";
    case 8: return chinese ? L"Pan 居中" : L"Pan center";
    default: return chinese ? L"控制" : L"Control";
    }
}
}

ApplicationShell::ApplicationShell(const HINSTANCE instance) : instance_(instance)
{
}

ApplicationShell::~ApplicationShell()
{
    RemoveTrayIcon();
    mediaObserver_.reset();
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

    RECT bounds{0, 0, Scale(GetDesktopWindow(), 1180), Scale(GetDesktopWindow(), 740)};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min(static_cast<int>(bounds.right - bounds.left),
        static_cast<int>(work.right - work.left));
    const int height = std::min(static_cast<int>(bounds.bottom - bounds.top),
        static_cast<int>(work.bottom - work.top));
    const int x = work.left + std::max(0, (static_cast<int>(work.right - work.left) - width) / 2);
    const int y = work.top + std::max(0, (static_cast<int>(work.bottom - work.top) - height) / 2);
    window_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, kProductName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
        nullptr, nullptr, instance_, this);
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

LRESULT CALLBACK ApplicationShell::LanguageProcedure(const HWND window,
    const UINT message, const WPARAM wParam, const LPARAM lParam,
    const UINT_PTR subclassId, const DWORD_PTR data)
{
    auto* shell = reinterpret_cast<ApplicationShell*>(data);
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, LanguageProcedure, subclassId);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (!shell) return DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCPAINT)
    {
        const auto dc = GetWindowDC(window);
        RECT rect{};
        GetWindowRect(window, &rect);
        OffsetRect(&rect, -rect.left, -rect.top);
        const auto brush = CreateSolidBrush(GetFocus() == window ? kAccent : kCardBorder);
        FrameRect(dc, &rect, brush);
        DeleteObject(brush);
        ReleaseDC(window, dc);
        return 0;
    }
    if (message == WM_PAINT || message == WM_PRINTCLIENT)
    {
        PAINTSTRUCT paint{};
        const auto dc = message == WM_PAINT ? BeginPaint(window, &paint) :
            reinterpret_cast<HDC>(wParam);
        if (!dc) return 0;
        RECT rect{};
        GetClientRect(window, &rect);
        const auto fill = CreateSolidBrush(kCard);
        FillRect(dc, &rect, fill);
        DeleteObject(fill);
        const auto pen = CreatePen(PS_SOLID, 1, GetFocus() == window ? kAccent : kCardBorder);
        const auto oldPen = SelectObject(dc, pen);
        const auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, 0, 0, rect.right, rect.bottom, Scale(shell->window_, 8),
            Scale(shell->window_, 8));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        const auto selected = static_cast<int>(SendMessageW(window, CB_GETCURSEL, 0, 0));
        std::wstring label;
        if (selected >= 0)
        {
            const auto length = static_cast<int>(SendMessageW(window,
                CB_GETLBTEXTLEN, selected, 0));
            if (length >= 0 && length < 32768)
            {
                label.resize(static_cast<size_t>(length) + 1U);
                SendMessageW(window, CB_GETLBTEXT, selected,
                    reinterpret_cast<LPARAM>(label.data()));
                label.resize(static_cast<size_t>(length));
            }
        }
        auto textRect = rect;
        textRect.left += Scale(shell->window_, 12);
        textRect.right -= Scale(shell->window_, 48);
        DrawTextLine(dc, label, textRect, shell->bodyFont_, kText);
        const auto oldFont = SelectObject(dc, shell->iconFont_);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kText);
        RECT arrow{rect.right - Scale(shell->window_, 34), 0,
            rect.right - Scale(shell->window_, 8), rect.bottom};
        DrawTextW(dc, L"\uE70D", 1, &arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldFont);
        if (message == WM_PAINT) EndPaint(window, &paint);
        return 0;
    }
    const auto result = DefSubclassProc(window, message, wParam, lParam);
    if (message == CB_SETCURSEL || message == WM_SETFOCUS || message == WM_KILLFOCUS ||
        message == WM_ENABLE || message == CB_SHOWDROPDOWN)
        InvalidateRect(window, nullptr, FALSE);
    return result;
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
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHideButtonId)), instance_, nullptr);
    previousPageButton_ = CreateWindowExW(0, L"BUTTON", L"Previous bank",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPreviousPageButtonId)), instance_, nullptr);
    nextPageButton_ = CreateWindowExW(0, L"BUTTON", L"Next bank",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNextPageButtonId)), instance_, nullptr);
    shortcutEnabledCheck_ = CreateWindowExW(0, L"BUTTON", L"Enable global summon shortcut",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShortcutEnabledId)), instance_, nullptr);
    shortcutBackgroundCheck_ = CreateWindowExW(0, L"BUTTON", L"Return to background after detection",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShortcutBackgroundId)), instance_, nullptr);
    shortcutButton_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShortcutButtonId)), instance_, nullptr);
    licenseLink_ = CreateWindowExW(0, L"BUTTON", L"MPL 2.0", WS_CHILD | WS_TABSTOP |
        BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLicenseLinkId)), instance_, nullptr);
    versionLink_ = CreateWindowExW(0, L"BUTTON", kProductVersion, WS_CHILD | WS_TABSTOP |
        BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVersionLinkId)), instance_, nullptr);
    issuesLink_ = CreateWindowExW(0, L"BUTTON", L"Report an issue", WS_CHILD | WS_TABSTOP |
        BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIssuesLinkId)), instance_, nullptr);
    languageCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST |
        CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLanguageComboId)), instance_, nullptr);
    SendMessageW(languageCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"简体中文"));
    SendMessageW(languageCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
    SetWindowTheme(languageCombo_, L"", L"");
    SetWindowSubclass(languageCombo_, LanguageProcedure, 1,
        reinterpret_cast<DWORD_PTR>(this));
    COMBOBOXINFO comboInfo{sizeof(comboInfo)};
    if (GetComboBoxInfo(languageCombo_, &comboInfo))
        SetWindowTheme(comboInfo.hwndList, L"DarkMode_Explorer", nullptr);
    constexpr const wchar_t* navigationLabels[]{L"Overview", L"General", L"About"};
    for (int index = 0; index < 3; ++index)
        navigationButtons_[static_cast<size_t>(index)] = CreateWindowExW(0, L"BUTTON",
            navigationLabels[index], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNavigationBaseId + index)),
            instance_, nullptr);
    for (const auto control : { startupCheck_, hideButton_, previousPageButton_,
        nextPageButton_, shortcutEnabledCheck_, shortcutBackgroundCheck_, shortcutButton_,
        languageCombo_, licenseLink_, versionLink_, issuesLink_ })
    {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
    for (const auto control : navigationButtons_)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
    // BS_OWNERDRAW does not retain the checkbox state reported by BM_SETCHECK.
    // Keep the rendered state synchronized with the actual Run registry value.
    startupEnabled_ = IsStartupEnabled();
    LoadShortcutSettings();
    UpdateLanguage();
    ApplyShortcut(shortcutEnabled_, shortcut_, false);

    host_ = std::make_unique<EuconHost>(window_);
    mediaObserver_ = std::make_unique<WindowsMediaObserver>();
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
    if (message == bridge::SummonMessage())
    {
        FB_TRACE("EUCON_SUMMON source=%llu", static_cast<unsigned long long>(wParam));
        if (wParam == bridge::SummonFromEuconKeyCommand && shortcutEnabled_ &&
            bridge::InvokeRegisteredShortcut(shortcut_))
        {
            FB_TRACE("EUCON_SUMMON_HOTKEY_DISPATCHED");
            return 0;
        }
        SummonForEucon();
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
        if (wParam == kReturnToBackgroundTimerId)
        {
            KillTimer(window_, kReturnToBackgroundTimerId);
            // EuControl completes application recognition asynchronously. Apply
            // the user's requested final window state after that focus handoff
            // instead of only scheduling work when background return is on.
            // This keeps EUCON Key Commands and the global hotkey identical.
            if (shortcutReturnToBackground_)
                HideMainWindow();
            else
                ShowMainWindow();
            FB_TRACE("EUCON_SUMMON_SETTLED returnToBackground=%d visible=%d iconic=%d",
                shortcutReturnToBackground_ ? 1 : 0,
                IsWindowVisible(window_) ? 1 : 0, IsIconic(window_) ? 1 : 0);
            return 0;
        }
        if (wParam == kMotorFlushTimerId && host_)
        {
            host_->FlushPendingMotors();
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (page_ == 0 && host_)
        {
            const int dpi = static_cast<int>(GetDpiForWindow(window_));
            const int x = MulDiv(GET_X_LPARAM(lParam), 96, dpi);
            const int y = MulDiv(GET_Y_LPARAM(lParam), 96, dpi);
            RECT client{};
            GetClientRect(window_, &client);
            const int logicalWidth = MulDiv(client.right, 96, dpi);
            const int rowsAvailable = ChannelRowsPerPage();
            if (x >= 256 && x < logicalWidth - 32 && y >= 242 &&
                y < 242 + ChannelListHeight())
            {
                std::vector<const AudioStripState*> visible;
                for (const auto& strip : strips_)
                    if (strip.active) visible.push_back(&strip);
                std::stable_sort(visible.begin(), visible.end(),
                    [](const auto* left, const auto* right)
                    {
                        return left->sortGroup < right->sortGroup;
                    });
                const int pageStart = channelPage_ * rowsAvailable;
                const int pageSize = std::min(rowsAvailable,
                    std::max(0, static_cast<int>(visible.size()) - pageStart));
                const int rowHeight = ChannelRowHeight(pageSize);
                const int row = (y - 242) / rowHeight;
                const int visibleIndex = pageStart + row;
                if (row >= 0 && row < pageSize && visibleIndex >= 0 &&
                    visibleIndex < static_cast<int>(visible.size()) &&
                    host_->SelectTrack(visible[static_cast<size_t>(visibleIndex)]->key))
                {
                    selectedTrackKey_ = visible[static_cast<size_t>(visibleIndex)]->key;
                    InvalidateRect(window_, nullptr, FALSE);
                }
            }
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kLanguageComboId && HIWORD(wParam) == CBN_SELCHANGE)
        {
            const auto selection = static_cast<int>(SendMessageW(languageCombo_,
                CB_GETCURSEL, 0, 0));
            if (selection == 0 || selection == 1)
            {
                const bool previous = chinese_;
                chinese_ = selection == 0;
                if (!SaveShortcutSettings())
                {
                    chinese_ = previous;
                    SendMessageW(languageCombo_, CB_SETCURSEL, chinese_ ? 0 : 1, 0);
                    MessageBoxW(window_, T(L"无法保存语言设置，已保留原设置。",
                        L"Windows could not save the language setting. The previous setting remains active."),
                        kProductName, MB_OK | MB_ICONWARNING);
                }
                UpdateLanguage();
            }
            return 0;
        }
        if (LOWORD(wParam) >= kNavigationBaseId && LOWORD(wParam) < kNavigationBaseId + 3)
        {
            page_ = LOWORD(wParam) - kNavigationBaseId;
            LayoutControls();
            InvalidateRect(window_, nullptr, FALSE);
            for (const auto navigation : navigationButtons_)
                InvalidateRect(navigation, nullptr, TRUE);
            return 0;
        }
        switch (LOWORD(wParam))
        {
        case kStartupCheckId:
        {
            const auto enabled = !startupEnabled_;
            if (!SetStartupEnabled(enabled))
            {
                MessageBoxW(window_, T(L"无法更新开机启动设置。",
                    L"Windows could not update the startup setting."),
                    kProductName, MB_OK | MB_ICONERROR);
            }
            startupEnabled_ = IsStartupEnabled();
            InvalidateRect(startupCheck_, nullptr, TRUE);
            return 0;
        }
        case kHideButtonId: HideMainWindow(); return 0;
        case kShortcutEnabledId:
        {
            const bool enabled = !shortcutEnabled_;
            if (ApplyShortcut(enabled, shortcut_, true))
            {
                const bool previous = shortcutEnabled_;
                shortcutEnabled_ = enabled;
                if (!SaveShortcutSettings())
                {
                    ApplyShortcut(previous, shortcut_, false);
                    shortcutEnabled_ = previous;
                    MessageBoxW(window_, T(L"无法保存快捷键设置，已保留原设置。",
                        L"Windows could not save the shortcut setting. The previous setting remains active."),
                        kProductName, MB_OK | MB_ICONWARNING);
                }
            }
            InvalidateRect(shortcutEnabledCheck_, nullptr, TRUE);
            return 0;
        }
        case kShortcutBackgroundId:
        {
            const bool previous = shortcutReturnToBackground_;
            shortcutReturnToBackground_ = !shortcutReturnToBackground_;
            if (!SaveShortcutSettings())
            {
                shortcutReturnToBackground_ = previous;
                MessageBoxW(window_, T(L"无法保存快捷键设置，已保留原设置。",
                    L"Windows could not save the shortcut setting. The previous setting remains active."),
                    kProductName, MB_OK | MB_ICONWARNING);
            }
            InvalidateRect(shortcutBackgroundCheck_, nullptr, TRUE);
            return 0;
        }
        case kShortcutButtonId:
        {
            auto next = shortcut_;
            if (bridge::CaptureShortcut(window_, shortcut_, next, chinese_) &&
                ApplyShortcut(shortcutEnabled_, next, true))
            {
                const auto previous = shortcut_;
                shortcut_ = next;
                if (!SaveShortcutSettings())
                {
                    ApplyShortcut(shortcutEnabled_, previous, false);
                    shortcut_ = previous;
                    MessageBoxW(window_, T(L"无法保存快捷键，已保留原快捷键。",
                        L"Windows could not save the shortcut setting. The previous shortcut remains active."),
                        kProductName, MB_OK | MB_ICONWARNING);
                }
                SetWindowTextW(shortcutButton_, bridge::ShortcutText(shortcut_).c_str());
            }
            return 0;
        }
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
        case kLicenseLinkId:
            ShellExecuteW(window_, L"open", kLicenseUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case kVersionLinkId:
            ShellExecuteW(window_, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case kIssuesLinkId:
            ShellExecuteW(window_, L"open", kIssuesUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case kTrayOpenId: ShowMainWindow(); return 0;
        case kTrayStartupId:
            if (!SetStartupEnabled(!startupEnabled_))
            {
                MessageBoxW(window_, T(L"无法更新开机启动设置。",
                    L"Windows could not update the startup setting."),
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
    case WM_HOTKEY:
        if (wParam == kGlobalShortcutId)
        {
            SummonForEucon();
            return 0;
        }
        break;
    case WM_DRAWITEM:
        if (wParam == kLanguageComboId)
        {
            DrawLanguageControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        if ((wParam >= kNavigationBaseId && wParam < kNavigationBaseId + 3) ||
            wParam == kHideButtonId || wParam == kPreviousPageButtonId ||
            wParam == kNextPageButtonId || wParam == kShortcutButtonId ||
            wParam == kLicenseLinkId || wParam == kVersionLinkId ||
            wParam == kIssuesLinkId)
        {
            DrawButtonControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        if (wParam == kStartupCheckId)
        {
            DrawStartupControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        if (wParam == kShortcutEnabledId)
        {
            DrawCheckboxControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam),
                L"Enable global summon shortcut", shortcutEnabled_);
            return TRUE;
        }
        if (wParam == kShortcutBackgroundId)
        {
            DrawCheckboxControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam),
                L"Return to background after detection", shortcutReturnToBackground_);
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
            item && item->CtlType == ODT_COMBOBOX && item->CtlID == kLanguageComboId)
        {
            item->itemHeight = static_cast<UINT>(Scale(window_, 30));
            return TRUE;
        }
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
        info->ptMinTrackSize = { Scale(window_, 1080), Scale(window_, 690) };
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
            nextPageButton_, shortcutEnabledCheck_, shortcutBackgroundCheck_, shortcutButton_,
            languageCombo_, licenseLink_, versionLink_, issuesLink_ })
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
        for (const auto control : navigationButtons_)
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
        LayoutControls();
        return 0;
    }
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
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
    if (change.kind != 6 && !change.trackKey.empty()) selectedTrackKey_ = change.trackKey;
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
    const auto font = [dpi](const int logicalPixels, const int weight)
    {
        return CreateFontW(-MulDiv(logicalPixels, dpi, 96), 0, 0, 0, weight, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    };
    titleFont_ = font(28, FW_SEMIBOLD);
    headingFont_ = font(18, FW_SEMIBOLD);
    bodyFont_ = font(14, FW_NORMAL);
    smallFont_ = font(12, FW_NORMAL);
    microFont_ = font(11, FW_NORMAL);
    iconFont_ = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe Fluent Icons");
}

void ApplicationShell::DestroyFonts()
{
    for (auto** font : { &titleFont_, &headingFont_, &bodyFont_, &smallFont_,
        &microFont_, &iconFont_ })
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
    const int height = MulDiv(client.bottom, 96, static_cast<int>(GetDpiForWindow(window_)));
    const int width = MulDiv(client.right, 96, static_cast<int>(GetDpiForWindow(window_)));
    const auto place = [&](HWND control, int x, int y, int w, int h, bool visible = true) {
        SetWindowPos(control, nullptr, Scale(window_, x), Scale(window_, y), Scale(window_, w),
            Scale(window_, h), SWP_NOACTIVATE | SWP_NOZORDER);
        ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    };
    for (int index = 0; index < 3; ++index)
        place(navigationButtons_[static_cast<size_t>(index)], 16,
            index == 0 ? 138 : 220 + (index - 1) * 44, 192, 38);
    place(hideButton_, 24, height - 66, 176, 36);

    const bool overview = page_ == 0;
    const bool general = page_ == 1;
    const bool about = page_ == 2;
    const int mediaTop = height - 32 - 86;
    const int pagerTop = mediaTop - 16 - 32;
    place(previousPageButton_, 256, pagerTop, 122, 32, overview);
    place(nextPageButton_, 388, pagerTop, 122, 32, overview);
    place(languageCombo_, width - 280, 164, 224, 200, general);
    place(startupCheck_, width - 112, 282, 56, 30, general);
    place(shortcutEnabledCheck_, width - 112, 398, 56, 30, general);
    place(shortcutButton_, width - 330, 436, 274, 38, general);
    place(shortcutBackgroundCheck_, width - 112, 548, 56, 30, general);
    place(licenseLink_, 368, 426, 70, 32, about);
    place(versionLink_, 462, 426, 80, 32, about);
    place(issuesLink_, width - 216, 426, 160, 32, about);
    UpdateChannelPagination();
}

int ApplicationShell::ChannelRowsPerPage() const
{
    return std::clamp((ChannelListHeight() - 4) / 60, 1, 5);
}

int ApplicationShell::ChannelListHeight() const
{
    if (!window_) return 110;
    RECT client{};
    GetClientRect(window_, &client);
    const int height = MulDiv(client.bottom, 96, static_cast<int>(GetDpiForWindow(window_)));
    const int mediaTop = height - 32 - 86;
    const int pagerTop = mediaTop - 16 - 32;
    return std::max(110, pagerTop - 16 - 242);
}

int ApplicationShell::ChannelRowHeight(const int pageSize) const
{
    const auto rowsAvailable = ChannelRowsPerPage();
    const auto displaySlots = pageSize == 4 ? 4 : rowsAvailable;
    return std::max(1, ChannelListHeight() / std::max(1, displaySlots));
}

void ApplicationShell::UpdateChannelPagination()
{
    const auto rowsPerPage = ChannelRowsPerPage();
    const auto pageCount = std::max(1, (activeCount_ + rowsPerPage - 1) / rowsPerPage);
    channelPage_ = std::clamp(channelPage_, 0, pageCount - 1);
    if (previousPageButton_)
    {
        ShowWindow(previousPageButton_, page_ == 0 ? SW_SHOWNA : SW_HIDE);
        EnableWindow(previousPageButton_, channelPage_ > 0);
    }
    if (nextPageButton_)
    {
        ShowWindow(nextPageButton_, page_ == 0 ? SW_SHOWNA : SW_HIDE);
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

    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    const int logicalWidth = MulDiv(client.right, 96, dpi);
    const int logicalHeight = MulDiv(client.bottom, 96, dpi);
    const int mainX = 256;
    const int mainWidth = logicalWidth - 288;
    const int mediaTop = logicalHeight - 32 - 86;
    const int pagerTop = mediaTop - 16 - 32;
    const auto logicalRect = [&](int x, int y, int width, int height) {
        return RECT{Scale(window_, x), Scale(window_, y), Scale(window_, x + width),
            Scale(window_, y + height)};
    };

    RECT sidebar{0, 0, Scale(window_, 224), client.bottom};
    const auto sidebarBrush = CreateSolidBrush(kSidebar);
    FillRect(memory, &sidebar, sidebarBrush);
    DeleteObject(sidebarBrush);
    const auto divider = CreatePen(PS_SOLID, 1, kCardBorder);
    const auto previousPen = SelectObject(memory, divider);
    MoveToEx(memory, Scale(window_, 224), 0, nullptr);
    LineTo(memory, Scale(window_, 224), client.bottom);
    SelectObject(memory, previousPen);
    DeleteObject(divider);

    DrawIconEx(memory, Scale(window_, 20), Scale(window_, 31), largeIcon_, Scale(window_, 40),
        Scale(window_, 40), 0, nullptr, DI_NORMAL);
    DrawTextLine(memory, L"Windows Fader Bridge", logicalRect(70, 28, 146, 24),
        smallFont_, kText);
    DrawTextLine(memory, L"for EUCON", logicalRect(70, 52, 146, 20), microFont_, kAccent);
    DrawTextLine(memory, T(L"工作区", L"WORKSPACE"), logicalRect(32, 104, 160, 22),
        microFont_, kMutedText);
    DrawTextLine(memory, T(L"设置", L"SETTINGS"), logicalRect(32, 187, 160, 22),
        microFont_, kMutedText);
    const wchar_t* pageTitlesZh[]{L"通道总览", L"通用设置", L"关于"};
    const wchar_t* pageTitlesEn[]{L"Overview", L"General", L"About"};
    DrawTextLine(memory, T(pageTitlesZh[page_], pageTitlesEn[page_]),
        logicalRect(mainX, 36, mainWidth, 42),
        titleFont_, kText);

    if (page_ == 0)
    {
        const auto ready = host_ && host_->IsReady();
        auto statusCard = logicalRect(mainX, 128, mainWidth, 84);
        FillRoundedRect(memory, statusCard, kCard, kCardBorder, Scale(window_, 12));
        const auto statusBrush = CreateSolidBrush(ready ? kGreen : kRed);
        const auto oldBrush = SelectObject(memory, statusBrush);
        const auto oldPen = SelectObject(memory, GetStockObject(NULL_PEN));
        Ellipse(memory, Scale(window_, mainX + 20), Scale(window_, 152),
            Scale(window_, mainX + 28), Scale(window_, 160));
        SelectObject(memory, oldPen);
        SelectObject(memory, oldBrush);
        DeleteObject(statusBrush);
        DrawTextLine(memory, ready ? T(L"桥接已运行", L"Bridge is active") :
            T(L"EUCON 初始化失败", L"EUCON initialization failed"),
            logicalRect(mainX + 42, 140, mainWidth - 236, 29), headingFont_, kText);
        DrawTextLine(memory, ready ? T(L"Windows Core Audio 已连接",
            L"Windows Core Audio connected") : T(L"请打开诊断信息查看 SDK 启动结果",
            L"Open diagnostics for the SDK startup result"),
            logicalRect(mainX + 42, 171, mainWidth - 236, 24), smallFont_, kMutedText);
        DrawTextLine(memory, std::to_wstring(activeCount_),
            logicalRect(logicalWidth - 146, 136, 86, 40), titleFont_, kAccent, DT_RIGHT);
        DrawTextLine(memory, T(L"在线通道", L"ONLINE CHANNELS"),
            logicalRect(logicalWidth - 220, 178, 160, 20), microFont_, kMutedText, DT_RIGHT);

        DrawTextLine(memory, T(L"音频通道", L"AUDIO CHANNELS"),
            logicalRect(mainX, 217, 280, 24),
            microFont_, kMutedText);
        DrawTextLine(memory, monoAudioEnabled_ ? L"MONO" : L"STEREO",
            logicalRect(logicalWidth - 174, 217, 142, 24), microFont_, kAccent, DT_RIGHT);
        const int listHeight = ChannelListHeight();
        auto channelsCard = logicalRect(mainX, 242, mainWidth, listHeight);
        FillRoundedRect(memory, channelsCard, kCard, kCardBorder, Scale(window_, 12));
        const auto rowsAvailable = ChannelRowsPerPage();
        std::vector<const AudioStripState*> visible;
        for (const auto& strip : strips_) if (strip.active) visible.push_back(&strip);
        std::stable_sort(visible.begin(), visible.end(), [](const auto* left, const auto* right) {
            return left->sortGroup < right->sortGroup;
        });
        const int pageStart = channelPage_ * rowsAvailable;
        const int pageSize = std::min(rowsAvailable,
            std::max(0, static_cast<int>(visible.size()) - pageStart));
        const int rowHeight = ChannelRowHeight(pageSize);
        for (int index = 0; index < pageSize; ++index)
        {
            const int visibleIndex = pageStart + index;
            const auto* strip = visible[static_cast<size_t>(visibleIndex)];
            const int top = 242 + index * rowHeight;
            const int contentTop = top + std::max(4, (rowHeight - 53) / 2);
            const bool selected = selectedTrackKey_.empty() ? visibleIndex == 0 :
                strip->key == selectedTrackKey_;
            if (selected)
            {
                auto selectedRow = logicalRect(mainX + 1, top + 1, mainWidth - 2,
                    rowHeight - 2);
                const auto selectedBrush = CreateSolidBrush(kSelection);
                FillRect(memory, &selectedRow, selectedBrush);
                DeleteObject(selectedBrush);
                auto marker = logicalRect(mainX, top + 10, 3,
                    std::max(4, rowHeight - 20));
                const auto markerBrush = CreateSolidBrush(kAccent);
                FillRect(memory, &marker, markerBrush);
                DeleteObject(markerBrush);
            }
            if (index > 0)
            {
                const auto rowPen = CreatePen(PS_SOLID, 1, RGB(40, 46, 56));
                const auto oldRowPen = SelectObject(memory, rowPen);
                MoveToEx(memory, Scale(window_, mainX + 12), Scale(window_, top), nullptr);
                LineTo(memory, Scale(window_, mainX + mainWidth - 12), Scale(window_, top));
                SelectObject(memory, oldRowPen);
                DeleteObject(rowPen);
            }
            DrawTextLine(memory, std::to_wstring(visibleIndex + 1),
                logicalRect(mainX + 16, top, 30, rowHeight), smallFont_,
                selected ? kAccent : kMutedText);
            DrawTextLine(memory, strip->name, logicalRect(mainX + 60, contentTop,
                mainWidth - 390, 29), bodyFont_, kText);
            const wchar_t* role = strip->role == AudioStripRole::MasterOutput ?
                T(L"主输出", L"Master output") : strip->role == AudioStripRole::OutputDevice ?
                T(L"音频输出", L"Audio output") : strip->role == AudioStripRole::InputDevice ?
                T(L"音频输入", L"Audio input") : T(L"应用", L"Application");
            std::wstring detail = role;
            if (strip->isDefault) detail += T(L"  ·  默认设备", L"  ·  Default device");
            if (strip->muted) detail += T(L"  ·  已静音", L"  ·  Muted");
            DrawTextLine(memory, detail, logicalRect(mainX + 60, contentTop + 27,
                mainWidth - 390, 24), smallFont_, strip->muted ? kRed : kMutedText);
            std::wstring pan = L"—";
            if (strip->panAvailable)
            {
                if (std::fabs(strip->pan) < 0.01F) pan = T(L"居中", L"Center");
                else pan = std::wstring(strip->pan < 0.0F ? L"L " : L"R ") +
                    std::to_wstring(static_cast<int>(std::lround(
                        std::fabs(strip->pan) * 100.0F))) + L"%";
            }
            DrawTextLine(memory, pan, logicalRect(mainX + mainWidth - 290,
                top, 80, rowHeight), smallFont_, kMutedText, DT_RIGHT);
            const int barX = mainX + mainWidth - 200;
            auto bar = logicalRect(barX, top + (rowHeight - 5) / 2, 98, 5);
            const auto barBrush = CreateSolidBrush(RGB(42, 48, 58));
            FillRect(memory, &bar, barBrush);
            DeleteObject(barBrush);
            const auto peak = strip->meterDb.empty() ? strip->peakDb :
                *std::max_element(strip->meterDb.begin(), strip->meterDb.end());
            const auto meter = std::clamp((peak + 60.0F) / 60.0F, 0.0F, 1.0F);
            bar.right = bar.left + static_cast<LONG>((bar.right - bar.left) * meter);
            const auto levelBrush = CreateSolidBrush(strip->muted ? kMutedText :
                peak > -3.0F ? kAccent : kGreen);
            FillRect(memory, &bar, levelBrush);
            DeleteObject(levelBrush);
            DrawTextLine(memory, std::to_wstring(static_cast<int>(std::lround(
                strip->volume * 100.0F))) + L"%", logicalRect(mainX + mainWidth - 92,
                top, 68, rowHeight), headingFont_, kText, DT_RIGHT);
        }
        if (visible.empty())
            DrawTextLine(memory, T(L"等待活动的 Windows 音频会话",
                L"Waiting for an active Windows audio session"),
                logicalRect(mainX + 20, 260, mainWidth - 40, 40), bodyFont_, kMutedText);
        DrawTextLine(memory, L"CH " + std::to_wstring(activeCount_ ? pageStart + 1 : 0) + L"–" +
            std::to_wstring(std::min(pageStart + rowsAvailable, activeCount_)),
            logicalRect(mainX + 272, pagerTop + 2, mainWidth - 272, 28),
            smallFont_, kMutedText, DT_RIGHT);

        const auto media = mediaObserver_ ? mediaObserver_->Snapshot() : WindowsMediaState{};
        const auto playback = PlaybackTime(media.timeline, media.playing);
        auto mediaCard = logicalRect(mainX, mediaTop, mainWidth, 86);
        FillRoundedRect(memory, mediaCard, kCard, kCardBorder, Scale(window_, 12));
        DrawTextLine(memory, media.available ? (media.playing ?
            T(L"正在播放", L"NOW PLAYING") : T(L"已暂停", L"PAUSED")) :
            T(L"媒体待机", L"MEDIA IDLE"),
            logicalRect(mainX + 20, mediaTop + 10, mainWidth - 40, 18), microFont_,
            media.available ? kAccent : kMutedText);
        DrawTextLine(memory, media.available && !media.title.empty() ? media.title :
            T(L"等待播放器", L"Waiting for a player"),
            logicalRect(mainX + 20, mediaTop + 30, mainWidth - 224, 25), bodyFont_, kText);
        DrawTextLine(memory, media.available ? media.artist : L"",
            logicalRect(mainX + 20, mediaTop + 58, mainWidth - 224, 20), smallFont_, kMutedText);
        DrawTextLine(memory, playback.available ? TimeText(playback.elapsedSeconds) : ClockText(),
            logicalRect(logicalWidth - 214, mediaTop + 27, 158, 27), headingFont_, kText, DT_RIGHT);
        DrawTextLine(memory, playback.available ? L"/ " + TimeText(playback.durationSeconds) :
            T(L"系统时间", L"SYSTEM CLOCK"),
            logicalRect(logicalWidth - 214, mediaTop + 57, 158, 20), smallFont_,
            kMutedText, DT_RIGHT);
    }
    else if (page_ == 1)
    {
        for (const auto& card : {logicalRect(mainX, 132, mainWidth, 100),
            logicalRect(mainX, 248, mainWidth, 100), logicalRect(mainX, 364, mainWidth, 132),
            logicalRect(mainX, 512, mainWidth, 120)})
            FillRoundedRect(memory, card, kCard, kCardBorder, Scale(window_, 12));
        DrawTextLine(memory, T(L"界面语言", L"Interface language"),
            logicalRect(mainX + 24, 132, mainWidth - 300, 100), headingFont_, kText);
        DrawTextLine(memory, T(L"随 Windows 启动", L"Start with Windows"),
            logicalRect(mainX + 24, 248, mainWidth - 164, 100), headingFont_, kText);
        DrawTextLine(memory, T(L"全局调出快捷键", L"Global summon shortcut"),
            logicalRect(mainX + 24, 371, mainWidth - 164, 46), headingFont_, kText);
        DrawTextLine(memory, shortcutEnabled_ ? T(L"已启用", L"Enabled") :
            T(L"已停用", L"Disabled"),
            logicalRect(mainX + 24, 407, mainWidth - 164, 26), smallFont_,
            shortcutEnabled_ ? kAccent : kMutedText);
        DrawTextLine(memory, T(L"识别后返回后台", L"Return to background after detection"),
            logicalRect(mainX + 24, 512, mainWidth - 164, 70), headingFont_, kText);
        DrawTextLine(memory,
            T(L"将本应用调到前台供 EuControl 识别，识别完成后自动返回后台。",
                L"Brings this application forward for EuControl recognition, then returns it to the background."),
            logicalRect(mainX + 24, 572, mainWidth - 164, 42), smallFont_, kMutedText);
    }
    else
    {
        auto aboutCard = logicalRect(mainX, 132, mainWidth, 360);
        FillRoundedRect(memory, aboutCard, kCard, kCardBorder, Scale(window_, 12));
        DrawIconEx(memory, Scale(window_, mainX + 24), Scale(window_, 158), largeIcon_,
            Scale(window_, 64), Scale(window_, 64), 0, nullptr, DI_NORMAL);
        DrawTextLine(memory, L"Windows Fader Bridge", logicalRect(mainX + 112, 158,
            mainWidth - 136, 34), titleFont_, kText);
        DrawTextLine(memory, L"for EUCON", logicalRect(mainX + 112, 200,
            mainWidth - 136, 26), bodyFont_, kAccent);
        DrawTextLine(memory, L"Windows Core Audio  ↔  EUCON",
            logicalRect(mainX + 24, 274, mainWidth - 48, 30), headingFont_, kText);
        DrawTextLine(memory,
            T(L"将 Windows 音频混音器发布为符合标准的 EUCON 应用模型。",
                L"Publishes the Windows audio mixer as a standards-based EUCON application model."),
            logicalRect(mainX + 24, 310, mainWidth - 48, 28), bodyFont_, kMutedText);
        DrawTextLine(memory, T(L"界面发现、分配、翻页与布局仍由 EuControl 管理。",
            L"Surface discovery, assignment, banking and layouts remain under EuControl."),
            logicalRect(mainX + 24, 341, mainWidth - 48, 28), bodyFont_, kMutedText);
        const auto footerPen = CreatePen(PS_SOLID, 1, kCardBorder);
        const auto oldFooterPen = SelectObject(memory, footerPen);
        MoveToEx(memory, Scale(window_, mainX + 24), Scale(window_, 414), nullptr);
        LineTo(memory, Scale(window_, mainX + mainWidth - 24), Scale(window_, 414));
        SelectObject(memory, oldFooterPen);
        DeleteObject(footerPen);
        DrawTextLine(memory, L"Lindelea", logicalRect(mainX + 24, 426, 64, 32),
            smallFont_, kMutedText);
        DrawTextLine(memory, L"·", logicalRect(mainX + 90, 426, 8, 32),
            smallFont_, kMutedText, DT_CENTER);
        DrawTextLine(memory, L"·", logicalRect(mainX + 179, 426, 8, 32),
            smallFont_, kMutedText, DT_CENTER);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(window_, &paint);
}

void ApplicationShell::DrawStartupControl(const DRAWITEMSTRUCT& item) const
{
    DrawCheckboxControl(item, T(L"随 Windows 启动", L"Start with Windows"), startupEnabled_);
}

void ApplicationShell::DrawCheckboxControl(const DRAWITEMSTRUCT& item, const wchar_t* caption,
    const bool checked) const
{
    (void)caption;
    const auto dc = item.hDC;
    SetDCBrushColor(dc, kCard);
    FillRect(dc, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const auto trackWidth = Scale(window_, 52);
    const auto trackHeight = Scale(window_, 26);
    RECT track{item.rcItem.left + (item.rcItem.right - item.rcItem.left - trackWidth) / 2,
        item.rcItem.top + (item.rcItem.bottom - item.rcItem.top - trackHeight) / 2,
        0, 0};
    track.right = track.left + trackWidth;
    track.bottom = track.top + trackHeight;
    const auto fill = CreateSolidBrush(checked ? kAccent : RGB(55, 62, 73));
    const auto pen = CreatePen(PS_SOLID, 1, checked ? kAccent : RGB(55, 62, 73));
    const auto oldBrush = SelectObject(dc, fill);
    const auto oldPen = SelectObject(dc, pen);
    RoundRect(dc, track.left, track.top, track.right, track.bottom, trackHeight, trackHeight);
    const int knob = Scale(window_, 18);
    const int knobLeft = checked ? track.right - Scale(window_, 4) - knob :
        track.left + Scale(window_, 4);
    const int knobTop = track.top + (trackHeight - knob) / 2;
    const auto knobBrush = CreateSolidBrush(checked ? RGB(26, 22, 35) : RGB(235, 238, 243));
    SelectObject(dc, knobBrush);
    SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, knobLeft, knobTop, knobLeft + knob, knobTop + knob);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(knobBrush);
    DeleteObject(pen);
    DeleteObject(fill);
    if ((item.itemState & ODS_FOCUS) != 0)
    {
        RECT focus = item.rcItem;
        InflateRect(&focus, -Scale(window_, 2), -Scale(window_, 2));
        DrawFocusRect(dc, &focus);
    }
}

void ApplicationShell::DrawButtonControl(const DRAWITEMSTRUCT& item) const
{
    const bool navigation = item.CtlID >= kNavigationBaseId && item.CtlID < kNavigationBaseId + 3;
    const bool link = item.CtlID == kLicenseLinkId || item.CtlID == kVersionLinkId ||
        item.CtlID == kIssuesLinkId;
    const bool active = navigation && static_cast<int>(item.CtlID) - kNavigationBaseId == page_;
    const bool sidebar = navigation || item.CtlID == kHideButtonId;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const auto base = CreateSolidBrush(link ? kCard : sidebar ? kSidebar : kBackground);
    FillRect(item.hDC, &item.rcItem, base);
    DeleteObject(base);
    COLORREF fill = active ? kSelection : sidebar && navigation ? kSidebar : kCard;
    if ((item.itemState & ODS_SELECTED) != 0) fill = kCardBorder;
    if (!link && (active || !navigation))
        FillRoundedRect(item.hDC, item.rcItem, fill, active ? fill : kCardBorder,
            Scale(window_, 10));
    auto text = WindowText(item.hwndItem);
    RECT textRect = item.rcItem;
    if (navigation) textRect.left += Scale(window_, 16);
    const auto alignment = item.CtlID == kIssuesLinkId ? DT_RIGHT :
        navigation || link ? DT_LEFT : DT_CENTER;
    DrawTextLine(item.hDC, text, textRect, link ? smallFont_ : bodyFont_,
        disabled ? kMutedText : active || link ? kAccent : kText,
        alignment);
    if ((item.itemState & ODS_FOCUS) != 0)
    {
        RECT focus = item.rcItem;
        InflateRect(&focus, -Scale(window_, 4), -Scale(window_, 4));
        DrawFocusRect(item.hDC, &focus);
    }
}

void ApplicationShell::DrawLanguageControl(const DRAWITEMSTRUCT& item) const
{
    const auto fill = CreateSolidBrush((item.itemState & ODS_SELECTED) != 0 ?
        kSelection : kCard);
    FillRect(item.hDC, &item.rcItem, fill);
    DeleteObject(fill);

    int index = item.itemID == static_cast<UINT>(-1) ?
        static_cast<int>(SendMessageW(item.hwndItem, CB_GETCURSEL, 0, 0)) :
        static_cast<int>(item.itemID);
    std::wstring label;
    if (index >= 0)
    {
        const auto length = static_cast<int>(SendMessageW(item.hwndItem,
            CB_GETLBTEXTLEN, index, 0));
        if (length >= 0 && length < 32768)
        {
            label.resize(static_cast<size_t>(length) + 1U);
            SendMessageW(item.hwndItem, CB_GETLBTEXT, index,
                reinterpret_cast<LPARAM>(label.data()));
            label.resize(static_cast<size_t>(length));
        }
    }
    auto textRect = item.rcItem;
    textRect.left += Scale(window_, 10);
    textRect.right -= Scale(window_, 20);
    DrawTextLine(item.hDC, label, textRect, bodyFont_,
        (item.itemState & ODS_DISABLED) != 0 ? kMutedText : kText);
    if ((item.itemState & ODS_FOCUS) != 0)
    {
        auto focus = item.rcItem;
        InflateRect(&focus, -Scale(window_, 3), -Scale(window_, 3));
        DrawFocusRect(item.hDC, &focus);
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
    KillTimer(window_, kReturnToBackgroundTimerId);
    startupEnabled_ = IsStartupEnabled();
    InvalidateRect(startupCheck_, nullptr, TRUE);
    const auto activated = bridge::ActivateTopLevelWindow(window_);
    FB_TRACE("WINDOW_ACTIVATE requested=1 foreground=%d", activated ? 1 : 0);
}

void ApplicationShell::SummonForEucon()
{
    ShowMainWindow();
    // Always settle after EuControl has observed the foreground change. The
    // timer enforces either outcome, including keeping the window open when
    // "Return to background after detection" is disabled.
    SetTimer(window_, kReturnToBackgroundTimerId, 400U, nullptr);
    FB_TRACE("EUCON_SUMMON_REQUEST returnToBackground=%d",
        shortcutReturnToBackground_ ? 1 : 0);
}

bool ApplicationShell::ApplyShortcut(const bool enabled, const bridge::Shortcut& shortcut,
    const bool showError)
{
    if (shortcutRegistration_.Apply(window_, kGlobalShortcutId, enabled, shortcut)) return true;
    if (showError)
        MessageBoxW(window_,
            T(L"该快捷键不可用或已被占用，已保留原快捷键。",
                L"This shortcut is unavailable or already in use. The previous shortcut remains active."),
            kProductName, MB_OK | MB_ICONWARNING);
    return false;
}

void ApplicationShell::LoadShortcutSettings()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kShortcutSettingsKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) return;
    shortcutEnabled_ = ReadShortcutValue(key, L"Enabled", 1) != 0;
    shortcutReturnToBackground_ = ReadShortcutValue(key, L"ReturnToBackground", 1) != 0;
    chinese_ = ReadShortcutValue(key, L"Language", 1) != 0;
    bridge::Shortcut saved{ReadShortcutValue(key, L"Modifiers", shortcut_.modifiers),
        ReadShortcutValue(key, L"VirtualKey", shortcut_.key)};
    if (bridge::ValidShortcut(saved)) shortcut_ = saved;
    RegCloseKey(key);
}

bool ApplicationShell::SaveShortcutSettings() const
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kShortcutSettingsKey, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const DWORD enabled = shortcutEnabled_ ? 1U : 0U;
    const DWORD background = shortcutReturnToBackground_ ? 1U : 0U;
    const DWORD language = chinese_ ? 1U : 0U;
    const DWORD modifiers = shortcut_.modifiers;
    const DWORD virtualKey = shortcut_.key;
    bool okay = true;
    for (const auto& value : {std::pair<const wchar_t*, const DWORD*>{L"Enabled", &enabled},
        {L"ReturnToBackground", &background}, {L"Language", &language},
        {L"Modifiers", &modifiers},
        {L"VirtualKey", &virtualKey}})
        okay = RegSetValueExW(key, value.first, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(value.second), sizeof(DWORD)) == ERROR_SUCCESS && okay;
    RegCloseKey(key);
    return okay;
}

const wchar_t* ApplicationShell::T(const wchar_t* simplifiedChinese,
    const wchar_t* english) const
{
    return chinese_ ? simplifiedChinese : english;
}

void ApplicationShell::UpdateLanguage()
{
    constexpr const wchar_t* navigationZh[]{L"通道总览", L"通用", L"关于"};
    constexpr const wchar_t* navigationEn[]{L"Overview", L"General", L"About"};
    for (int index = 0; index < 3; ++index)
        SetWindowTextW(navigationButtons_[static_cast<size_t>(index)],
            T(navigationZh[index], navigationEn[index]));
    SetWindowTextW(hideButton_, T(L"后台运行", L"Hide to tray"));
    SetWindowTextW(previousPageButton_, T(L"上一组", L"Previous bank"));
    SetWindowTextW(nextPageButton_, T(L"下一组", L"Next bank"));
    SetWindowTextW(startupCheck_, T(L"随 Windows 启动", L"Start with Windows"));
    SetWindowTextW(shortcutEnabledCheck_,
        T(L"启用全局调出快捷键", L"Enable global summon shortcut"));
    SetWindowTextW(shortcutBackgroundCheck_,
        T(L"识别后返回后台", L"Return to background after detection"));
    SetWindowTextW(shortcutButton_, bridge::ShortcutText(shortcut_).c_str());
    SetWindowTextW(licenseLink_, L"MPL 2.0");
    SetWindowTextW(versionLink_, kProductVersion);
    SetWindowTextW(issuesLink_, T(L"报告错误", L"Report an issue"));
    SendMessageW(languageCombo_, CB_SETCURSEL, chinese_ ? 0 : 1, 0);
    for (const auto control : navigationButtons_) InvalidateRect(control, nullptr, TRUE);
    for (const auto control : {startupCheck_, hideButton_, previousPageButton_,
        nextPageButton_, shortcutEnabledCheck_, shortcutBackgroundCheck_, shortcutButton_,
        languageCombo_, licenseLink_, versionLink_, issuesLink_})
        InvalidateRect(control, nullptr, TRUE);
    UpdateTrayTooltip();
    InvalidateRect(window_, nullptr, FALSE);
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
        std::to_wstring(activeCount_) + T(L" 个活动通道", L" active channels");
    wcsncpy_s(data.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void ApplicationShell::ShowTrayMenu(const POINT location)
{
    startupEnabled_ = IsStartupEnabled();
    InvalidateRect(startupCheck_, nullptr, TRUE);
    const auto menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, kTrayOpenId,
        T(L"打开 Windows Fader Bridge for EUCON", L"Open Windows Fader Bridge for EUCON"));
    const auto ready = host_ && host_->IsReady();
    const auto status = std::wstring(ready ? T(L"桥接已运行 · ", L"Bridge is active · ") :
        T(L"EUCON 初始化失败 · ", L"EUCON initialization failed · ")) +
        std::to_wstring(activeCount_) + (chinese_ ? L" 个通道" :
        activeCount_ == 1 ? L" channel" : L" channels");
    AppendMenuW(menu, MF_OWNERDRAW | MF_DISABLED, kTrayStatusId,
        reinterpret_cast<LPCWSTR>(status.c_str()));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (startupEnabled_ ? MF_CHECKED : 0),
        kTrayStartupId, T(L"随 Windows 启动", L"Start with Windows"));
    AppendMenuW(menu, MF_STRING, kTrayRestartId, T(L"重新启动", L"Restart"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayDiagnosticsId,
        T(L"打开诊断目录", L"Open diagnostics folder"));
    AppendMenuW(menu, MF_STRING, kTrayAboutId,
        T(L"关于 Windows Fader Bridge for EUCON...",
            L"About Windows Fader Bridge for EUCON..."));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitId, T(L"退出", L"Exit"));
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        location.x, location.y, 0, window_, nullptr);
    DestroyMenu(menu);
}

void ApplicationShell::ShowAboutDialog() const
{
    const auto message = std::wstring(kProductName) + L" " + kProductVersion +
        T(L"\n\n连接 Windows Core Audio 与 Avid EUCON 的原生设备无关桥接器。"
            L"\n\nEUCON 控制器发现、分配、翻页和硬件通信由 EuControl 或 WSControl 管理。",
            L"\n\nA native, device-independent bridge between Windows Core Audio and Avid EUCON."
            L"\n\nEUCON surface discovery, assignment, banking, and hardware communication "
            L"are managed by EuControl or WSControl.") +
        L"\n\nCopyright 2026 Lindelea";
    MessageBoxW(window_, message.c_str(),
        T(L"关于 Windows Fader Bridge for EUCON", L"About Windows Fader Bridge for EUCON"),
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
        MessageBoxW(window_, T(L"Windows 无法重新启动应用。",
            L"Windows could not restart the application."),
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
