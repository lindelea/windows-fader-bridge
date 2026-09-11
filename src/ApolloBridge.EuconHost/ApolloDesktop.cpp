#include "ApolloDesktop.h"
#include "ApolloProductVersion.h"
#include "DesktopSettings.h"
#include <algorithm>
#include <cmath>
#include <dwmapi.h>
#include <gdiplus.h>
#include <iomanip>
#include <set>
#include <shellapi.h>
#include <sstream>
#include <uxtheme.h>
#include <windowsx.h>

namespace apollo
{
namespace
{
// Compose each native child in one copy, including focus/hover transitions.
class ControlFrame
{
    HDC target_, memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    RECT rect_;
  public:
    ControlFrame(HDC target, RECT rect) : target_(target), rect_(rect)
    {
        if (rect.right <= rect.left || rect.bottom <= rect.top) return;
        memory_ = CreateCompatibleDC(target);
        bitmap_ = CreateCompatibleBitmap(target, rect.right - rect.left, rect.bottom - rect.top);
        if (memory_ && bitmap_)
        {
            old_ = SelectObject(memory_, bitmap_);
            SetViewportOrgEx(memory_, -rect.left, -rect.top, nullptr);
        }
    }
    HDC Dc() const { return old_ ? memory_ : target_; }
    void Present()
    {
        if (old_) BitBlt(target_, rect_.left, rect_.top, rect_.right - rect_.left,
                        rect_.bottom - rect_.top, memory_, rect_.left, rect_.top, SRCCOPY);
    }
    ~ControlFrame()
    {
        if (old_) SelectObject(memory_, old_);
        if (bitmap_) DeleteObject(bitmap_);
        if (memory_) DeleteDC(memory_);
    }
};
constexpr wchar_t DesktopClass[] = L"Lindelea.UadConsoleBridge.EUCON.Desktop";
constexpr COLORREF Bg = RGB(17, 20, 24), Surface = RGB(25, 29, 34), Edge = RGB(47, 53, 61);
constexpr COLORREF Ink = RGB(235, 237, 240), Secondary = RGB(159, 169, 180), Dim = RGB(111, 122, 135);
constexpr COLORREF Gold = RGB(224, 177, 104), Green = RGB(100, 201, 165), Red = RGB(237, 138, 130);
constexpr COLORREF Selected = RGB(46, 43, 37);
constexpr UINT TrayMessage = WM_APP + 42;
enum Id
{
    Overview = 200,
    General,
    Connection,
    Access,
    Protection,
    Settings = 210,
    HideWindow,
    SaveSettings,
    Discard,
    LockAll,
    OpenLogs,
    Connect = 220,
    Disconnect,
    Restart,
    Exit,
    Restore,
    Language = 240,
    Background,
    Minimized,
    Startup,
    AutoConnect,
    ConfigExtension,
    Profile,
    ChannelAccess,
    MonitorAccess,
    SensitiveAccess,
    ConfigAccess,
    RestoreAccess,
    Ceiling,
    FocusShortcut,
    FocusShortcutRecord,
    FocusReturnToBackground,
    ChannelList = 270
};
std::wstring W(const std::string &s)
{
    if (s.empty())
        return {};
    const int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n)
        return L"—";
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::wstring Number(double v, int precision = 1)
{
    if (!std::isfinite(v))
        return L"—";
    std::wostringstream s;
    s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(precision) << v;
    return s.str();
}
bool Checked(HWND w, int id)
{
    return SendDlgItemMessageW(w, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
int Choice(HWND w, int id)
{
    return static_cast<int>(SendDlgItemMessageW(w, id, CB_GETCURSEL, 0, 0));
}
int ProfileIndex(const AccessPolicy &p)
{
    if (!p.Any())
        return 0;
    if (p == AccessPolicy{true, true, false, false})
        return 1;
    if (p == AccessPolicy{true, true, true, true})
        return 2;
    return 3;
}
void EnableChanged(HWND w, bool enabled)
{
    if (w && (IsWindowEnabled(w) != FALSE) != enabled)
        EnableWindow(w, enabled);
}
void DrawRound(HDC dc, RECT r, COLORREF fill, COLORREF edge, int radius)
{
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float x = static_cast<float>(r.left) + .5f, y = static_cast<float>(r.top) + .5f;
    const float width = static_cast<float>(r.right - r.left) - 1,
                height = static_cast<float>(r.bottom - r.top) - 1;
    const float d = std::min(static_cast<float>(radius), std::min(width, height));
    if (d <= 0)
        return;
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + width - d, y, d, d, 270, 90);
    path.AddArc(x + width - d, y + height - d, d, d, 0, 90);
    path.AddArc(x, y + height - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush b(Gdiplus::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    Gdiplus::Pen p(Gdiplus::Color(255, GetRValue(edge), GetGValue(edge), GetBValue(edge)));
    g.FillPath(&b, &path);
    g.DrawPath(&p, &path);
}
} // namespace
ApolloDesktop::ApolloDesktop(Preferences p, bool startup, DesktopActions actions)
    : saved_(p), draft_(std::move(p)), savedStartup_(startup), draftStartup_(startup),
      actions_(std::move(actions))
{
    fieldBrush_ = CreateSolidBrush(Surface);
}
ApolloDesktop::~ApolloDesktop()
{
    if (window_ && IsWindow(window_))
        DestroyWindow(window_);
    for (auto f : fonts_)
        if (f)
            DeleteObject(f);
    if (icon_)
        DestroyIcon(icon_);
    DeleteObject(fieldBrush_);
    if (graphicsToken_)
        Gdiplus::GdiplusShutdown(graphicsToken_);
}
const wchar_t *ApolloDesktop::T(const wchar_t *zh, const wchar_t *en) const
{
    return draft_.language == "zh-CN" ? zh : en;
}
bool ApolloDesktop::Create(HINSTANCE instance, bool preview)
{
    instance_ = instance;
    Gdiplus::GdiplusStartupInput graphics;
    if (Gdiplus::GdiplusStartup(&graphicsToken_, &graphics, nullptr) != Gdiplus::Ok)
        return false;
    state_.preview = preview;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    icon_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, 0));
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = instance;
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = DesktopClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon_;
    wc.hIconSm = icon_;
    RegisterClassExW(&wc);
    dpi_ = static_cast<int>(GetDpiForSystem());
    RECT r{0, 0, S(1080), S(820)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
    window_ = CreateWindowExW(0, DesktopClass,
                              preview ? L"UAD Console Bridge for EUCON — UI Preview"
                                      : L"UAD Console Bridge for EUCON",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, this);
    if (!window_)
        return false;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTray();
    return true;
}
void ApolloDesktop::Fonts()
{
    const int sizes[] = {14, 12, 18, 27, 52, 20};
    for (int i = 0; i < 6; ++i)
    {
        if (fonts_[i])
            DeleteObject(fonts_[i]);
        const auto face = i == 4                       ? L"Bahnschrift"
                          : i == 5                     ? L"Segoe Fluent Icons"
                          : draft_.language == "zh-CN" ? L"Microsoft YaHei UI"
                                                       : L"Segoe UI";
        fonts_[i] = CreateFontW(-S(sizes[i]), 0, 0, 0, i == 2 || i == 3 ? FW_SEMIBOLD : FW_NORMAL, FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    }
    for (auto child : children_)
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_[0]), TRUE);
}
HWND ApolloDesktop::Add(const wchar_t *type, int id, const std::wstring &name, DWORD style)
{
    auto child = CreateWindowExW(0, type, name.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window_,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    children_.push_back(child);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_[0]), FALSE);
    return child;
}
HWND ApolloDesktop::Button(int id, const std::wstring &name)
{
    auto button = Add(L"BUTTON", id, name, BS_OWNERDRAW | WS_TABSTOP);
    SetWindowSubclass(button, ButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
    return button;
}
HWND ApolloDesktop::Toggle(int id, const std::wstring &name, bool checked)
{
    auto child = Add(L"BUTTON", id, name, BS_AUTOCHECKBOX | WS_TABSTOP);
    SetWindowTheme(child, L"", L"");
    SetWindowSubclass(child, ToggleProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(child, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return child;
}
HWND ApolloDesktop::Combo(int id, const std::vector<std::wstring> &choices, int selected)
{
    auto child = Add(WC_COMBOBOXW, id, L"",
                     CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP);
    SetWindowTheme(child, L"", L"");
    for (const auto &choice : choices)
        SendMessageW(child, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.c_str()));
    SendMessageW(child, CB_SETCURSEL, selected, 0);
    SendMessageW(child, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), S(36));
    SendMessageW(child, CB_SETITEMHEIGHT, 0, S(36));
    SetWindowSubclass(child, ComboProc, 1, reinterpret_cast<DWORD_PTR>(this));
    COMBOBOXINFO info{sizeof(info)};
    if (GetComboBoxInfo(child, &info))
        SetWindowTheme(info.hwndList, L"DarkMode_Explorer", nullptr);
    return child;
}
void ApolloDesktop::Build()
{
    const bool visible = IsWindowVisible(window_) != FALSE;
    if (visible) SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    building_ = true;
    for (auto child : children_)
        DestroyWindow(child);
    children_.clear();
    Button(HideWindow, T(L"后台运行", L"Hide to tray"));
    if (!page_)
    {
        Button(Settings, T(L"设置", L"Settings"));
        const auto list = Add(WC_LISTVIEWW, ChannelList, T(L"通道状态，只读", L"Channel status, read only"),
                              LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_OWNERDRAWFIXED | LVS_OWNERDATA |
                                  LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP);
        SetWindowTheme(list, L"DarkMode_Explorer", nullptr);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(list, Surface);
        ListView_SetTextBkColor(list, Surface);
        ListView_SetTextColor(list, Ink);
        LVCOLUMNW column{};
        column.mask = LVCF_WIDTH;
        column.cx = S(900);
        ListView_InsertColumn(list, 0, &column);
        RefreshChannels();
    }
    else
    {
        Button(Overview, T(L"返回状态", L"Overview"));
        Button(General, T(L"通用", L"General"));
        Button(Connection, T(L"连接", L"Connection"));
        Button(Access, T(L"控制", L"Control"));
        Button(Protection, T(L"安全限制", L"Protection"));
        Button(SaveSettings, T(L"保存设置", L"Save settings"));
        Button(Discard, T(L"撤销更改", L"Discard changes"));
    }
    if (page_ == 1)
    {
        Combo(Language, {L"简体中文", L"English"}, draft_.language == "zh-CN" ? 0 : 1);
        Toggle(Background, T(L"关闭窗口后继续运行", L"Keep running when the window closes"),
               draft_.background);
        Toggle(Minimized, T(L"启动时隐藏主窗口", L"Start with the window hidden"), draft_.startMinimized);
        Toggle(Startup, T(L"登录 Windows 时启动", L"Launch at Windows sign-in"), draftStartup_);
        Toggle(FocusShortcut, T(L"启用全局调出快捷键", L"Enable global summon shortcut"),
               draft_.focusShortcutEnabled);
        Button(FocusShortcutRecord, bridge::ShortcutText(
            {draft_.focusShortcutModifiers, draft_.focusShortcutKey}));
        Toggle(FocusReturnToBackground,
               T(L"识别后返回后台", L"Return to background after detection"),
               draft_.focusReturnToBackground);
        Button(OpenLogs, T(L"打开日志文件夹", L"Open log folder"));
    }
    if (page_ == 2)
    {
        Toggle(AutoConnect, T(L"自动连接 EUCON", L"Connect EUCON automatically"), draft_.autoConnect);
        Toggle(ConfigExtension, T(L"启用 CONFIG", L"Enable CONFIG"), draft_.configExtension);
        Button(Connect, T(L"连接 EUCON", L"Connect EUCON"));
        Button(Disconnect, T(L"暂停控制", L"Suspend control"));
    }
    if (page_ == 3)
    {
        Combo(Profile,
              {T(L"只读", L"Read only"), T(L"混音控制", L"Mixing"), T(L"完整控制", L"Full control"),
               T(L"自定义", L"Custom")},
              ProfileIndex(draft_.access));
        Toggle(ChannelAccess, T(L"通道控制", L"Channel control"), draft_.access.channels);
        Toggle(MonitorAccess, T(L"控制室", L"Control room"), draft_.access.monitor);
        Toggle(SensitiveAccess, T(L"敏感操作", L"Sensitive controls"), draft_.access.sensitive);
        Toggle(ConfigAccess, T(L"声卡与插件配置", L"Interface and plug-in configuration"),
               draft_.access.configuration);
        EnableChanged(GetDlgItem(window_, SensitiveAccess), draft_.access.channels);
    }
    if (page_ == 4)
    {
        auto edit = Add(L"EDIT", Ceiling, Number(draft_.monitorCeiling), WS_TABSTOP | ES_AUTOHSCROLL);
        SetWindowSubclass(edit, EditProc, 1, 0);
        SendMessageW(edit, EM_SETLIMITTEXT, 7, 0);
        Toggle(RestoreAccess, T(L"启动时恢复控制", L"Restore control at startup"),
               draft_.restorePermissions);
        Button(LockAll, T(L"暂停全部控制", L"Suspend all control"));
    }
    building_ = false;
    Layout();
    if (visible) SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
void ApolloDesktop::Place(int id, int x, int y, int w, int h)
{
    const auto child = GetDlgItem(window_, id);
    if (child)
        SetWindowPos(child, nullptr, S(x), S(y), S(w), S(h), SWP_NOZORDER | SWP_NOACTIVATE);
}
void ApolloDesktop::Layout()
{
    RECT r{};
    GetClientRect(window_, &r);
    width_ = MulDiv(r.right, 96, dpi_);
    height_ = MulDiv(r.bottom, 96, dpi_);
    Place(HideWindow, width_ - 162, 30, 130, 36);
    Place(Settings, width_ - 278, 30, 100, 36);
    if (!page_)
    {
        Place(ChannelList, 44, 381, width_ - 88, std::max(44, height_ - 445));
        if (auto list = GetDlgItem(window_, ChannelList))
        {
            RECT client{};
            GetClientRect(list, &client);
            ListView_SetColumnWidth(list, 0, std::max(1L, client.right - 1));
        }
        return;
    }
    Place(Overview, 24, 136, 170, 42);
    for (int i = 1; i <= 4; ++i)
        Place(Overview + i, 24, 210 + (i - 1) * 48, 170, 42);
    Place(SaveSettings, width_ - 218, height_ - 82, 186, 40);
    Place(Discard, width_ - 390, height_ - 82, 156, 40);
    const int right = width_ - 60;
    if (page_ == 1)
    {
        Place(Language, right - 236, 198, 212, 230);
        Place(Background, right - 74, 274, 50, 28);
        Place(Minimized, right - 74, 346, 50, 28);
        Place(Startup, right - 74, 418, 50, 28);
        Place(FocusShortcut, right - 74, 514, 50, 28);
        Place(FocusShortcutRecord, right - 254, 552, 230, 38);
        Place(FocusReturnToBackground, right - 74, 606, 50, 28);
        Place(OpenLogs, 278, 700, 200, 34);
    }
    if (page_ == 2)
    {
        Place(Connect, 278, 258, 178, 38);
        Place(Disconnect, 468, 258, 178, 38);
        Place(AutoConnect, right - 74, 350, 50, 28);
        Place(ConfigExtension, right - 74, 446, 50, 28);
        EnableChanged(GetDlgItem(window_, Connect),
                      !state_.published && !state_.conflict && state_.snapshot.connected && !state_.preview);
        EnableChanged(GetDlgItem(window_, Disconnect), state_.published && !state_.preview);
    }
    if (page_ == 3)
    {
        Place(Profile, right - 236, 192, 212, 230);
        Place(ChannelAccess, right - 74, 273, 50, 28);
        Place(MonitorAccess, right - 74, 353, 50, 28);
        Place(SensitiveAccess, right - 74, 433, 50, 28);
        Place(ConfigAccess, right - 74, 513, 50, 28);
    }
    if (page_ == 4)
    {
        Place(Ceiling, right - 159, 218, 88, 29);
        Place(RestoreAccess, right - 74, 340, 50, 28);
        Place(LockAll, 278, 539, 268, 38);
    }
}
void ApolloDesktop::Show(bool settings)
{
    if (settings && !page_)
        Navigate(1);
    bridge::ActivateTopLevelWindow(window_);
}
void ApolloDesktop::Hide()
{
    if (!trayAdded_)
        AddTray();
    if (trayAdded_)
        ShowWindow(window_, SW_HIDE);
    else
    {
        notice_ =
            T(L"系统托盘不可用，窗口将保持打开。", L"System tray unavailable. The window will remain open.");
        InvalidateRect(window_, nullptr, FALSE);
    }
}
void ApolloDesktop::Update(DesktopState state)
{
    const bool fresh = state.preview || std::chrono::steady_clock::now() - state.snapshot.receivedAt <
                                             std::chrono::seconds(2);
    const bool chromeChanged = !state.snapshot.controlRevision ||
        state.snapshot.controlRevision != state_.snapshot.controlRevision ||
        state.snapshot.generation != state_.snapshot.generation ||
        state.snapshot.metadataRevision != state_.snapshot.metadataRevision ||
        state.snapshot.connected != state_.snapshot.connected ||
        state.published != state_.published || state.conflict != state_.conflict ||
        state.preview != state_.preview || state.configExtension != state_.configExtension ||
        state.monitorEnabled != state_.monitorEnabled || state.activeCeiling != state_.activeCeiling ||
        state.notice != state_.notice || fresh != displayFresh_;
    displayFresh_ = fresh;
    state_ = std::move(state);
    // Invalidation coalesces naturally in the window queue; never force painting
    // inside the observation/control callback or introduce a frame timer.
    if (page_ == 2)
    {
        EnableChanged(GetDlgItem(window_, Connect),
                      !state_.published && !state_.conflict && state_.snapshot.connected && !state_.preview);
        EnableChanged(GetDlgItem(window_, Disconnect), state_.published && !state_.preview);
    }
    if (IsWindowVisible(window_) && !IsIconic(window_))
    {
        if (!page_)
            RefreshChannels();
        if (chromeChanged)
            InvalidateRect(window_, nullptr, FALSE);
    }
}
void ApolloDesktop::Navigate(int page)
{
    if (!page && dirty_)
    {
        const int answer = MessageBoxW(window_,
                                       T(L"设置尚未保存。放弃这些更改并返回？",
                                         L"Settings have not been saved. Discard changes and return?"),
                                       T(L"未保存的更改", L"Unsaved changes"),
                                       MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer != IDOK)
            return;
        draft_ = saved_;
        draftStartup_ = savedStartup_;
        dirty_ = false;
        Fonts();
    }
    page_ = page;
    notice_.clear();
    Build();
}
void ApolloDesktop::RefreshChannels()
{
    const auto list = GetDlgItem(window_, ChannelList);
    if (!list)
        return;
    const bool fresh = state_.preview || std::chrono::steady_clock::now() - state_.snapshot.receivedAt <
                                             std::chrono::seconds(2);
    auto next = DesktopChannels(state_.snapshot, fresh);
    const auto selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const auto selectedKey = selected >= 0 && static_cast<size_t>(selected) < channelRows_.size()
                                 ? channelRows_[selected].channel.key
                                 : std::string{};
    bool changed = next.size() != channelRows_.size() ||
                   ListView_GetItemCount(list) != static_cast<int>(next.size());
    if (!changed)
        for (size_t i = 0; i < next.size(); ++i)
            changed |= next[i].channel.key != channelRows_[i].channel.key;
    channelRows_ = std::move(next);
    if (changed)
    {
        ListView_SetItemCountEx(list, static_cast<int>(channelRows_.size()),
                                LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        if (!selectedKey.empty())
            for (size_t i = 0; i < channelRows_.size(); ++i)
                if (channelRows_[i].channel.key == selectedKey)
                    ListView_SetItemState(list, static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
        RECT client{};
        GetClientRect(list, &client);
        ListView_SetColumnWidth(list, 0, std::max(1L, client.right - 1));
    }
    InvalidateRect(list, nullptr, FALSE);
}
std::wstring ApolloDesktop::ChannelAccessibleText(size_t index) const
{
    if (index >= channelRows_.size())
        return {};
    const auto &row = channelRows_[index];
    const auto flag = [&](std::optional<bool> value) {
        return !value ? L"—" : *value ? T(L"开启", L"On") : T(L"关闭", L"Off");
    };
    return std::to_wstring(index + 1) + L". " + W(row.channel.name) + L", " + W(row.channel.deviceName) +
           L", " + row.format + T(L", 电平 ", L", Level ") + row.level + L" dB" + T(L", 声像 ", L", Pan ") +
           row.pan + T(L", 信号 ", L", Signal ") + row.signal + L" dBFS" + T(L", 峰值 ", L", Peak ") +
           row.peak + L" dBFS, Mute " + flag(row.mute) + L", Solo " + flag(row.solo) + L", UAD " +
           row.record + T(L", 输出 ", L", Output ") + W(row.channel.destination);
}
void ApolloDesktop::DrawChannel(DRAWITEMSTRUCT *d)
{
    if (d->itemID >= channelRows_.size())
        return;
    const auto &row = channelRows_[d->itemID];
    const auto selected = (d->itemState & ODS_SELECTED) != 0;
    auto background = CreateSolidBrush(selected        ? RGB(37, 44, 52)
                                       : d->itemID % 2 ? RGB(28, 32, 38)
                                                       : Surface);
    FillRect(d->hDC, &d->rcItem, background);
    DeleteObject(background);
    const int top = MulDiv(d->rcItem.top, 96, dpi_), width = MulDiv(d->rcItem.right, 96, dpi_);
    const auto widths = DesktopChannelWidths(width);
    const auto color = ChannelColor(row.channel);
    RECT mark{S(1), d->rcItem.top + S(8), S(4), d->rcItem.bottom - S(8)};
    auto accent = CreateSolidBrush(RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
    FillRect(d->hDC, &mark, accent);
    DeleteObject(accent);
    int x = 0;
    const auto cell = [&](const std::wstring &value, int column, int font = 0, COLORREF color = Ink) {
        Text(d->hDC, value, x + 4, top, widths[column] - 8, 44, font, color,
             (column == 1 || column == 9 ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE |
                 DT_END_ELLIPSIS);
        x += widths[column];
    };
    cell(std::to_wstring(d->itemID + 1), 0, 1, Secondary);
    Text(d->hDC, W(row.channel.name), x + 4, top + 2, widths[1] - 8, 23, 0, Ink);
    Text(d->hDC, W(row.channel.deviceName) + L" · " + W(row.channel.ioType), x + 4, top + 25, widths[1] - 8,
         17, 1, Secondary);
    x += widths[1];
    cell(row.format, 2, 1, Secondary);
    cell(row.level, 3);
    cell(row.pan, 4, 1, Secondary);
    Text(d->hDC, row.signal, x + 4, top + 1, widths[5] - 8, 29, 0, row.clipped ? Red : Green,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (row.signalDb && std::isfinite(*row.signalDb))
    {
        const int barWidth = widths[5] - 24;
        RECT track{S(x + 12), S(top + 33), S(x + 12 + barWidth), S(top + 36)};
        auto b = CreateSolidBrush(Edge);
        FillRect(d->hDC, &track, b);
        DeleteObject(b);
        track.right =
            track.left + S(static_cast<int>(barWidth * std::clamp((*row.signalDb + 60) / 60, 0.0, 1.0)));
        b = CreateSolidBrush(row.clipped ? Red : Green);
        FillRect(d->hDC, &track, b);
        DeleteObject(b);
    }
    x += widths[5];
    cell(row.peak, 6, 0, row.clipped ? Red : Secondary);
    for (int i = 0; i < 2; ++i)
    {
        const auto flag = i ? row.solo : row.mute;
        const auto label = !flag ? L"—" : i ? L"S" : L"M";
        Text(d->hDC, label, x + i * 38, top + 8, 36, 28, 1, flag.value_or(false) ? i ? Gold : Red : Dim,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    x += widths[7];
    cell(row.record, 8, 1, row.record == L"REC" ? Red : Secondary);
    cell(row.channel.destination.empty() ? L"—" : W(row.channel.destination), 9, 1, Secondary);
    if (d->itemState & ODS_FOCUS)
    {
        auto focus = d->rcItem;
        InflateRect(&focus, -1, -1);
        DrawFocusRect(d->hDC, &focus);
    }
}
void ApolloDesktop::SyncDraft()
{
    if (page_ == 1)
    {
        draft_.language = Choice(window_, Language) == 0 ? "zh-CN" : "en-US";
        draft_.background = Checked(window_, Background);
        draft_.startMinimized = Checked(window_, Minimized);
        draftStartup_ = Checked(window_, Startup);
        draft_.focusShortcutEnabled = Checked(window_, FocusShortcut);
        draft_.focusReturnToBackground = Checked(window_, FocusReturnToBackground);
    }
    if (page_ == 2)
    {
        draft_.autoConnect = Checked(window_, AutoConnect);
        draft_.configExtension = Checked(window_, ConfigExtension);
    }
    if (page_ == 3)
    {
        draft_.access = {Checked(window_, ChannelAccess), Checked(window_, MonitorAccess),
                         Checked(window_, SensitiveAccess), Checked(window_, ConfigAccess)};
        if (!draft_.access.channels)
            draft_.access.sensitive = false;
    }
    if (page_ == 4)
    {
        draft_.restorePermissions = Checked(window_, RestoreAccess);
        wchar_t value[32]{};
        GetDlgItemTextW(window_, Ceiling, value, 32);
        wchar_t *end = nullptr;
        const double db = std::wcstod(value, &end);
        draft_.monitorCeiling = end != value && !*end ? db : NAN;
    }
    dirty_ = true;
}
void ApolloDesktop::Save()
{
    SyncDraft();
    try
    {
        ValidatePreferences(draft_);
    }
    catch (...)
    {
        notice_ =
            T(L"请输入 −96 至 0 dB 之间的主监听音量上限。", L"Enter a monitor ceiling between −96 and 0 dB.");
        return;
    }
    const bool activate =
        page_ == 3 || draft_.access != saved_.access || draft_.monitorCeiling != saved_.monitorCeiling;
    if (state_.preview)
    {
        saved_ = draft_;
        savedStartup_ = draftStartup_;
        dirty_ = false;
        notice_ = T(L"预览已更新。未保存设置，未更改系统或设备。",
                    L"Preview updated. No settings, system or device changes were made.");
    }
    else
    {
        const auto result = actions_.save(draft_, draftStartup_, activate);
        if (!result.empty())
        {
            notice_ = result;
            return;
        }
        saved_ = draft_;
        savedStartup_ = draftStartup_;
        dirty_ = false;
        notice_ = draft_.configExtension != state_.configExtension
                      ? T(L"设置已保存。CONFIG 扩展将在下次启动时生效。",
                          L"Settings saved. The CONFIG extension change takes effect after restart.")
                      : T(L"设置已保存。", L"Settings saved.");
    }
    Build();
}
void ApolloDesktop::Act(int id, int code)
{
    if (building_)
        return;
    if (id >= Overview && id <= Protection)
    {
        Navigate(id - Overview);
        return;
    }
    switch (id)
    {
    case Settings:
        Navigate(1);
        break;
    case HideWindow:
        Hide();
        break;
    case SaveSettings:
        Save();
        break;
    case Discard:
        draft_ = saved_;
        draftStartup_ = savedStartup_;
        dirty_ = false;
        notice_.clear();
        Fonts();
        Build();
        break;
    case OpenLogs:
        OpenFolder();
        break;
    case FocusShortcutRecord: {
        bridge::Shortcut next{draft_.focusShortcutModifiers, draft_.focusShortcutKey};
        if (bridge::CaptureShortcut(window_, next, next, draft_.language == "zh-CN"))
        {
            draft_.focusShortcutModifiers = next.modifiers;
            draft_.focusShortcutKey = next.key;
            dirty_ = true;
            Build();
            SetFocus(GetDlgItem(window_, FocusShortcutRecord));
        }
        break;
    }
    case Connect:
        if (!state_.preview)
            actions_.connect();
        break;
    case Disconnect:
        if (!state_.preview)
            actions_.disconnect();
        break;
    case Restart:
        if (!state_.preview && actions_.restart)
            actions_.restart();
        break;
    case LockAll:
        if (!state_.preview)
            actions_.lock();
        draft_.access = {};
        draft_.restorePermissions = false;
        dirty_ = true;
        notice_ = T(L"控制已暂停。", L"Control suspended.");
        Build();
        break;
    case Profile:
        if (code == CBN_SELCHANGE)
        {
            const int p = Choice(window_, Profile);
            if (p == 0)
                draft_.access = {};
            if (p == 1)
                draft_.access = {true, true, false, false};
            if (p == 2)
            {
                draft_.access = {true, true, true, true};
                draft_.configExtension = true;
            }
            dirty_ = true;
            Build();
            SetFocus(GetDlgItem(window_, Profile));
        }
        break;
    default:
        if (code == BN_CLICKED || code == CBN_SELCHANGE || (id == Ceiling && code == EN_CHANGE))
        {
            SyncDraft();
            notice_.clear();
            if (id == Language)
            {
                Fonts();
                Build();
                SetFocus(GetDlgItem(window_, Language));
            }
            if (id == ChannelAccess)
            {
                SendDlgItemMessageW(window_, SensitiveAccess, BM_SETCHECK,
                                    draft_.access.sensitive ? BST_CHECKED : BST_UNCHECKED, 0);
                EnableChanged(GetDlgItem(window_, SensitiveAccess), draft_.access.channels);
            }
            if (id >= ChannelAccess && id <= ConfigAccess)
                SendDlgItemMessageW(window_, Profile, CB_SETCURSEL, ProfileIndex(draft_.access), 0);
        }
    }
    InvalidateRect(window_, nullptr, FALSE);
}
void ApolloDesktop::Panel(HDC dc, int x, int y, int w, int h, COLORREF fill, COLORREF edge)
{
    DrawRound(dc, {S(x), S(y), S(x + w), S(y + h)}, fill, edge, S(12));
}
void ApolloDesktop::Text(HDC dc, const std::wstring &value, int x, int y, int w, int h, int font,
                         COLORREF color, UINT flags)
{
    const auto old = SelectObject(dc, fonts_[font]);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT r{S(x), S(y), S(x + w), S(y + h)};
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &r,
              DT_NOPREFIX | (flags ? flags : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS));
    SelectObject(dc, old);
}
void ApolloDesktop::Paint(HDC dc)
{
    RECT client{};
    GetClientRect(window_, &client);
    auto brush = CreateSolidBrush(Bg);
    FillRect(dc, &client, brush);
    DeleteObject(brush);
    if (icon_)
        DrawIconEx(dc, S(31), S(26), icon_, S(50), S(50), 0, nullptr, DI_NORMAL);
    Text(dc, L"UAD Console Bridge", 97, 23, width_ - 410, 35, 3, Ink);
    Text(dc, L"for EUCON  ·  " BRIDGE_PRODUCT_VERSION_DISPLAY_W, 99, 62, 220, 20, 1, Gold);
    const auto line = [&](int x, int y, int w) {
        RECT r{S(x), S(y), S(x + w), S(y + 1)};
        auto b = CreateSolidBrush(Edge);
        FillRect(dc, &r, b);
        DeleteObject(b);
    };
    line(32, 98, width_ - 64);
    const bool stale =
        state_.snapshot.connected && !state_.preview &&
        std::chrono::steady_clock::now() - state_.snapshot.receivedAt >= std::chrono::seconds(2);
    const bool online = state_.snapshot.connected && state_.snapshot.onlineDevices > 0 && !stale;
    const auto &s = state_.snapshot;
    const auto readout = [&](const char *key) {
        const auto it = s.globalConfig.find(key);
        return online && it != s.globalConfig.end() && it->second != "N/A" ? W(it->second)
                                                                           : std::wstring(L"—");
    };
    if (!page_)
    {
        const int leftWidth = (width_ - 88) * 55 / 100, right = leftWidth + 56,
                  rightWidth = width_ - right - 32;
        Panel(dc, 32, 122, leftWidth, 166, Surface, Edge);
        Panel(dc, right, 122, rightWidth, 166, Surface, Edge);
        Text(dc, T(L"连接状态", L"CONNECTION"), 56, 139, 130, 26, 1, Secondary);
        const auto status = stale         ? T(L"等待状态更新", L"Waiting for status")
                            : online      ? T(L"设备在线", L"Interface online")
                            : s.connected ? T(L"等待声卡", L"Waiting for interface")
                                          : T(L"等待 UAD Console", L"Waiting for UAD Console");
        Text(dc, status, 56, 169, leftWidth - 48, 31, 3, online ? Ink : Secondary);
        std::set<std::string> names;
        for (const auto &c : s.channels)
            if (!c.deviceName.empty())
                names.insert(c.deviceName);
        for (const auto &m : s.monitors)
            if (!m.deviceName.empty())
                names.insert(m.deviceName);
        std::wstring devices;
        for (const auto &n : names)
        {
            if (!devices.empty())
                devices += L" / ";
            devices += W(n);
        }
        Text(dc, online ? devices : T(L"未发现在线设备", L"No online interface detected"), 57, 202,
             leftWidth - 48, 24, 0, Secondary);
        line(56, 232, leftWidth - 48);
        Text(dc, T(L"采样率", L"SAMPLE RATE"), 56, 236, 160, 20, 1, Secondary);
        Text(dc, readout("SampleRate"), 56, 255, 168, 23, 2, Ink);
        Text(dc, T(L"时钟源", L"CLOCK SOURCE"), 56 + leftWidth / 2, 236, leftWidth / 2 - 48, 20, 1,
             Secondary);
        Text(dc, readout("ClockSource"), 56 + leftWidth / 2, 255, leftWidth / 2 - 48, 23, 2, Ink);
        if (state_.conflict)
            Text(dc, T(L"EUCON SDK 示例正在运行", L"EUCON SDK example is running"), 210, 139,
                 leftWidth - 202, 26, 1, Gold, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        Text(dc, T(L"控制室", L"CONTROL ROOM"), right + 24, 139, rightWidth - 48, 26, 1, Secondary);
        const Monitor *m = online && s.monitors.size() == 1 ? &s.monitors.front() : nullptr;
        const auto level = m && m->level ? Number(m->level->value.Number()) : std::wstring(L"—");
        Text(dc, level, right + 20, 166, rightWidth - 105, 58, 4, Ink);
        Text(dc, L"dB", right + rightWidth - 79, 195, 52, 27, 2, Secondary);
        Text(dc, T(L"监听源", L"Source"), right + 24, 221, 112, 25, 0, Secondary);
        std::wstring source = m ? W(m->source) : L"—";
        if (m && (m->source == "mon" || m->source == "monitor"))
            source = L"MONITOR";
        else if (m &&
                 (m->source == "cue1" || m->source == "cue2" || m->source == "cue3" || m->source == "cue4"))
            source = L"CUE " + std::wstring(1, static_cast<wchar_t>(m->source.back()));
        Text(dc, source, right + 142, 221, rightWidth - 166, 25, 0, Ink,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const wchar_t *labels[] = {L"MUTE", L"DIM", L"MONO"};
        const bool active[] = {m && m->mute && m->mute->value.Bool(), m && m->dim && m->dim->value.Bool(),
                               m && m->mono && m->mono->value.Bool()};
        const int badge = (rightWidth - 64) / 3;
        for (int i = 0; i < 3; ++i)
        {
            Panel(dc, right + 24 + i * (badge + 8), 253, badge, 24, active[i] ? Selected : Surface,
                  active[i] ? Gold : Edge);
            Text(dc, labels[i], right + 24 + i * (badge + 8), 253, badge, 24, 1, active[i] ? Gold : Dim,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        Text(dc,
             std::wstring(T(L"控制上限  ", L"Control ceiling  ")) +
                 Number(state_.monitorEnabled ? state_.activeCeiling : saved_.monitorCeiling) + L" dB",
             right + 145, 139, rightWidth - 169, 26, 1, Secondary, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        Panel(dc, 32, 304, width_ - 64, height_ - 354, Surface, Edge);
        const auto channels = SurfaceChannels(s);
        Text(dc,
             std::wstring(T(L"通道状态 · ", L"CHANNELS · ")) + std::to_wstring(online ? channels.size() : 0),
             56, 318, 190, 25, 0, Ink);
        size_t counts[8]{};
        const uint32_t colors[] = {0x00FF00, 0xFF8000, 0x0000FF, 0xFF0000,
                                   0x00FFFF, 0x8000FF, 0xFFFF00, 0x9AA5B5};
        for (const auto &c : channels)
        {
            const auto color = ChannelColor(c);
            for (int i = 0; i < 8; ++i)
                if (color == colors[i])
                    ++counts[i];
        }
        const wchar_t *zh[] = {L"话放", L"线路", L"S/PDIF", L"虚拟", L"ADAT", L"AUX", L"对讲", L"其他"};
        const wchar_t *en[] = {L"PREAMP", L"LINE", L"S/PDIF", L"VIRTUAL", L"ADAT", L"AUX", L"TALK", L"OTHER"};
        const int n = counts[7] ? 8 : 7, col = (width_ - 310) / n;
        for (int i = 0; i < n; ++i)
        {
            const int x = 260 + col * i;
            Text(dc, std::wstring(T(zh[i], en[i])) + L" " + std::to_wstring(online ? counts[i] : 0), x, 318,
                 col - 8, 23, 1, Secondary);
            const auto color = RGB((colors[i] >> 16) & 255, (colors[i] >> 8) & 255, colors[i] & 255);
            RECT marker{S(x), S(344), S(x + col - 16), S(346)};
            auto b = CreateSolidBrush(online && counts[i] ? color : Edge);
            FillRect(dc, &marker, b);
            DeleteObject(b);
        }
        RECT listRect{};
        GetClientRect(GetDlgItem(window_, ChannelList), &listRect);
        const auto widths = DesktopChannelWidths(MulDiv(std::max(1L, listRect.right - 1), 96, dpi_));
        const wchar_t *zhColumns[] = {L"#",         L"通道",      L"格式",  L"电平 dB", L"声像",
                                      L"信号 dBFS", L"峰值 dBFS", L"M / S", L"UAD",     L"输出"};
        const wchar_t *enColumns[] = {L"#",          L"CHANNEL",   L"FORMAT", L"FADER dB", L"PAN",
                                      L"LEVEL dBFS", L"PEAK dBFS", L"M / S",  L"UAD",      L"OUTPUT"};
        int columnX = 44;
        for (int i = 0; i < 10; ++i)
        {
            Text(dc, T(zhColumns[i], enColumns[i]), columnX + 4, 352, widths[i] - 8, 25, 1, Secondary,
                 (i == 1 || i == 9 ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE);
            columnX += widths[i];
        }
    }
    else
    {
        Text(dc, T(L"设置", L"SETTINGS"), 38, 185, 150, 22, 1, Dim);
        const wchar_t *zh[] = {L"", L"通用", L"连接", L"控制", L"安全限制"};
        const wchar_t *en[] = {L"", L"General", L"Connection", L"Control", L"Protection"};
        Text(dc, T(zh[page_], en[page_]), 254, 127, width_ - 310, 37, 3, Ink);
        const int x = 254, cardWidth = width_ - 286, labelWidth = cardWidth - 132;
        const auto row = [&](int y, const wchar_t *a, const wchar_t *b, const wchar_t *c, const wchar_t *d) {
            Text(dc, T(a, b), x + 24, y, labelWidth, 27, 0, Ink);
            if (*c || *d)
                Text(dc, T(c, d), x + 24, y + 31, labelWidth, 33, 1, Secondary, DT_WORDBREAK);
        };
        if (page_ == 1)
        {
            Panel(dc, x, 184, cardWidth, 292, Surface, Edge);
            Text(dc, T(L"界面语言", L"Interface language"), x + 24, 200, cardWidth - 320, 31, 0, Ink);
            line(x + 24, 252, cardWidth - 48);
            row(268, L"关闭窗口后继续运行", L"Keep running when the window closes",
                L"可从系统托盘恢复窗口或退出程序。", L"Restore the window or quit from the system tray.");
            row(340, L"启动时隐藏主窗口", L"Start with the window hidden", L"程序启动后直接驻留系统托盘。",
                L"Start directly in the system tray.");
            row(412, L"登录 Windows 时启动", L"Launch at Windows sign-in", L"仅为当前 Windows 用户设置。",
                L"Applies to the current Windows user only.");
            Panel(dc, x, 500, cardWidth, 194, Surface, Edge);
            row(510, L"启用全局调出快捷键", L"Enable global summon shortcut", L"", L"");
            Text(dc, T(L"快捷键", L"Shortcut"), x + 24, 558, 160, 28, 0, Ink);
            row(600, L"识别后返回后台", L"Return to background after detection", L"", L"");
            Text(dc,
                 T(L"用于同时运行多个 EUCON 应用。快捷键会短暂调出本应用，便于 EuControl 识别并跟随；"
                   L"启用后会在识别完成后隐藏窗口。",
                   L"For workflows with multiple EUCON applications. The shortcut briefly summons this app "
                   L"so EuControl can follow it, then hides the window after detection when enabled."),
                 x + 24, 649, cardWidth - 48, 44, 1, Secondary, DT_WORDBREAK);
        }
        if (page_ == 2)
        {
            Panel(dc, x, 184, cardWidth, 136, Surface, Edge);
            Text(dc, L"EUCON", x + 24, 202, 150, 29, 2, Ink);
            Panel(dc, x, 334, cardWidth, 78, Surface, Edge);
            row(344, L"自动连接 EUCON", L"Connect EUCON automatically",
                L"声卡在线时自动连接。", L"Connect automatically while the interface is online.");
            Panel(dc, x, 428, cardWidth, 132, Surface, Edge);
            row(440, L"CONFIG", L"CONFIG", L"声卡设置、CUE 输出、插件选择和预置。",
                L"Interface settings, cue outputs, plug-in selection and presets.");
            if (draft_.configExtension != state_.configExtension)
                Text(dc, T(L"更改将在下次启动时生效。", L"This change takes effect after restart."),
                     x + 24, 519, cardWidth - 48, 27, 1, Gold);
        }
        if (page_ == 3)
        {
            Panel(dc, x, 180, cardWidth, 64, Surface, Edge);
            Text(dc, T(L"控制方案", L"Control profile"), x + 24, 194, cardWidth - 300, 31, 0, Ink);
            Panel(dc, x, 258, cardWidth, 326, Surface, Edge);
            row(276, L"通道控制", L"Channel control", L"", L"");
            row(356, L"控制室", L"Control room", L"", L"");
            row(436, L"48V、UNISON 与对讲", L"48V, UNISON and talkback", L"", L"");
            row(516, L"声卡与插件配置", L"Interface and plug-in configuration", L"", L"");
        }
        if (page_ == 4)
        {
            Panel(dc, x, 184, cardWidth, 131, Surface, Edge);
            Text(dc, T(L"主监听音量上限", L"Monitor level ceiling"), x + 24, 218, cardWidth - 242, 28, 0,
                 Ink);
            Panel(dc, width_ - 231, 206, 124, 47, Bg, Edge);
            Text(dc, L"dB", width_ - 104, 216, 46, 30, 0, Secondary);
            Text(dc,
                 T(L"范围 −96 至 0 dB。仅限制经本程序发出的音量控制，不改变当前音量，也不限制 Console "
                   L"或硬件操作。",
                   L"Range: −96 to 0 dB. Limits level commands from this bridge only. Does not change the "
                   L"current level or limit Console or hardware controls."),
                 x + 24, 266, cardWidth - 48, 40, 1, Secondary, DT_WORDBREAK);
            Panel(dc, x, 328, cardWidth, 93, Surface, Edge);
            row(344, L"启动时恢复控制", L"Restore control at startup", L"", L"");
            Panel(dc, x, 438, cardWidth, 157, Surface, Edge);
            Text(dc, T(L"暂停全部控制", L"Suspend all control"), x + 24, 452, cardWidth - 48, 28, 0, Ink);
        }
        if (dirty_)
            Text(dc, T(L"尚未保存", L"Unsaved changes"), 254, height_ - 80, width_ - 680, 36, 1, Gold);
    }
    line(32, height_ - 33, width_ - 64);
    std::wstring footer = page_ && dirty_ && !notice_.empty() ? notice_
                          : !state_.notice.empty()            ? state_.notice
                          : !notice_.empty()                  ? notice_
                          : state_.preview ? T(L"界面预览 · 未连接设备", L"UI preview · No device connection")
                          : !online        ? T(L"等待声卡连接。", L"Waiting for an interface connection.")
                                           : L"";
    Text(dc, footer, 34, height_ - 30, width_ - 68, 25, 1, !state_.notice.empty() ? Gold : Secondary);
}
void ApolloDesktop::DrawItem(DRAWITEMSTRUCT *d)
{
    ControlFrame frame(d->hDC, d->rcItem);
    auto buffered = *d;
    buffered.hDC = frame.Dc();
    DrawItemContents(&buffered);
    frame.Present();
}
void ApolloDesktop::DrawItemContents(DRAWITEMSTRUCT *d)
{
    if (d->CtlType == ODT_LISTVIEW && d->CtlID == ChannelList)
    {
        DrawChannel(d);
        return;
    }
    const bool disabled = (d->itemState & ODS_DISABLED) != 0;
    const bool selected = (d->itemState & ODS_SELECTED) != 0;
    if (d->CtlType == ODT_COMBOBOX)
    {
        auto b = CreateSolidBrush(selected ? Selected : Surface);
        FillRect(d->hDC, &d->rcItem, b);
        DeleteObject(b);
        if (d->itemID != static_cast<UINT>(-1))
        {
            const auto n = SendMessageW(d->hwndItem, CB_GETLBTEXTLEN, d->itemID, 0);
            if (n >= 0 && n < 4096)
            {
                std::wstring value(static_cast<size_t>(n) + 1, L'\0');
                SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, reinterpret_cast<LPARAM>(value.data()));
                auto r = d->rcItem;
                r.left += S(12);
                r.right -= S(12);
                SelectObject(d->hDC, fonts_[0]);
                SetTextColor(d->hDC, disabled ? Dim : Ink);
                SetBkMode(d->hDC, TRANSPARENT);
                DrawTextW(d->hDC, value.c_str(), -1, &r,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        return;
    }
    const bool nav = d->CtlID >= Overview && d->CtlID <= Protection;
    const bool current = nav && d->CtlID == static_cast<UINT>(Overview + page_);
    const bool primary = d->CtlID == SaveSettings;
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(d->hwndItem, &cursor);
    const bool hot = !disabled && PtInRect(&d->rcItem, cursor);
    const COLORREF fill = primary           ? hot ? RGB(237, 193, 126) : Gold
                          : current         ? Selected
                          : selected || hot ? Edge
                          : nav             ? Bg
                                            : Surface;
    const bool inCard = d->CtlID == OpenLogs || d->CtlID == FocusShortcutRecord ||
                        d->CtlID == Connect || d->CtlID == Disconnect || d->CtlID == LockAll;
    auto background = CreateSolidBrush(inCard ? Surface : Bg);
    FillRect(d->hDC, &d->rcItem, background);
    DeleteObject(background);
    DrawRound(d->hDC, d->rcItem, fill, primary ? Gold : current ? RGB(102, 82, 54) : nav ? Bg : Edge, S(8));
    wchar_t label[160]{};
    GetWindowTextW(d->hwndItem, label, 160);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, disabled ? Dim : primary ? Bg : current ? Gold : Ink);
    SelectObject(d->hDC, fonts_[0]);
    auto r = d->rcItem;
    r.left += S(16);
    r.right -= S(16);
    DrawTextW(d->hDC, label, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (nav ? DT_LEFT : DT_CENTER));
    if (d->itemState & ODS_FOCUS)
    {
        auto f = d->rcItem;
        InflateRect(&f, -S(3), -S(3));
        DrawFocusRect(d->hDC, &f);
    }
}
LRESULT CALLBACK ApolloDesktop::EditProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                         DWORD_PTR)
{
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, EditProc, id);
    if (message == WM_KEYDOWN && w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        SendMessageW(window, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_CHAR && w == 1)
        return 0;
    return DefSubclassProc(window, message, w, l);
}
LRESULT CALLBACK ApolloDesktop::ToggleProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                           DWORD_PTR data)
{
    auto self = reinterpret_cast<ApolloDesktop *>(data);
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, ToggleProc, id);
        return DefSubclassProc(window, message, w, l);
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_PAINT || message == WM_PRINTCLIENT)
    {
        PAINTSTRUCT paint{};
        auto dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(w);
        RECT r{};
        GetClientRect(window, &r);
        ControlFrame frame(dc, r);
        dc = frame.Dc();
        auto b = CreateSolidBrush(Surface);
        FillRect(dc, &r, b);
        DeleteObject(b);
        const bool on = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED,
                   enabled = IsWindowEnabled(window) != FALSE;
        auto fill = on && enabled ? Gold : Edge;
        DrawRound(dc, {1, self->S(2), r.right - 1, r.bottom - self->S(2)}, fill,
                  GetFocus() == window ? Ink : fill, r.bottom);
        const int size = self->S(16), x = on ? r.right - size - self->S(6) : self->S(6),
                  y = (r.bottom - size) / 2;
        const auto dot = !enabled ? Dim : on ? Bg : Ink;
        {
            Gdiplus::Graphics g(dc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush knob(Gdiplus::Color(255, GetRValue(dot), GetGValue(dot), GetBValue(dot)));
            g.FillEllipse(&knob, x, y, size, size);
        }
        frame.Present();
        if (message == WM_PAINT)
            EndPaint(window, &paint);
        return 0;
    }
    const auto result = DefSubclassProc(window, message, w, l);
    if (message == BM_SETCHECK || message == WM_ENABLE || message == WM_SETFOCUS || message == WM_KILLFOCUS ||
        message == WM_LBUTTONUP || message == WM_KEYUP)
        InvalidateRect(window, nullptr, FALSE);
    return result;
}
LRESULT CALLBACK ApolloDesktop::ButtonProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                           DWORD_PTR)
{
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, ButtonProc, id);
        return DefSubclassProc(window, message, w, l);
    }
    if (message == WM_MOUSEMOVE)
    {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        InvalidateRect(window, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE)
        InvalidateRect(window, nullptr, FALSE);
    return DefSubclassProc(window, message, w, l);
}
LRESULT CALLBACK ApolloDesktop::ComboProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                          DWORD_PTR data)
{
    auto self = reinterpret_cast<ApolloDesktop *>(data);
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, ComboProc, id);
        return DefSubclassProc(window, message, w, l);
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_PAINT || message == WM_PRINTCLIENT)
    {
        PAINTSTRUCT paint{};
        auto dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(w);
        RECT r{};
        GetClientRect(window, &r);
        ControlFrame frame(dc, r);
        dc = frame.Dc();
        auto background = CreateSolidBrush(Surface);
        FillRect(dc, &r, background);
        DeleteObject(background);
        DrawRound(dc, r, Surface, GetFocus() == window ? Gold : Edge, self->S(8));
        const int selected = static_cast<int>(SendMessageW(window, CB_GETCURSEL, 0, 0));
        wchar_t value[256]{};
        if (selected >= 0 && SendMessageW(window, CB_GETLBTEXTLEN, selected, 0) < 256)
            SendMessageW(window, CB_GETLBTEXT, selected, reinterpret_cast<LPARAM>(value));
        const int width = MulDiv(r.right, 96, self->dpi_), height = MulDiv(r.bottom, 96, self->dpi_);
        self->Text(dc, value, 12, 0, width - 50, height, 0, Ink);
        self->Text(dc, L"\uE70D", width - 30, 0, 18, height, 5, Secondary,
                   DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        frame.Present();
        if (message == WM_PAINT)
            EndPaint(window, &paint);
        return 0;
    }
    const auto result = DefSubclassProc(window, message, w, l);
    if (message == CB_SETCURSEL || message == WM_ENABLE || message == WM_SETFOCUS ||
        message == WM_KILLFOCUS || message == CB_SHOWDROPDOWN)
        InvalidateRect(window, nullptr, FALSE);
    return result;
}
void ApolloDesktop::AddTray()
{
    NOTIFYICONDATAW tray{sizeof(tray)};
    tray.hWnd = window_;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = icon_;
    wcscpy_s(tray.szTip, L"UAD Console Bridge for EUCON");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (trayAdded_)
    {
        tray.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray);
    }
}
void ApolloDesktop::TrayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, Restore, T(L"显示窗口", L"Show window"));
    AppendMenuW(menu, MF_STRING, Settings, T(L"设置", L"Settings"));
    AppendMenuW(menu, MF_STRING, Restart, T(L"重新启动", L"Restart"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Exit, T(L"退出", L"Quit"));
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window_);
    const auto command =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);
    if (command == Restore)
        Show();
    if (command == Settings)
        Show(true);
    if (command == Restart && actions_.restart)
        actions_.restart();
    if (command == Exit && actions_.exit)
        actions_.exit();
}
void ApolloDesktop::OpenFolder()
{
    if (state_.preview)
    {
        notice_ = T(L"界面预览不创建日志文件夹。正常运行时可使用此功能。",
                    L"UI preview does not create a log folder. This action is available during normal operation.");
        return;
    }
    try
    {
        const auto folder = DesktopFolder() / L"logs";
        std::filesystem::create_directories(folder);
        PIDLIST_ABSOLUTE item = nullptr;
        if (FAILED(SHParseDisplayName(folder.c_str(), nullptr, &item, 0, nullptr)))
            throw std::runtime_error("Cannot open folder");
        SHELLEXECUTEINFOW open{sizeof(open)};
        open.fMask = SEE_MASK_IDLIST;
        open.hwnd = window_;
        open.lpVerb = L"open";
        open.lpIDList = item;
        open.nShow = SW_SHOWNORMAL;
        const bool success = ShellExecuteExW(&open) != FALSE;
        CoTaskMemFree(item);
        if (!success)
            throw std::runtime_error("Cannot open folder");
    }
    catch (...)
    {
        notice_ = T(L"无法打开日志文件夹。", L"Unable to open the log folder.");
    }
}
LRESULT CALLBACK ApolloDesktop::Proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto *self = reinterpret_cast<ApolloDesktop *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<ApolloDesktop *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(window, message, w, l);
    return self->Message(message, w, l);
}
LRESULT ApolloDesktop::Message(UINT message, WPARAM w, LPARAM l)
{
    if (taskbarCreated_ && message == taskbarCreated_)
    {
        trayAdded_ = false;
        AddTray();
        if (!trayAdded_)
            Show();
        return 0;
    }
    switch (message)
    {
    case WM_CREATE:
        dpi_ = static_cast<int>(GetDpiForWindow(window_));
        Fonts();
        Build();
        return 0;
    case WM_COMMAND:
        Act(LOWORD(w), HIWORD(w));
        return 0;
    case WM_DRAWITEM:
        DrawItem(reinterpret_cast<DRAWITEMSTRUCT *>(l));
        return TRUE;
    case WM_MEASUREITEM:
        reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight =
            S(reinterpret_cast<MEASUREITEMSTRUCT *>(l)->CtlID == ChannelList ? 44 : 36);
        return TRUE;
    case WM_NOTIFY: {
        const auto header = reinterpret_cast<NMHDR *>(l);
        if (header->idFrom == ChannelList && header->code == LVN_GETDISPINFOW)
        {
            auto info = reinterpret_cast<NMLVDISPINFOW *>(l);
            if (info->item.mask & LVIF_TEXT)
            {
                const auto text = ChannelAccessibleText(static_cast<size_t>(info->item.iItem));
                if (info->item.pszText && info->item.cchTextMax > 0)
                    lstrcpynW(info->item.pszText, text.c_str(), info->item.cchTextMax);
            }
            return 0;
        }
        if (header->idFrom == ChannelList && header->code == LVN_ODFINDITEMW)
            return -1; // Status rows have no editable/search action; retain native arrow/page navigation.
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(w), Ink);
        SetBkColor(reinterpret_cast<HDC>(w), Surface);
        return reinterpret_cast<LRESULT>(fieldBrush_);
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        auto target = BeginPaint(window_, &ps);
        RECT r{};
        GetClientRect(window_, &r);
        auto dc = CreateCompatibleDC(target);
        auto bitmap = CreateCompatibleBitmap(target, std::max(1L, r.right), std::max(1L, r.bottom));
        const auto old = SelectObject(dc, bitmap);
        Paint(dc);
        BitBlt(target, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window_, &ps);
        return 0;
    }
    case WM_SIZE:
        Layout();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(w);
        Fonts();
        const auto *r = reinterpret_cast<RECT *>(l);
        SetWindowPos(window_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Build();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        RECT r{0, 0, S(1020), S(820)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
        reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
        return 0;
    }
    case TrayMessage:
        if (LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT || LOWORD(l) == WM_LBUTTONDBLCLK)
            Show();
        else if (LOWORD(l) == WM_CONTEXTMENU || LOWORD(l) == WM_RBUTTONUP)
            TrayMenu();
        return 0;
    case WM_CLOSE:
        if (saved_.background)
            Hide();
        else if (actions_.exit)
            actions_.exit();
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w && actions_.exit)
            actions_.exit();
        return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW tray{sizeof(tray)};
        tray.hWnd = window_;
        tray.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &tray);
        trayAdded_ = false;
        return 0;
    }
    }
    return DefWindowProcW(window_, message, w, l);
}
} // namespace apollo
