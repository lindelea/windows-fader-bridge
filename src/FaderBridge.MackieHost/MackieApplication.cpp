#include "MackieApplication.h"
#include "CommandCatalog.h"
#include "DiagnosticLog.h"
#include "WindowsCommandExecutor.h"
#include <Dwmapi.h>
#include <UxTheme.h>
#include <AppModel.h>
#include <set>
#include <sstream>
#include <iomanip>

namespace
{
constexpr UINT AudioMessage = WM_APP + 10, TrayMessage = WM_APP + 11, CommandResult = WM_APP + 12;
enum Id { Input = 101, Output, Profile, Refresh, Connection, Touch, Lcd, Meters, Trace,
    Tracks, PreviousBank, NextBank, BankLabel, ClearSolo, Mono, Focus, SelectTrack, VolumeKnobs,
    Category, CommandChoice, Learn, BindingList, RemoveBinding, State, MediaLabel, Hint,
    RouteOut, RouteIn, ApplyOut, ApplyIn, Hide, Diagnostics, ExitApp, Compact };
std::wstring Text(HWND control)
{
    const int n = GetWindowTextLengthW(control);
    std::wstring text(n + 1, L'\0');
    GetWindowTextW(control, text.data(), n + 1); text.resize(n); return text;
}
int Choice(HWND window, int id) { return static_cast<int>(SendDlgItemMessageW(window, id, CB_GETCURSEL, 0, 0)); }
bool Checked(HWND window, int id) { return SendDlgItemMessageW(window, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void SetCheck(HWND window, int id, bool value) { SendDlgItemMessageW(window, id, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
std::wstring Percent(float v) { return std::to_wstring(static_cast<int>(std::lround(v * 100))) + L"%"; }

// This Windows-only activation helper never changes audio or controller state.
// Processes owning an app's child windows are considered for packaged apps.
struct WindowSearch { const AudioStripState* strip; HWND found = nullptr; };
BOOL CALLBACK ChildMatch(HWND child, LPARAM parameter)
{
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    DWORD process = 0; GetWindowThreadProcessId(child, &process);
    const auto& pids = search.strip->focusProcessIds;
    bool match = std::find(pids.begin(), pids.end(), process) != pids.end();
    if (!match)
    {
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
        if (handle)
        {
            wchar_t path[32768]{}; DWORD length = 32768;
            if (!search.strip->focusExecutablePath.empty() && QueryFullProcessImageNameW(handle, 0, path, &length))
                match = _wcsicmp(path, search.strip->focusExecutablePath.c_str()) == 0;
            if (!match && !search.strip->focusPackageFamilyName.empty())
            {
                UINT32 size = 0;
                if (GetPackageFamilyName(handle, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER && size < 32768)
                {
                    std::wstring family(size, L'\0');
                    if (GetPackageFamilyName(handle, &size, family.data()) == ERROR_SUCCESS)
                        match = _wcsicmp(family.c_str(), search.strip->focusPackageFamilyName.c_str()) == 0;
                }
            }
            CloseHandle(handle);
        }
    }
    if (match)
    { search.found = GetAncestor(child, GA_ROOT); return FALSE; }
    return TRUE;
}
BOOL CALLBACK WindowMatch(HWND candidate, LPARAM parameter)
{
    if (!IsWindowVisible(candidate) || GetWindow(candidate, GW_OWNER) || !GetWindowTextLengthW(candidate) ||
        (GetWindowLongPtrW(candidate, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(candidate, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    ChildMatch(candidate, parameter);
    if (!search.found) EnumChildWindows(candidate, ChildMatch, parameter);
    return search.found ? FALSE : TRUE;
}
bool FocusApplication(const AudioStripState& strip)
{
    WindowSearch search{&strip};
    EnumWindows(WindowMatch, reinterpret_cast<LPARAM>(&search));
    if (!search.found) return false;
    if (IsIconic(search.found))
    {
        // This runs on the command worker, never in the MIDI message loop.
        DWORD_PTR result = 0;
        SendMessageTimeoutW(search.found, WM_SYSCOMMAND, SC_RESTORE, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 120, &result);
        ShowWindowAsync(search.found, SW_RESTORE);
        for (int i = 0; i < 20 && IsIconic(search.found); ++i) Sleep(5);
    }
    const DWORD own = GetCurrentThreadId();
    const DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD target = GetWindowThreadProcessId(search.found, nullptr);
    const bool attachForeground = foreground && foreground != own && AttachThreadInput(own, foreground, TRUE);
    const bool attachTarget = target && target != own && target != foreground && AttachThreadInput(own, target, TRUE);
    BringWindowToTop(search.found);
    const bool result = SetForegroundWindow(search.found) != FALSE;
    if (attachTarget) AttachThreadInput(own, target, FALSE);
    if (attachForeground) AttachThreadInput(own, foreground, FALSE);
    return result;
}
}

MackieApplication::MackieApplication(bool smoke)
    : settings_(MackieSettings::Load()), midi_([this](DWORD raw) { OnMidi(raw); }),
      surface_([this](const mackie::Bytes& bytes) {
          // A user binding takes LED ownership of that note, not of strip LEDs.
          if (bytes.size() == 3 && bytes[0] == 0x90 && settings_.bindings.count(bytes[1])) return;
          midi_.Send(bytes);
      }, [this](const mackie::Action& action) { OnAction(action); }), smoke_(smoke)
{
    surface_.RestoreOrder(settings_.trackOrder);
    surface_.RequireTouch = settings_.touch; surface_.LcdEnabled = settings_.lcd; surface_.MetersEnabled = settings_.meters;
}
MackieApplication::~MackieApplication()
{
    midi_.Close(); audio_.reset(); media_.reset();
    { std::scoped_lock lock(commandMutex_); commandRunning_ = false; commandQueue_.clear(); }
    commandWake_.notify_all();
    if (commandWorker_.joinable()) commandWorker_.join();
    if (font_) DeleteObject(font_);
    if (titleFont_) DeleteObject(titleFont_);
    if (background_) DeleteObject(background_);
}
int MackieApplication::Run(HINSTANCE instance, int show)
{
    instance_ = instance;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = WindowProc;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"WindowsFaderBridge.Mackie.Main";
    background_ = CreateSolidBrush(RGB(22, 27, 35)); wc.hbrBackground = background_;
    RegisterClassW(&wc);
    // System-DPI aware: the OS scales this fixed research dashboard at other DPIs.
    const auto dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(1040, dpi, 96), MulDiv(810, dpi, 96)};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    window_ = CreateWindowExW(0, wc.lpszClassName, L"Windows Fader Bridge for Mackie Control — Research",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, this);
    if (!window_) return 2;
    BOOL dark = TRUE; DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
    CreateControls(); RefreshPorts();
    audio_ = std::make_unique<NativeAudioController>(window_, AudioMessage);
    media_ = std::make_unique<MackieMedia>();
    commandWorker_ = std::thread(&MackieApplication::CommandLoop, this);
    audio_->Start(); started_ = GetTickCount64();
    SetTimer(window_, 1, 16, nullptr);
    if (!smoke_)
    {
        tray_.cbSize = sizeof(tray_); tray_.hWnd = window_; tray_.uID = 1;
        tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; tray_.uCallbackMessage = TrayMessage;
        tray_.hIcon = wc.hIcon;
        wcscpy_s(tray_.szTip, L"Windows Fader Bridge for Mackie Control");
        if (!Shell_NotifyIconW(NIM_ADD, &tray_)) tray_.hWnd = nullptr;
        ShowWindow(window_, show); UpdateWindow(window_);
    }
    Status(L"安全待机：选择匹配的 MCU 输入 / 输出端口后连接。不会自动打开任何设备。");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(window_, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return static_cast<int>(message.wParam);
}
HWND MackieApplication::Control(const wchar_t* type, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height)
{
    const UINT dpi = GetDpiForSystem();
    const HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        MulDiv(x, dpi, 96), MulDiv(y, dpi, 96), MulDiv(width, dpi, 96), MulDiv(height, dpi, 96),
        window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return control;
}
void MackieApplication::CreateControls()
{
    const int dpi = GetDpiForSystem();
    font_ = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    titleFont_ = CreateFontW(-MulDiv(24, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto title = Control(L"STATIC", L"Windows Fader Bridge  /  Mackie Control", 0, 0, 24, 16, 980, 34);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
    Control(L"STATIC", L"独立研究版 · 通用 MCU 协议 · EUCON 程序不受影响", 0, 0, 24, 53, 970, 22);
    constexpr DWORD combo = CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;
    constexpr DWORD button = BS_PUSHBUTTON | WS_TABSTOP;
    Control(L"STATIC", L"MIDI 输入（设备 → Windows）", 0, 0, 24, 87, 380, 20);
    Control(L"STATIC", L"MIDI 输出（Windows → 设备）", 0, 0, 430, 87, 380, 20);
    Control(WC_COMBOBOXW, L"", combo, Input, 24, 111, 388, 340);
    Control(WC_COMBOBOXW, L"", combo, Output, 430, 111, 388, 340);
    Control(L"BUTTON", L"刷新端口", button, Refresh, 836, 108, 180, 30);
    auto profile = Control(WC_COMBOBOXW, L"", combo, Profile, 24, 153, 260, 120);
    SendMessageW(profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"通用 Mackie Control（默认）"));
    SendMessageW(profile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"iCON P1-Nano（标准 MCU）"));
    SendMessageW(profile, CB_SETCURSEL, settings_.profile == L"p1-nano" ? 1 : 0, 0);
    Control(L"BUTTON", L"触摸保护", BS_AUTOCHECKBOX | WS_TABSTOP, Touch, 308, 153, 110, 25);
    Control(L"BUTTON", L"LCD", BS_AUTOCHECKBOX | WS_TABSTOP, Lcd, 435, 153, 70, 25);
    Control(L"BUTTON", L"峰值表", BS_AUTOCHECKBOX | WS_TABSTOP, Meters, 521, 153, 90, 25);
    Control(L"BUTTON", L"MIDI 日志", BS_AUTOCHECKBOX | WS_TABSTOP, Trace, 628, 153, 150, 25);
    SetCheck(window_, Touch, settings_.touch); SetCheck(window_, Lcd, settings_.lcd); SetCheck(window_, Meters, settings_.meters);
    // The system light theme draws black checkbox captions even on a dark
    // parent. Classic checkbox painting honors our explicit text colors.
    for (int id : {Touch, Lcd, Meters, Trace}) SetWindowTheme(GetDlgItem(window_, id), L"", L"");
    Control(L"BUTTON", L"连接设备", button, Connection, 836, 149, 180, 32);
    Control(L"STATIC", L"", SS_LEFT, State, 24, 194, 992, 38);
    Control(L"BUTTON", L"◀ 上一组", button, PreviousBank, 24, 239, 100, 28);
    Control(L"BUTTON", L"下一组 ▶", button, NextBank, 136, 239, 100, 28);
    Control(L"STATIC", L"", 0, BankLabel, 251, 244, 355, 22);
    Control(L"BUTTON", L"旋钮：Pan", button, VolumeKnobs, 640, 239, 140, 28);
    Control(L"BUTTON", L"清理离线空位", button, Compact, 796, 239, 220, 28);
    auto tracks = Control(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, Tracks, 24, 280, 992, 212);
    SetWindowTheme(tracks, L"DarkMode_Explorer", nullptr);
    ListView_SetExtendedListViewStyle(tracks, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(tracks, RGB(29, 35, 45)); ListView_SetTextBkColor(tracks, RGB(29, 35, 45)); ListView_SetTextColor(tracks, RGB(229, 236, 245));
    const wchar_t* names[] = {L"CH", L"Windows 通道（完整名称）", L"音量", L"Pan", L"峰值", L"状态"};
    const int widths[] = {55, 463, 72, 65, 85, 224};
    for (int i = 0; i < 6; ++i) { LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = const_cast<wchar_t*>(names[i]); c.cx = MulDiv(widths[i], dpi, 96); ListView_InsertColumn(tracks, i, &c); }
    Control(L"BUTTON", L"选择通道", button, SelectTrack, 24, 504, 112, 28);
    Control(L"BUTTON", L"应用到前台", button, Focus, 148, 504, 126, 28);
    Control(L"BUTTON", L"Clear Solo", button, ClearSolo, 286, 504, 126, 28);
    Control(L"BUTTON", L"单声道切换", button, Mono, 424, 504, 134, 28);
    Control(L"STATIC", L"", SS_LEFT, MediaLabel, 578, 501, 438, 38);
    Control(L"STATIC", L"所选应用输出", 0, 0, 24, 550, 110, 22);
    Control(WC_COMBOBOXW, L"", combo, RouteOut, 137, 545, 281, 230);
    Control(L"BUTTON", L"应用", button, ApplyOut, 432, 543, 70, 28);
    Control(L"STATIC", L"所选应用输入", 0, 0, 527, 550, 110, 22);
    Control(WC_COMBOBOXW, L"", combo, RouteIn, 639, 545, 290, 230);
    Control(L"BUTTON", L"应用", button, ApplyIn, 943, 543, 73, 28);
    Control(L"STATIC", L"自由分配 Windows 命令  ·  选择命令 → 学习 → 按设备按键", 0, 0, 24, 590, 980, 24);
    auto category = Control(WC_COMBOBOXW, L"", combo, Category, 24, 622, 224, 300);
    std::set<std::wstring> categories;
    for (const auto& command : MackieCommands) if (categories.insert(command.category).second)
        SendMessageW(category, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(command.category));
    SendMessageW(category, CB_SETCURSEL, 0, 0);
    Control(WC_COMBOBOXW, L"", combo, CommandChoice, 263, 622, 365, 320);
    Control(L"BUTTON", L"学习按键", button, Learn, 642, 619, 143, 30);
    Control(L"BUTTON", L"取消选中分配", button, RemoveBinding, 801, 619, 215, 30);
    Control(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | WS_BORDER, BindingList, 24, 665, 604, 96);
    Control(L"STATIC", L"MCU 功能键或任意其他 MIDI 通道的 Note 按键。\n不占用通道、分页、触摸等核心控制。\n所有分配保存在此用户的独立 Mackie 配置中。", 0, Hint, 642, 665, 374, 72);
    Control(L"BUTTON", L"诊断目录", button, Diagnostics, 640, 768, 112, 28);
    Control(L"BUTTON", L"后台运行", button, Hide, 767, 768, 112, 28);
    Control(L"BUTTON", L"退出", button, ExitApp, 894, 768, 122, 28);
    FillCommands(); FillBindings();
}
void MackieApplication::RefreshPorts()
{
    if (midi_.Connected()) return;
    inputs_ = WinMidiPort::Inputs(); outputs_ = WinMidiPort::Outputs();
    const auto fill = [&](int id, const std::vector<MidiPortName>& ports, const std::wstring& previous) {
        SendDlgItemMessageW(window_, id, CB_RESETCONTENT, 0, 0);
        int selected = -1, matches = 0;
        for (std::size_t i = 0; i < ports.size(); ++i)
        {
            auto label = ports[i].name + L"  [" + std::to_wstring(ports[i].index) + L"]";
            if (IconDawPort(ports[i].name) == 4) label += L" — iMAP 保留，不可连接";
            SendDlgItemMessageW(window_, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (ports[i].name == previous) { selected = static_cast<int>(i); ++matches; }
        }
        // Duplicate names are ambiguous; force the user to choose, never guess.
        SendDlgItemMessageW(window_, id, CB_SETCURSEL, matches == 1 ? selected : -1, 0);
    };
    fill(Input, inputs_, settings_.input); fill(Output, outputs_, settings_.output);
}
void MackieApplication::Connect()
{
    if (smoke_) return;
    const int a = Choice(window_, Input), b = Choice(window_, Output);
    if (a < 0 || b < 0 || a >= static_cast<int>(inputs_.size()) || b >= static_cast<int>(outputs_.size()))
    { Status(L"请手动选择匹配的一对 MIDI 输入 / 输出。"); return; }
    const auto& input = inputs_[a]; const auto& output = outputs_[b];
    const int inPort = IconDawPort(input.name), outPort = IconDawPort(output.name);
    if (inPort == 4 || outPort == 4) { Status(L"P1-Nano 端口 4 由 iMAP / 固件维护使用，不能作为 MCU 端口。"); return; }
    if ((inPort || outPort) && inPort != outPort) { Status(L"P1-Nano 必须选择相同 DAW 编号的输入 / 输出端口。"); return; }
    if (Choice(window_, Profile) == 1 && (!inPort || !outPort))
    { Status(L"这不是可识别的 P1-Nano 端口；其他设备请选择通用 Mackie Control。"); return; }
    // Port indexes can change after a hotplug. Verify this exact selection again.
    const auto ins = WinMidiPort::Inputs(), outs = WinMidiPort::Outputs();
    const auto exists = [](const std::vector<MidiPortName>& ports, const MidiPortName& p) {
        return std::any_of(ports.begin(), ports.end(), [&](const auto& x) { return x.index == p.index && x.name == p.name; }); };
    if (!exists(ins, input) || !exists(outs, output)) { RefreshPorts(); Status(L"端口列表已改变，请重新选择。"); return; }
    if (!midi_.Open(input.index, output.index)) { Status(midi_.Error()); return; }
    selectedInput_ = input.index; selectedOutput_ = output.index;
    settings_.input = input.name; settings_.output = output.name;
    settings_.profile = Choice(window_, Profile) == 1 ? L"p1-nano" : L"mcu";
    surface_.ResetConnection(); customPressed_.fill(false); customFeedback_.clear();
    for (int id : {Input, Output, Profile, Refresh, Touch, Lcd, Meters}) EnableWindow(GetDlgItem(window_, id), FALSE);
    SetDlgItemTextW(window_, Connection, L"断开设备"); Save();
    surface_.Feedback(GetTickCount64(), true);
    Status(L"MCU 已连接：触摸推子控制音量，旋钮控制 Pan，Rec 选择默认设备，Select 选择 / 聚焦应用。");
}
void MackieApplication::Disconnect()
{
    learning_ = false; SetDlgItemTextW(window_, Learn, L"学习按键");
    midi_.Close(); surface_.ResetConnection(); customPressed_.fill(false); customFeedback_.clear();
    for (int id : {Input, Output, Profile, Refresh, Touch, Lcd, Meters}) EnableWindow(GetDlgItem(window_, id), TRUE);
    SetDlgItemTextW(window_, Connection, L"连接设备");
}
void MackieApplication::OnMidi(DWORD raw)
{
    if (!mackie::ValidShort(raw)) return;
    const int type = raw & 0xF0, channel = raw & 15, note = (raw >> 8) & 127;
    if (type == 0x90 || type == 0x80)
    {
        // Touch is never consumed by learning or command dispatch. This must
        // precede the protected-note rejection, including during Learn mode.
        if (channel == 0 && note >= 0x68 && note <= 0x70) { surface_.Input(raw, GetTickCount64()); return; }
        const bool down = type == 0x90 && ((raw >> 16) & 127) != 0;
        const int key = channel * 128 + note;
        const bool previous = customPressed_[key]; customPressed_[key] = down;
        if (learning_)
        {
            if (down && !previous)
            {
                if (!MackieSettings::Bindable(channel, note)) { Status(L"这是核心 MCU 按键，不能覆盖。请选择 F 键 / 功能键，或 MIDI 通道 2–16 的 Note。"); return; }
                settings_.bindings[key] = learningCommand_; learning_ = false;
                customFeedback_.clear(); SetDlgItemTextW(window_, Learn, L"学习按键");
                FillBindings(); Save(); Status(L"分配已保存。本次学习按键不会执行命令。");
            }
            // Keep core touch releases alive while learning, otherwise a motor
            // could stay indefinitely suppressed after the user lets go.
            if (channel == 0 && !down) surface_.Input(raw, GetTickCount64());
            return;
        }
        const auto binding = settings_.bindings.find(key);
        if (binding != settings_.bindings.end())
        {
            if (down && !previous) ExecuteCommand(binding->second);
            return;
        }
    }
    if (!learning_) surface_.Input(raw, GetTickCount64());
}
const AudioStripState* MackieApplication::Find(const std::wstring& key) const
{
    if (frame_) for (const auto& strip : frame_->strips) if (strip.active && strip.key == key) return &strip;
    return nullptr;
}
void MackieApplication::OnAction(const mackie::Action& action)
{
    if (!audio_ || closing_ || smoke_) return;
    using K = mackie::ActionKind;
    const auto strip = Find(action.key);
    bool accepted = false;
    switch (action.kind)
    {
    case K::Volume: if (strip) accepted = audio_->QueueTrackControl(AudioTrackControl::Volume, strip->key, action.value); break;
    case K::Pan: if (strip) accepted = audio_->QueueTrackControl(AudioTrackControl::Pan, strip->key, action.value); break;
    case K::Mute: if (strip) accepted = audio_->QueueTrackControl(AudioTrackControl::Mute, strip->key, action.value); break;
    case K::DefaultDevice: if (strip) accepted = audio_->QueueTrackControl(AudioTrackControl::SetDefault, strip->key); break;
    case K::Solo: if (strip) accepted = audio_->QueueToggleSolo(strip->key); break;
    case K::ClearSolo: accepted = audio_->QueueClearSolo(); break;
    case K::Focus:
        if (strip && strip->role == AudioStripRole::Application)
        {
            std::scoped_lock lock(commandMutex_);
            if (commandQueue_.size() < 64) { commandQueue_.push_back([target = *strip] { return FocusApplication(target); }); accepted = true; }
            commandWake_.notify_one();
        }
        else if (strip) accepted = true; // Endpoint selection has no application window.
        break;
    default:
        if (strip && strip->role == AudioStripRole::Application) media_->Target(strip->focusExecutablePath, strip->focusPackageFamilyName);
        media_->Request(action.kind, action.value); accepted = true; break;
    }
    FB_TRACE("MACKIE_ACTION kind=%d key=%ls value=%.4f accepted=%d", static_cast<int>(action.kind), action.key.c_str(), action.value, accepted);
    if (!accepted) Status(L"请求未被接受，可能是通道已离线、应用无窗口或 Windows 限制；请查看诊断。");
}
void MackieApplication::OnFrame(std::unique_ptr<AudioFrame> frame)
{
    if (closing_) return;
    frame_ = std::move(frame);
    std::vector<mackie::Track> tracks;
    for (const auto& strip : frame_->strips) if (strip.active)
    {
        mackie::Track t;
        t.key = strip.key; t.name = strip.name; t.active = true; t.muted = strip.muted; t.solo = strip.soloed;
        t.defaultDevice = strip.isDefault; t.defaultSelectable = strip.defaultSelectable;
        t.master = strip.role == AudioStripRole::MasterOutput; t.application = strip.role == AudioStripRole::Application;
        t.canPan = strip.panAvailable; t.volume = strip.volume; t.pan = strip.pan; t.peakDb = strip.peakDb;
        if (!strip.meterDb.empty()) t.peakDb = *std::max_element(strip.meterDb.begin(), strip.meterDb.end());
        tracks.push_back(std::move(t));
    }
    surface_.Update(tracks, GetTickCount64()); surface_.AnySolo = frame_->anySolo;
    if (settings_.trackOrder != surface_.Order()) { settings_.trackOrder = surface_.Order(); dirtySettings_ = true; }
}
void MackieApplication::Tick()
{
    const auto now = GetTickCount64();
    if (smoke_ && now - started_ >= 6000)
    {
        const auto active = frame_ ? std::count_if(frame_->strips.begin(), frame_->strips.end(), [](const auto& s) { return s.active; }) : 0;
        const bool okay = audio_ && audio_->IsReady() && active > 0 && !midi_.Connected() && midi_.Sent == 0;
        FB_TRACE("MACKIE_SMOKE result=%s tracks=%zu midi_inputs=%zu midi_outputs=%zu midi_sent=%llu",
            okay ? "PASS" : "FAIL", static_cast<std::size_t>(active), inputs_.size(), outputs_.size(), midi_.Sent);
        Quit(); PostQuitMessage(okay ? 0 : 3); return;
    }
    const auto selected = surface_.Selected();
    if (selected) if (const auto strip = Find(selected->key); strip && strip->role == AudioStripRole::Application)
        media_->Target(strip->focusExecutablePath, strip->focusPackageFamilyName);
    const auto media = media_->State(mediaStatus_);
    surface_.MediaAvailable = media.available; surface_.MediaPlaying = media.playing; surface_.MediaRepeat = media.repeatMode != 0;
    if (midi_.Connected())
    {
        midi_.Collect(); surface_.Feedback(now);
        for (const auto& [key, id] : settings_.bindings)
        {
            const auto command = FindMackieCommand(id);
            const bool on = command && command->special ?
                (frame_ && (command->special == 1 ? frame_->monoAudioEnabled : frame_->anySolo)) : customPressed_[key];
            if (!customFeedback_.count(key) || customFeedback_[key] != on)
            {
                midi_.Send({static_cast<std::uint8_t>(0x90 | key / 128), static_cast<std::uint8_t>(key % 128), static_cast<std::uint8_t>(on ? 127 : 0)});
                customFeedback_[key] = on;
            }
        }
        if (midi_.Faulted()) { const auto error = midi_.Error(); Disconnect(); Status(L"MIDI 已安全断开：" + error); }
    }
    if (now >= renderDue_)
    {
        renderDue_ = now + 250; Render();
        if (media.available)
        {
            auto text = (media.playing ? L"▶ " : L"Ⅱ ") + media.title;
            if (!media.artist.empty()) text += L" — " + media.artist;
            if (media.hasPosition) text += L"  " + Percent(media.position) + (media.canSeek ? L"" : L"（只读）");
            if (!mediaStatus_.empty()) text += L"\n" + mediaStatus_;
            SetDlgItemTextW(window_, MediaLabel, text.c_str());
        }
        else SetDlgItemTextW(window_, MediaLabel, mediaStatus_.empty() ? L"媒体：所选应用未提供可读取的播放状态" : mediaStatus_.c_str());
    }
    if (dirtySettings_ && now >= saveDue_) { saveDue_ = now + 2000; Save(); }
}
std::wstring MackieApplication::RowKey() const
{
    const int row = ListView_GetNextItem(GetDlgItem(window_, Tracks), -1, LVNI_SELECTED);
    return row >= 0 && row < static_cast<int>(rowKeys_.size()) ? rowKeys_[row] : L"";
}
void MackieApplication::Render()
{
    renderGuard_ = true;
    const HWND list = GetDlgItem(window_, Tracks);
    auto selected = RowKey();
    if (selected.empty() && surface_.Selected()) selected = surface_.Selected()->key;
    const auto& order = surface_.Order();
    if (rowKeys_ != order)
    {
        ListView_DeleteAllItems(list); rowKeys_ = order;
        for (int i = 0; i < static_cast<int>(order.size()); ++i)
        {
            auto text = std::to_wstring(i + 1); LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = i; item.pszText = text.data();
            ListView_InsertItem(list, &item);
        }
    }
    for (int i = 0; i < static_cast<int>(rowKeys_.size()); ++i)
    {
        const auto strip = Find(rowKeys_[i]);
        std::wstring cells[5]{strip ? strip->name : L"（离线 · 保留通道位置）", L"", L"", L"", L""};
        if (strip)
        {
            cells[1] = Percent(strip->volume);
            if (strip->panAvailable) cells[2] = std::abs(strip->pan) < .01F ? L"Center" :
                (strip->pan < 0 ? L"L " : L"R ") + Percent(std::abs(strip->pan));
            else cells[2] = L"—";
            const auto peak = strip->meterDb.empty() ? strip->peakDb : *std::max_element(strip->meterDb.begin(), strip->meterDb.end());
            cells[3] = peak <= -100 ? L"—" : std::to_wstring(static_cast<int>(peak)) + L" dBFS";
            if (strip->role == AudioStripRole::MasterOutput) cells[4] += L"MASTER ";
            if (strip->isDefault) cells[4] += L"DEFAULT ";
            if (strip->muted) cells[4] += L"MUTE ";
            if (strip->soloed) cells[4] += L"SOLO ";
            if (surface_.Selected() && surface_.Selected()->key == strip->key) cells[4] += L"SEL";
        }
        for (int column = 1; column <= 5; ++column)
        {
            wchar_t old[1024]{}; ListView_GetItemText(list, i, column, old, 1024);
            if (cells[column - 1] != old) ListView_SetItemText(list, i, column, cells[column - 1].data());
        }
        if (rowKeys_[i] == selected) ListView_SetItemState(list, i, LVIS_SELECTED, LVIS_SELECTED);
    }
    auto bank = L"硬件 CH " + std::to_wstring(surface_.BankStart() + 1) + L"–" + std::to_wstring(surface_.BankStart() + 8) +
        L"  /  " + std::to_wstring(order.size()) + L" 个逻辑位置";
    SetDlgItemTextW(window_, BankLabel, bank.c_str());
    SetDlgItemTextW(window_, VolumeKnobs, surface_.VolumeKnobs() ? L"旋钮：Volume" : (surface_.Flipped() ? L"旋钮：Volume / Flip" : L"旋钮：Pan"));
    auto state = (midi_.Connected() ? L"● 已连接   " : L"○ MIDI 未连接   ") + status_;
    state += L"\nRX " + std::to_wstring(midi_.Received) + L"  TX " + std::to_wstring(midi_.Sent) + L"  Errors " + std::to_wstring(midi_.Errors);
    if (frame_) state += frame_->monoAudioEnabled ? L"  ·  MONO" : L"  ·  STEREO";
    SetDlgItemTextW(window_, State, state.c_str());
    renderGuard_ = false;
    FillRoutes();
}
void MackieApplication::FillRoutes()
{
    const auto key = RowKey(); const auto strip = Find(key);
    const bool application = strip && strip->role == AudioStripRole::Application;
    const auto same = [](const auto& a, const auto& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) { return x.id == y.id && x.name == y.name; }); };
    auto outputs = frame_ ? frame_->outputRoutes : std::vector<AudioRouteOption>{};
    auto inputs = frame_ ? frame_->inputRoutes : std::vector<AudioRouteOption>{};
    outputs.insert(outputs.begin(), {L"", L"跟随系统默认输出"}); inputs.insert(inputs.begin(), {L"", L"跟随系统默认输入"});
    const std::wstring actualOutput = strip ? strip->outputRouteId : L"", actualInput = strip ? strip->inputRouteId : L"";
    if (frame_ && (routeKey_ != key || !same(outputRoutes_, outputs) || !same(inputRoutes_, inputs) ||
        observedOutputRoute_ != actualOutput || observedInputRoute_ != actualInput))
    {
        routeKey_ = key; outputRoutes_ = std::move(outputs); inputRoutes_ = std::move(inputs);
        observedOutputRoute_ = actualOutput; observedInputRoute_ = actualInput;
        const auto fill = [&](int id, const auto& routes, const std::wstring& current) {
            SendDlgItemMessageW(window_, id, CB_RESETCONTENT, 0, 0);
            int selected = 0;
            for (std::size_t i = 0; i < routes.size(); ++i)
            { SendDlgItemMessageW(window_, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(routes[i].name.c_str())); if (routes[i].id == current) selected = static_cast<int>(i); }
            SendDlgItemMessageW(window_, id, CB_SETCURSEL, selected, 0);
        };
        fill(RouteOut, outputRoutes_, strip ? strip->outputRouteId : L""); fill(RouteIn, inputRoutes_, strip ? strip->inputRouteId : L"");
    }
    for (int id : {RouteOut, RouteIn, ApplyOut, ApplyIn}) EnableWindow(GetDlgItem(window_, id), application);
}
void MackieApplication::FillCommands()
{
    const auto category = Text(GetDlgItem(window_, Category));
    SendDlgItemMessageW(window_, CommandChoice, CB_RESETCONTENT, 0, 0);
    for (const auto& command : MackieCommands) if (category == command.category)
    {
        const auto index = SendDlgItemMessageW(window_, CommandChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(command.label));
        SendDlgItemMessageW(window_, CommandChoice, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(&command));
    }
    SendDlgItemMessageW(window_, CommandChoice, CB_SETCURSEL, 0, 0);
}
void MackieApplication::FillBindings()
{
    SendDlgItemMessageW(window_, BindingList, LB_RESETCONTENT, 0, 0);
    for (const auto& [key, id] : settings_.bindings)
    {
        const auto command = FindMackieCommand(id);
        auto text = L"MIDI " + std::to_wstring(key / 128 + 1) + L" / Note " + std::to_wstring(key % 128) + L"  →  " + (command ? command->label : id);
        const auto row = SendDlgItemMessageW(window_, BindingList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        SendDlgItemMessageW(window_, BindingList, LB_SETITEMDATA, row, key);
    }
}
void MackieApplication::ExecuteCommand(const std::wstring& id)
{
    const auto command = FindMackieCommand(id);
    if (!command || !audio_ || smoke_ || closing_) return;
    if (command->special == 1) audio_->QueueToggleMonoAudio();
    else if (command->special == 2) audio_->QueueClearSolo();
    else
    {
        std::scoped_lock lock(commandMutex_);
        if (commandQueue_.size() < 64) commandQueue_.push_back([value = command->command] { return WindowsCommandExecutor::Execute(value); });
        commandWake_.notify_one();
    }
    Status(L"执行：" + std::wstring(command->label));
}
void MackieApplication::CommandLoop()
{
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MSG message{}; PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // Create the input queue before AttachThreadInput.
    for (;;)
    {
        std::function<bool()> command;
        {
            std::unique_lock lock(commandMutex_);
            commandWake_.wait(lock, [&] { return !commandRunning_ || !commandQueue_.empty(); });
            if (!commandRunning_) break;
            command = commandQueue_.front(); commandQueue_.pop_front();
        }
        bool okay = false;
        try { okay = command(); }
        catch (const std::exception& error) { FB_TRACE("MACKIE_COMMAND_EXCEPTION %s", error.what()); }
        PostMessageW(window_, CommandResult, okay, 0);
    }
    if (SUCCEEDED(com)) CoUninitialize();
}
void MackieApplication::Save()
{
    if (smoke_) return;
    settings_.trackOrder = surface_.Order();
    dirtySettings_ = !settings_.Save();
    if (dirtySettings_) Status(L"配置保存失败，请检查用户配置目录权限。");
}
void MackieApplication::Status(const std::wstring& text) { status_ = text; FB_TRACE("MACKIE_STATUS %ls", text.c_str()); }
void MackieApplication::Command(int id, int notification)
{
    switch (id)
    {
    case Refresh: RefreshPorts(); break;
    case Connection: if (midi_.Connected()) { Disconnect(); Status(L"设备已断开。Windows 音频状态保留。"); } else Connect(); break;
    case Touch: settings_.touch = Checked(window_, Touch); surface_.RequireTouch = settings_.touch; Save(); break;
    case Lcd: settings_.lcd = Checked(window_, Lcd); surface_.LcdEnabled = settings_.lcd; Save(); break;
    case Meters: settings_.meters = Checked(window_, Meters); surface_.MetersEnabled = settings_.meters; Save(); break;
    case Trace: midi_.Trace = Checked(window_, Trace); break;
    case PreviousBank: surface_.Bank(-8); Status(surface_.Status()); break;
    case NextBank: surface_.Bank(8); Status(surface_.Status()); break;
    case VolumeKnobs: surface_.SetEncoderVolume(!surface_.VolumeKnobs()); break;
    case SelectTrack: if (!surface_.Select(RowKey())) Status(L"通道离线或推子仍被触摸，无法选择。"); break;
    case Focus: surface_.Select(RowKey(), true); break;
    case ClearSolo: ExecuteCommand(L"ClearSolo"); break;
    case Mono: ExecuteCommand(L"MonoAudio"); break;
    case Compact:
        if (midi_.Connected()) { Status(L"请先断开 MIDI，再清理离线空位；避免已分配通道在操作中移动。"); break; }
        { std::vector<std::wstring> active; for (const auto& key : surface_.Order()) if (Find(key)) active.push_back(key);
          surface_.RestoreOrder(active); Save(); Status(L"离线位置已清理；当前在线设备和应用的相对顺序不变。"); }
        break;
    case Category: if (notification == CBN_SELCHANGE) FillCommands(); break;
    case Learn:
        if (learning_) { learning_ = false; SetDlgItemTextW(window_, Learn, L"学习按键"); Status(L"学习已取消。"); break; }
        if (!midi_.Connected()) { Status(L"请先连接 MIDI 设备。"); break; }
        if (surface_.Touched()) { Status(L"先松开推子，再开始学习。"); break; }
        { const auto i = Choice(window_, CommandChoice); if (i < 0) break;
          const auto* command = reinterpret_cast<const MackieCommand*>(SendDlgItemMessageW(window_, CommandChoice, CB_GETITEMDATA, i, 0));
          learningCommand_ = command->id; learning_ = true; SetDlgItemTextW(window_, Learn, L"取消学习");
          Status(L"请按要分配的设备按键。此按键本次不会执行任何命令。"); }
        break;
    case RemoveBinding:
        if (surface_.Touched()) { Status(L"先松开推子，再更改按键分配。"); break; }
        { const auto row = SendDlgItemMessageW(window_, BindingList, LB_GETCURSEL, 0, 0); if (row == LB_ERR) break;
          const int key = static_cast<int>(SendDlgItemMessageW(window_, BindingList, LB_GETITEMDATA, row, 0));
          midi_.Send({static_cast<std::uint8_t>(0x90 | key / 128), static_cast<std::uint8_t>(key % 128), 0});
          settings_.bindings.erase(key); customFeedback_.erase(key); surface_.InvalidateFeedback(); FillBindings(); Save(); }
        break;
    case ApplyOut: case ApplyIn:
        { const bool capture = id == ApplyIn; const int index = Choice(window_, capture ? RouteIn : RouteOut);
          const auto& routes = capture ? inputRoutes_ : outputRoutes_; const auto key = RowKey();
          if (index >= 0 && index < static_cast<int>(routes.size()) && key == routeKey_ && audio_)
          { audio_->QueueApplicationRoute(key, capture, routes[index].id); Status(L"应用设备路由请求已发送；是否即时生效还取决于应用。"); } }
        break;
    case Diagnostics:
        { const auto folder = std::filesystem::path(DiagnosticLog::Instance().Path()).parent_path(); ShellExecuteW(window_, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); } break;
    case Hide: if (tray_.hWnd) ShowWindow(window_, SW_HIDE); else Status(L"托盘不可用，窗口保持可见。"); break;
    case ExitApp: Quit(); break;
    }
}
void MackieApplication::TrayMenu()
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"打开 Mackie Control 控制面板");
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 2, midi_.Connected() ? L"状态：MIDI 已连接" : L"状态：MIDI 未连接");
    AppendMenuW(menu, MF_STRING, Diagnostics, L"打开诊断目录");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ExitApp, L"退出 Mackie 版（不影响 EUCON）");
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(window_);
    const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command == 1) { ShowWindow(window_, SW_RESTORE); SetForegroundWindow(window_); }
    else if (command) Command(command, 0);
}
void MackieApplication::Quit()
{
    if (closing_) return;
    closing_ = true; KillTimer(window_, 1); Save(); Disconnect();
    audio_.reset(); media_.reset();
    { std::scoped_lock lock(commandMutex_); commandRunning_ = false; commandQueue_.clear(); }
    commandWake_.notify_all(); if (commandWorker_.joinable()) commandWorker_.join();
    MSG message{};
    while (PeekMessageW(&message, window_, AudioMessage, AudioMessage, PM_REMOVE)) delete reinterpret_cast<AudioFrame*>(message.lParam);
    if (tray_.hWnd) Shell_NotifyIconW(NIM_DELETE, &tray_);
    DestroyWindow(window_);
}
LRESULT CALLBACK MackieApplication::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto self = reinterpret_cast<MackieApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<MackieApplication*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->Message(message, w, l) : DefWindowProcW(window, message, w, l);
}
LRESULT MackieApplication::Message(UINT message, WPARAM w, LPARAM l)
{
    switch (message)
    {
    case AudioMessage: OnFrame(std::unique_ptr<AudioFrame>(reinterpret_cast<AudioFrame*>(l))); return 0;
    case WM_TIMER: Tick(); return 0;
    case WM_COMMAND: Command(LOWORD(w), HIWORD(w)); return 0;
    case CommandResult: if (!w) Status(L"Windows 命令执行失败；请查看日志或目标窗口的权限。"); return 0;
    case WM_NOTIFY:
        if (const auto header = reinterpret_cast<NMHDR*>(l); header->idFrom == Tracks && !renderGuard_)
        { if (header->code == LVN_ITEMCHANGED) FillRoutes(); else if (header->code == NM_DBLCLK) surface_.Select(RowKey(), true); }
        break;
    case WM_DEVICECHANGE:
        if (midi_.Connected())
        {
            const auto ins = WinMidiPort::Inputs(), outs = WinMidiPort::Outputs();
            const auto same = [](const auto& ports, UINT index, const std::wstring& name) { return std::any_of(ports.begin(), ports.end(), [&](const auto& p) { return p.index == index && p.name == name; }); };
            if (!same(ins, selectedInput_, settings_.input) || !same(outs, selectedOutput_, settings_.output))
            { Disconnect(); Status(L"设备列表发生变化，MIDI 已断开；请确认端口后手动重连。"); }
        }
        return 0;
    case TrayMessage: if (l == WM_RBUTTONUP) TrayMenu(); else if (l == WM_LBUTTONDBLCLK) { ShowWindow(window_, SW_RESTORE); SetForegroundWindow(window_); } return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(w), RGB(222, 233, 245)); SetBkColor(reinterpret_cast<HDC>(w), RGB(22, 27, 35));
        return reinterpret_cast<LRESULT>(background_);
    case WM_CLOSE: if (tray_.hWnd && !smoke_) ShowWindow(window_, SW_HIDE); else Quit(); return 0;
    case WM_ENDSESSION: if (w) Quit(); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window_, message, w, l);
}
