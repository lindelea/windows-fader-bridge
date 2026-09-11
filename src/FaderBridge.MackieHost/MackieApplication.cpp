#include "MackieApplication.h"
#include "MackieDesktop.h"
#include "CommandCatalog.h"
#include "DiagnosticLog.h"
#include "WindowsCommandExecutor.h"
#include "MackieJogInput.h"
#include <Dwmapi.h>
#include <UxTheme.h>
#include <AppModel.h>
#include <set>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace
{
constexpr UINT AudioMessage = WM_APP + 10, TrayMessage = WM_APP + 11, CommandResult = WM_APP + 12;
constexpr int RestartApp = 1901;
enum Id { Input = 101, Output, Profile, Refresh, Connection, Touch, Lcd, Meters, Trace,
    Tracks, PreviousBank, NextBank, BankLabel,
    Category, CommandChoice, Learn, BindingList, RemoveBinding, State, MediaLabel, Hint,
    Hide, Diagnostics, ExitApp, WindowsPreset, EncoderSettings, EncoderTarget, JogSettingsButton, JogStatus };
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

MackieApplication::DeviceContext::DeviceContext(MackieApplication& app,std::string identity,MackieSettings config)
    : id(std::move(identity)), settings(std::move(config)),
      midi([this,&app](DWORD raw){DeviceScope scope(app,this);app.OnMidi(raw);}),
      surface([this](const mackie::Bytes& bytes){
          if(bytes.size()==3&&bytes[0]==0x90&&settings.bindings.count(bytes[1]))return;
          midi.Send(bytes);
      },[this,&app](const mackie::Action& action){DeviceScope scope(app,this);app.OnAction(action);})
{
    surface.RestoreOrder(settings.trackOrder);
    surface.Jog.settings = settings.jog;
    surface.CursorGestures.ticks = settings.cursorTicks;
    for (int direction = 0; direction < 2; ++direction)
        surface.JogSeekDirections[direction] = mackie::JogSeekDirection(settings.cursorCommands[4][direction]);
    surface.RequireTouch=settings.touch;surface.LcdEnabled=settings.lcd;surface.MetersEnabled=settings.meters;
}
MackieApplication::MackieApplication(bool smoke,bool diagnostics)
    : workspace_(MackieWorkspace::Load()),smoke_(smoke),diagnostics_(diagnostics)
{
    emptyDevice_=std::make_unique<DeviceContext>(*this,"",MackieSettings{});
    for(auto& id:workspace_.devices)
    {
        auto device=std::make_unique<DeviceContext>(*this,id,MackieSettings::Load(workspace_.DevicePath(id)));
        if(id==workspace_.selected)selectedDevice_=device.get();
        devices_.push_back(std::move(device));
    }
    if(!selectedDevice_&&!devices_.empty())selectedDevice_=devices_.front().get();
    FB_TRACE("MACKIE_STARTUP phase=settings_loaded");
}
MackieApplication::~MackieApplication()
{
    desktop_.reset();
    for(auto& device:devices_){device->midi.Close();device->media.reset();}
    audio_.reset();emptyDevice_->media.reset();
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
    window_ = CreateWindowExW(0, wc.lpszClassName, L"Windows Fader Bridge — Mackie Diagnostics",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, this);
    if (!window_) return 2;
    if (!smoke_ && !globalShortcut_.Apply(window_, 1, true,
        {workspace_.shortcutModifiers, workspace_.shortcutKey}))
        FB_TRACE("MACKIE_GLOBAL_SHORTCUT_UNAVAILABLE modifiers=%u key=%u",
                 workspace_.shortcutModifiers, workspace_.shortcutKey);
    FB_TRACE("MACKIE_STARTUP phase=window_created");
    BOOL dark = TRUE; DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
    CreateControls();
    FB_TRACE("MACKIE_STARTUP phase=controls_created");
    RefreshPorts();
    FB_TRACE("MACKIE_STARTUP phase=ports_enumerated");
    audio_ = std::make_unique<NativeAudioController>(window_, AudioMessage);
    for(auto& device:devices_)device->media=std::make_unique<MackieMedia>();
    emptyDevice_->media=std::make_unique<MackieMedia>();
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
        desktop_ = std::make_unique<MackieDesktop>(*this);
        if (!desktop_->Create()) { desktop_.reset(); ShowWindow(window_, SW_SHOW); }
        else if (show != SW_HIDE || !tray_.hWnd) desktop_->Show();
        if(diagnostics_)ShowWindow(window_,SW_SHOW);
    }
    Status(L"Mackie Control 已启动。");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (desktop_ && desktop_->Window() && IsDialogMessageW(desktop_->Window(), &message)) continue;
        if (encoderWindow_ && IsDialogMessageW(encoderWindow_, &message)) continue;
        if (jogWindow_ && IsDialogMessageW(jogWindow_, &message)) continue;
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
    Control(L"STATIC", L"Mackie Control · 诊断面板 / Diagnostic dashboard", 0, 0, 24, 53, 970, 22);
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
    SendMessageW(profile, CB_SETCURSEL, Settings().profile == L"p1-nano" ? 1 : 0, 0);
    Control(L"BUTTON", L"触摸保护", BS_AUTOCHECKBOX | WS_TABSTOP, Touch, 308, 153, 110, 25);
    Control(L"BUTTON", L"LCD", BS_AUTOCHECKBOX | WS_TABSTOP, Lcd, 435, 153, 70, 25);
    Control(L"BUTTON", L"峰值表", BS_AUTOCHECKBOX | WS_TABSTOP, Meters, 521, 153, 90, 25);
    Control(L"BUTTON", L"MIDI 日志", BS_AUTOCHECKBOX | WS_TABSTOP, Trace, 628, 153, 150, 25);
    SetCheck(window_, Touch, Settings().touch); SetCheck(window_, Lcd, Settings().lcd); SetCheck(window_, Meters, Settings().meters);
    // The system light theme draws black checkbox captions even on a dark
    // parent. Classic checkbox painting honors our explicit text colors.
    for (int id : {Touch, Lcd, Meters, Trace}) SetWindowTheme(GetDlgItem(window_, id), L"", L"");
    Control(L"BUTTON", L"连接设备", button, Connection, 836, 149, 180, 32);
    Control(L"STATIC", L"", SS_LEFT, State, 24, 194, 992, 38);
    Control(L"BUTTON", L"◀ 上一组", button, PreviousBank, 24, 239, 100, 28);
    Control(L"BUTTON", L"下一组 ▶", button, NextBank, 136, 239, 100, 28);
    Control(L"STATIC", L"", 0, BankLabel, 251, 244, 355, 22);
    Control(L"STATIC", L"", SS_RIGHT | SS_ENDELLIPSIS, EncoderTarget, 620, 244, 396, 22);
    auto tracks = Control(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, Tracks, 24, 280, 992, 246);
    SetWindowTheme(tracks, L"DarkMode_Explorer", nullptr);
    ListView_SetExtendedListViewStyle(tracks, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(tracks, RGB(29, 35, 45)); ListView_SetTextBkColor(tracks, RGB(29, 35, 45)); ListView_SetTextColor(tracks, RGB(229, 236, 245));
    const wchar_t* names[] = {L"CH", L"Windows 通道（完整名称）", L"音量", L"Pan", L"峰值", L"状态"};
    const int widths[] = {55, 463, 72, 65, 85, 224};
    for (int i = 0; i < 6; ++i) { LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = const_cast<wchar_t*>(names[i]); c.cx = MulDiv(widths[i], dpi, 96); ListView_InsertColumn(tracks, i, &c); }
    Control(L"STATIC", L"", SS_LEFT, MediaLabel, 24, 538, 992, 38);
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
    Control(L"BUTTON", L"一键应用 Windows 80 键预设", button, WindowsPreset, 642, 665, 374, 30);
    Control(L"STATIC", L"配套触屏预设：MIDI 通道 16 / Note 0–79。\n保留原分配，冲突时不覆盖；先断开 MIDI。\n不更改推子、触摸、分页或设备设置。", 0, Hint, 642, 703, 374, 58);
    Control(L"BUTTON", L"诊断目录", button, Diagnostics, 640, 768, 112, 28);
    Control(L"BUTTON", L"旋钮 3–8 分配…", button, EncoderSettings, 24, 768, 185, 28);
    Control(L"BUTTON", L"Jog 设置…", button, JogSettingsButton, 224, 768, 125, 28);
    Control(L"STATIC", L"", SS_ENDELLIPSIS, JogStatus, 360, 774, 266, 22);
    Control(L"BUTTON", L"后台运行", button, Hide, 767, 768, 112, 28);
    Control(L"BUTTON", L"退出", button, ExitApp, 894, 768, 122, 28);
    FillCommands(); FillBindings();
}
void MackieApplication::RefreshPorts()
{
    if (Midi().Connected()) return;
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
    fill(Input, inputs_, Settings().input); fill(Output, outputs_, Settings().output);
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
    if(!SaveDeviceConfiguration(Settings().deviceName,input.name,output.name,Choice(window_,Profile)==1?L"p1-nano":L"mcu"))
    {Status(L"无法保存设备：请检查端口冲突或配置目录。");return;}
    OpenDevice(input.index,output.index);
}
void MackieApplication::Disconnect(bool manual)
{
    if(manual)Device().connection.ManualDisconnect();
    Learning() = false;
    Midi().Close(); Surface().ResetConnection(); CustomPressed().fill(false); CustomFeedback().clear();
    SyncDeviceControls();
}
void MackieApplication::OnMidi(DWORD raw)
{
    if (!mackie::ValidShort(raw)) return;
    if (desktop_ && desktop_->Midi(raw)) return;
    const int type = raw & 0xF0, channel = raw & 15, note = (raw >> 8) & 127;
    if (IsSelectedDevice() && jogWindow_ && channel == 0 && type == 0xB0 && note == 0x3C)
    {
        const int delta = mackie::Delta((raw >> 16) & 127);
        if (delta) SetDlgItemTextW(jogWindow_, 302, delta > 0 ?
            L"检测到：普通 Jog 右转（编辑期间不执行）" : L"检测到：普通 Jog 左转（编辑期间不执行）");
        return;
    }
    // Continue tracking the Zoom switch while editing, without executing any
    // rotation. Otherwise a device-local mode change could desynchronize it.
    if (IsSelectedDevice() && encoderWindow_ && channel == 0 && ((type == 0xB0 && note >= 0x10 && note <= 0x17) ||
        ((type == 0x90 || type == 0x80) && note >= 0x20 && note <= 0x27))) return;
    if (type == 0x90 || type == 0x80)
    {
        // Cursor/Zoom belong to their dedicated four-axis map. Even during
        // learning, track the real Zoom switch; never let an old binding eat it.
        if (channel == 0 && note >= 0x60 && note <= 0x65)
        {
            Surface().Input(raw, GetTickCount64());
            if (IsSelectedDevice() && jogWindow_ && note == 0x64)
                SendDlgItemMessageW(jogWindow_, 301, CB_SETCURSEL, Surface().CursorZoom() ? 1 : 0, 0);
            if (Learning()) Status(L"Move / Zoom 请在 Jog 设置中分配方向；Navi / Focus 保持原机行为。");
            FB_TRACE("MACKIE_CURSOR_INPUT note=%02X down=%d zoom=%d", note,
                type == 0x90 && ((raw >> 16) & 127) != 0, Surface().CursorZoom());
            return;
        }
        // Touch is never consumed by learning or command dispatch. This must
        // precede the protected-note rejection, including during Learn mode.
        if (channel == 0 && note >= 0x68 && note <= 0x70) { Surface().Input(raw, GetTickCount64()); return; }
        const bool down = type == 0x90 && ((raw >> 16) & 127) != 0;
        const int key = channel * 128 + note;
        const bool previous = CustomPressed()[key]; CustomPressed()[key] = down;
        if (Learning())
        {
            if (down && !previous)
            {
                if (!MackieSettings::Bindable(channel, note)) { Status(L"这是核心 MCU 按键，不能覆盖。请选择 F 键 / 功能键，或 MIDI 通道 2–16 的 Note。"); return; }
                Settings().bindings[key] = LearningCommand(); Learning() = false;
                CustomFeedback().clear(); SetDlgItemTextW(window_, Learn, L"学习按键");
                FillBindings(); Save(); Status(L"分配已保存。本次学习按键不会执行命令。");
            }
            // Keep core touch releases alive while learning, otherwise a motor
            // could stay indefinitely suppressed after the user lets go.
            if (channel == 0 && !down) Surface().Input(raw, GetTickCount64());
            return;
        }
        const auto binding = Settings().bindings.find(key);
        if (binding != Settings().bindings.end())
        {
            if (down && !previous) ExecuteCommand(binding->second);
            return;
        }
    }
    if (!Learning()) Surface().Input(raw, GetTickCount64());
}
const AudioStripState* MackieApplication::Find(const std::wstring& key) const
{
    if (frame_) for (const auto& strip : frame_->strips) if (strip.active && strip.key == key) return &strip;
    return nullptr;
}
void MackieApplication::OnAction(const mackie::Action& action)
{
    if (!audio_ || closing_ || smoke_) return;
    if (desktop_ && desktop_->Preview(action)) return;
    using K = mackie::ActionKind;
    if (action.kind == K::CursorCommand)
    {
        if (!mackie::ValidCursorAxis(action.encoder) ||
            (action.value != -1.F && action.value != 1.F)) return;
        const int direction = action.value > 0 ? 1 : 0;
        const auto& id = Settings().cursorCommands[action.encoder][direction];
        const auto label = std::wstring(mackie::CursorLabels[action.encoder]) +
            (action.encoder < 4 && action.encoder % 2 == 0 ? (direction ? L" ↓" : L" ↑") : (direction ? L" →" : L" ←"));
        if (IsSelectedDevice() && jogWindow_)
        {
            SetDlgItemTextW(jogWindow_, 302, (L"检测到：" + label + L"（编辑期间不执行；请保存或取消后测试）").c_str());
            return;
        }
        if (Learning() || (IsSelectedDevice() && encoderWindow_)) return;
        FB_TRACE("MACKIE_CURSOR_ACTION axis=%d direction=%d command=%ls", action.encoder, direction, id.c_str());
        if (id.empty()) { Status(L"收到 " + label + L"：未分配，请打开 Jog 设置。"); return; }
        if (!mackie::ValidCursorCommand(id)) { Status(L"方向命令无效，请重新分配。"); return; }
        const auto foreground = GetForegroundWindow();
        const auto sampled = GetTickCount64();
        std::scoped_lock lock(commandMutex_);
        if (commandQueue_.size() >= 4) { Status(L"方向命令正忙，本次转动未排队。"); return; }
        commandQueue_.push_back([id, foreground, sampled] {
            if (GetTickCount64() - sampled > 150 || GetForegroundWindow() != foreground)
            { FB_TRACE("MACKIE_CURSOR_DISPATCH command=%ls skipped=expired_or_focus_changed", id.c_str()); return true; }
            bool sent = false;
            if (!mackie::CursorModifiersHeld())
            {
                if (const auto local = mackie::FindCursorCommand(id)) sent = mackie::SendCursorInput(local->input);
                else if (const auto command = FindMackieCommand(id); command && !command->special)
                    sent = WindowsCommandExecutor::Execute(command->command);
            }
            FB_TRACE("MACKIE_CURSOR_DISPATCH command=%ls sent=%d", id.c_str(), sent);
            return sent;
        });
        commandWake_.notify_one();
        const auto local = mackie::FindCursorCommand(id);
        const auto command = FindMackieCommand(id);
        Status(L"收到 " + label + L" → " + (local ? local->label : command->label) + L"（已排队，实际响应由目标软件决定）");
        return;
    }
    if (action.kind == K::Encoder)
    {
        const auto id = EncoderBinding(Settings().encoders, action.encoder, action.value);
        if (!id) return;
        if (const auto command = FindMackieChannelCommand(*id))
        {
            const auto selected = Surface().Selected();
            if (!selected || selected->key != action.key ||
                !Surface().ChannelCommand(command->kind, command->value, GetTickCount64()))
                Status(L"当前通道不支持：" + std::wstring(command->label));
        }
        else ExecuteCommand(*id);
        return;
    }
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
        if (action.strictTarget && (!strip || strip->role != AudioStripRole::Application ||
            (strip->focusExecutablePath.empty() && strip->focusPackageFamilyName.empty()))) break;
        if (strip && strip->role == AudioStripRole::Application) Media()->Target(strip->focusExecutablePath, strip->focusPackageFamilyName);
        else Media()->Target({}, {}); // Endpoint strips follow Windows' current player.
        Media()->Request(action.kind, action.value, !action.strictTarget); accepted = true; break;
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
    emptyDevice_->surface.Update(tracks,GetTickCount64());emptyDevice_->surface.AnySolo=frame_->anySolo;
    for(auto& device:devices_)
    {
        DeviceScope scope(*this,device.get());
        Surface().DeferTopology=std::any_of(devices_.begin(),devices_.end(),[&](const auto& other){return other.get()!=device.get()&&other->surface.Touched();});
        Surface().Update(tracks,GetTickCount64());Surface().AnySolo=frame_->anySolo;
        if(Settings().trackOrder!=Surface().Order()){Settings().trackOrder=Surface().Order();DirtySettings()=true;}
    }
    // Meter paint is asynchronous and contains no list/model reconciliation.
    if (desktop_) desktop_->AudioFrameChanged();
}
void MackieApplication::Tick()
{
    const auto now = GetTickCount64();
    if (smoke_ && now - started_ >= 6000)
    {
        const auto active = frame_ ? std::count_if(frame_->strips.begin(), frame_->strips.end(), [](const auto& s) { return s.active; }) : 0;
        const auto sent = std::accumulate(devices_.begin(),devices_.end(),emptyDevice_->midi.Sent.load(),
            [](std::uint64_t total,const auto& device){return total+device->midi.Sent.load();});
        const bool disconnected = !emptyDevice_->midi.Connected() && std::none_of(devices_.begin(),devices_.end(),
            [](const auto& device){return device->midi.Connected();});
        const bool okay = audio_ && audio_->IsReady() && active > 0 && disconnected && sent == 0;
        FB_TRACE("MACKIE_SMOKE result=%s tracks=%zu midi_inputs=%zu midi_outputs=%zu midi_sent=%llu",
            okay ? "PASS" : "FAIL", static_cast<std::size_t>(active), inputs_.size(), outputs_.size(), sent);
        Quit(); PostQuitMessage(okay ? 0 : 3); return;
    }
    ReconcileDevices();
    for(auto& device:devices_){DeviceScope scope(*this,device.get());TickDevice(now);}
    if(devices_.empty())TickDevice(now);
}
void MackieApplication::TickDevice(std::uint64_t now)
{
    const auto selected = Surface().Selected();
    const auto strip = selected ? Find(selected->key) : nullptr;
    if (strip && strip->role == AudioStripRole::Application)
        Media()->Target(strip->focusExecutablePath, strip->focusPackageFamilyName);
    else Media()->Target({}, {});
    const auto media = Media()->State(MediaStatus());
    const auto time = PlaybackTime(media.timeline, media.playing);
    SYSTEMTIME local{};
    GetLocalTime(&local);
    const auto display = DisplayClock().Update(media, now, local.wHour * 3600 + local.wMinute * 60 + local.wSecond);
    Surface().MediaAvailable = media.available; Surface().MediaPlaying = media.playing; Surface().MediaRepeat = media.repeatMode != 0;
    Surface().TimeDisplaySeconds = display.seconds;
    if (Midi().Connected())
    {
        Midi().Collect(); Surface().Feedback(now);
        for (const auto& [key, id] : Settings().bindings)
        {
            // The manufacturer marks these MCU controls as switch-only.
            // Other MIDI channels remain user-defined, as with the touch preset.
            if (key >= 0x60 && key <= 0x67 && key != 0x64 && key != 0x65) continue;
            const auto command = FindMackieCommand(id);
            bool on = CustomPressed()[key];
            if (command && command->special == 1) on = frame_ && frame_->monoAudioEnabled;
            else if (command && command->special == 2) on = frame_ && frame_->anySolo;
            if (!CustomFeedback().count(key) || CustomFeedback()[key] != on)
            {
                Midi().Send({static_cast<std::uint8_t>(0x90 | key / 128), static_cast<std::uint8_t>(key % 128), static_cast<std::uint8_t>(on ? 127 : 0)});
                CustomFeedback()[key] = on;
            }
        }
        if (Midi().Faulted()) { const auto error = Midi().Error(); Disconnect(false);Device().connection.Failed(now);Status(L"MIDI 已断开：" + error); }
    }
    if (IsSelectedDevice() && now >= renderDue_)
    {
        renderDue_ = now + 250; Render();
        if (desktop_) desktop_->Tick();
        if (media.available)
        {
            auto text = (media.playing ? L"▶ " : L"Ⅱ ") + media.title;
            if (!media.artist.empty()) text += L" — " + media.artist;
            if (time.available) text += L"  " + mackie::TimeText(time.elapsedSeconds) + L" / " + mackie::TimeText(time.durationSeconds) + (media.canSeek ? L"" : L"（只读）");
            else if (media.hasPosition) text += L"  " + Percent(media.position) + (media.canSeek ? L"" : L"（只读）");
            text += std::wstring(L"\n数字屏：") + (display.systemClock ? L"系统时间 " : L"歌曲进度 ") + mackie::TimeText(display.seconds);
            if (!MediaStatus().empty()) text += L"  ·  " + MediaStatus();
            SetDlgItemTextW(window_, MediaLabel, text.c_str());
        }
        else SetDlgItemTextW(window_, MediaLabel, (L"数字屏：系统时间 " + mackie::TimeText(display.seconds) +
            L"  ·  当前没有可读取的媒体状态" + (MediaStatus().empty() ? L"" : L"\n" + MediaStatus())).c_str());
    }
    // Releasing the last touched fader can compact the order between frames.
    if (Settings().trackOrder != Surface().Order()) { Settings().trackOrder = Surface().Order(); DirtySettings() = true; }
    if (DirtySettings() && now >= SaveDue()) { SaveDue() = now + 2000; Save(); }
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
    const auto current = Surface().Selected();
    const auto selected = current ? current->key : L"";
    const auto& order = Surface().Order();
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
        std::wstring cells[5]{strip ? strip->name : L"（已离线 · 松开推子后自动整理）", L"", L"", L"", L""};
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
            if (Surface().Selected() && Surface().Selected()->key == strip->key) cells[4] += L"SEL";
        }
        for (int column = 1; column <= 5; ++column)
        {
            wchar_t old[1024]{}; ListView_GetItemText(list, i, column, old, 1024);
            if (cells[column - 1] != old) ListView_SetItemText(list, i, column, cells[column - 1].data());
        }
        ListView_SetItemState(list, i, rowKeys_[i] == selected ? LVIS_SELECTED : 0, LVIS_SELECTED);
    }
    auto bank = L"硬件 CH " + std::to_wstring(Surface().BankStart() + 1) + L"–" + std::to_wstring(Surface().BankStart() + 8) +
        L"  /  " + std::to_wstring(order.size()) + L" 个逻辑位置";
    SetDlgItemTextW(window_, BankLabel, bank.c_str());
    SetDlgItemTextW(window_, EncoderTarget, current ? (L"旋钮目标：" + current->name).c_str() : L"旋钮目标：无在线通道");
    SetDlgItemTextW(window_, JogStatus, Surface().CursorZoom() ?
        L"Jog：独立分配 · 方向层：Zoom" : L"Jog：独立分配 · 方向层：Move");
    auto state = (Midi().Connected() ? L"● 已连接   " : L"○ MIDI 未连接   ") + status_;
    state += L"\nRX " + std::to_wstring(Midi().Received.load()) + L"  TX " + std::to_wstring(Midi().Sent.load()) + L"  Errors " + std::to_wstring(Midi().Errors.load());
    if (frame_) state += frame_->monoAudioEnabled ? L"  ·  MONO" : L"  ·  STEREO";
    SetDlgItemTextW(window_, State, state.c_str());
    renderGuard_ = false;
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
    for (const auto& [key, id] : Settings().bindings)
    {
        const auto command = FindMackieCommand(id);
        auto text = L"MIDI " + std::to_wstring(key / 128 + 1) + L" / Note " + std::to_wstring(key % 128) + L"  →  " + (command ? command->label : id);
        const auto row = SendDlgItemMessageW(window_, BindingList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        SendDlgItemMessageW(window_, BindingList, LB_SETITEMDATA, row, key);
    }
}
void MackieApplication::ShowEncoderSettings()
{
    if (jogWindow_) { SetForegroundWindow(jogWindow_); return; }
    if (encoderWindow_) { ShowWindow(encoderWindow_, SW_RESTORE); SetForegroundWindow(encoderWindow_); return; }
    if (Surface().Touched() || Learning()) { Status(L"请先松开推子并结束按键学习，再编辑旋钮。"); return; }
    WNDCLASSW wc{}; wc.hInstance = instance_; wc.lpfnWndProc = EncoderWindowProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = background_;
    wc.lpszClassName = L"WindowsFaderBridge.Mackie.Encoders";
    RegisterClassW(&wc);
    const int dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(1030, dpi, 96), MulDiv(520, dpi, 96)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&bounds, style, FALSE, WS_EX_CONTROLPARENT);
    RECT owner{}; GetWindowRect(window_, &owner);
    encoderWindow_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"当前通道旋钮 · 自由分配",
        style, owner.left, owner.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        window_, nullptr, instance_, this);
    if (!encoderWindow_) { Status(L"无法打开旋钮设置。"); return; }
    Surface().ResetEncoderInput();
    const auto control = [&](const wchar_t* type, const wchar_t* text, DWORD flags, int id, int x, int y, int w, int h) {
        const HWND c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | flags,
            MulDiv(x, dpi, 96), MulDiv(y, dpi, 96), MulDiv(w, dpi, 96), MulDiv(h, dpi, 96),
            encoderWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE); return c;
    };
    control(L"STATIC", L"旋钮 1 固定音量；旋钮 2 固定 Pan，按下回中。其余旋钮可分别设置左转、右转和按下。", 0, 0, 24, 20, 982, 24);
    control(L"STATIC", L"“当前通道”命令跟随选择；其他 Windows 命令按其原有范围控制前台窗口或系统，不会自动切换前台。", 0, 0, 24, 50, 982, 40);
    const wchar_t* columns[] = {L"左转（逆时针）", L"右转（顺时针）", L"按下"};
    for (int gesture = 0; gesture < 3; ++gesture)
        control(L"STATIC", columns[gesture], 0, 0, 100 + gesture * 302, 99, 286, 24);
    for (int encoder = 0; encoder < 6; ++encoder)
    {
        control(L"STATIC", (L"旋钮 " + std::to_wstring(encoder + 3)).c_str(), 0, 0, 24, 139 + encoder * 44, 72, 24);
        for (int gesture = 0; gesture < 3; ++gesture)
        {
            const auto combo = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                200 + encoder * 3 + gesture, 100 + gesture * 302, 135 + encoder * 44, 286, 330);
            SendMessageW(combo, CB_SETDROPPEDWIDTH, MulDiv(410, dpi, 96), 0);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"— 未分配 —"));
            SendMessageW(combo, CB_SETITEMDATA, 0, reinterpret_cast<LPARAM>(L""));
            int selected = 0;
            const auto add = [&](const wchar_t* id, const std::wstring& label) {
                const auto index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                SendMessageW(combo, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(id));
                if (Settings().encoders[encoder][gesture] == id) selected = static_cast<int>(index);
            };
            for (const auto& command : MackieChannelCommands) add(command.id, L"当前通道 · " + std::wstring(command.label));
            for (const auto& command : MackieCommands) add(command.id, std::wstring(command.category) + L" · " + command.label);
            SendMessageW(combo, CB_SETCURSEL, selected, 0);
        }
    }
    control(L"STATIC", L"例如：旋钮 3 左转上一首 / 右转下一首 / 按下播放暂停；旋钮 4 左转缩小 / 右转放大 / 按下恢复缩放。\n默认均不分配。命令式旋转每条消息执行一次，最多每秒 10 次；编辑期间旋钮暂不执行，推子和触摸仍正常。", 0, 0, 24, 410, 982, 48);
    control(L"BUTTON", L"清空 3–8（保存后生效）", BS_PUSHBUTTON | WS_TABSTOP, 301, 24, 472, 245, 30);
    control(L"BUTTON", L"保存", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 750, 472, 120, 30);
    control(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 886, 472, 120, 30);
    BOOL dark = TRUE; DwmSetWindowAttribute(encoderWindow_, 20, &dark, sizeof(dark));
    ShowWindow(encoderWindow_, SW_SHOWNORMAL); SetForegroundWindow(encoderWindow_);
    SetFocus(GetDlgItem(encoderWindow_, 200));
}
void MackieApplication::SaveEncoderSettings()
{
    auto next = Settings();
    for (int encoder = 0; encoder < 6; ++encoder) for (int gesture = 0; gesture < 3; ++gesture)
    {
        const int control = 200 + encoder * 3 + gesture;
        const auto index = Choice(encoderWindow_, control);
        const auto data = SendDlgItemMessageW(encoderWindow_, control, CB_GETITEMDATA, index, 0);
        if (index < 0 || !data || data == CB_ERR) return;
        const auto id = reinterpret_cast<const wchar_t*>(data);
        if (!ValidEncoderCommand(id)) return;
        next.encoders[encoder][gesture] = id;
    }
    next.trackOrder = Surface().Order();
    if (!next.Save())
    { MessageBoxW(encoderWindow_, L"配置保存失败，原来的分配没有改变。请检查配置目录权限。", L"未保存", MB_OK | MB_ICONERROR); return; }
    Settings() = std::move(next); DirtySettings() = false;
    DestroyWindow(encoderWindow_);
    Status(L"旋钮分配已保存，重新启动仍会保留。1 音量 / 2 Pan 始终控制当前通道。");
}
LRESULT CALLBACK MackieApplication::EncoderWindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto self = reinterpret_cast<MackieApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<MackieApplication*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) switch (message)
    {
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) self->SaveEncoderSettings();
        else if (LOWORD(w) == IDCANCEL) DestroyWindow(window);
        else if (LOWORD(w) == 301) for (int id = 200; id < 218; ++id) SendDlgItemMessageW(window, id, CB_SETCURSEL, 0, 0);
        return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(w), RGB(222, 233, 245)); SetBkColor(reinterpret_cast<HDC>(w), RGB(22, 27, 35));
        return reinterpret_cast<LRESULT>(self->background_);
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        self->encoderWindow_ = nullptr; self->Surface().ResetEncoderInput(); return 0;
    }
    return DefWindowProcW(window, message, w, l);
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
void MackieApplication::ApplyWindowsPreset()
{
    if (smoke_ || Midi().Connected() || Learning() || Surface().Touched())
    { Status(L"请先断开 MIDI，再应用 80 键预设；不会自动连接或执行命令。"); return; }
    auto next = Settings();
    if (!next.ApplyWindows80Preset())
    { Status(L"MIDI 通道 16 / Note 0–79 存在其他分配：原配置未变，请先处理冲突。"); return; }
    if (next.bindings == Settings().bindings)
    { Status(L"Windows 80 键预设已经就绪，无需重复应用。"); return; }
    try
    {
        const auto path = Settings().storage;
        if (path.empty()) { Status(L"无法定位用户配置目录，原配置未变。"); return; }
        if (std::filesystem::exists(path))
        {
            auto backup = path;
            backup += L".before-windows80-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".bak";
            std::filesystem::copy_file(path, backup); // No overwrite; backup failure aborts.
        }
        next.trackOrder = Surface().Order();
        if (!next.Save(path)) { Status(L"预设保存失败，内存中的分配未变。"); return; }
        Settings() = std::move(next); DirtySettings() = false;
        CustomPressed().fill(false); CustomFeedback().clear(); FillBindings();
        Status(L"80 键已配好，旧配置已备份。导入配套触屏文件后连接即可，无需逐键学习。");
    }
    catch (const std::exception& error)
    { FB_TRACE("MACKIE_PRESET_ERROR %s", error.what()); Status(L"无法安全备份 / 保存预设，原分配未变。"); }
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
    if (smoke_ || Device().id.empty()) return;
    Settings().trackOrder = Surface().Order();
    DirtySettings() = !Settings().Save();
    if (DirtySettings()) Status(L"配置保存失败，请检查用户配置目录权限。");
}
void MackieApplication::Status(const std::wstring& text)
{
    status_ = text; FB_TRACE("MACKIE_STATUS %ls", text.c_str());
    if (desktop_ && IsSelectedDevice() && (text.find(L"失败") != std::wstring::npos || text.find(L"未被接受") != std::wstring::npos))
        desktop_->Notice(L"操作未完成，请查看日志。", L"The action could not complete. See the log.", true);
}
void MackieApplication::Command(int id, int notification)
{
    switch (id)
    {
    case Refresh: RefreshPorts(); break;
    case Connection: if (Midi().Connected()) { Disconnect(); Status(L"设备已断开。Windows 音频状态保留。"); } else Connect(); break;
    case Touch: Settings().touch = Checked(window_, Touch); Surface().RequireTouch = Settings().touch; Save(); break;
    case Lcd: Settings().lcd = Checked(window_, Lcd); Surface().LcdEnabled = Settings().lcd; Save(); break;
    case Meters: Settings().meters = Checked(window_, Meters); Surface().MetersEnabled = Settings().meters; Save(); break;
    case Trace: Midi().Trace = Checked(window_, Trace); break;
    case PreviousBank: Surface().Bank(-8); Status(Surface().Status()); break;
    case NextBank: Surface().Bank(8); Status(Surface().Status()); break;
    case Category: if (notification == CBN_SELCHANGE) FillCommands(); break;
    case WindowsPreset: ApplyWindowsPreset(); break;
    case EncoderSettings: ShowEncoderSettings(); break;
    case JogSettingsButton: ShowJogSettings(); break;
    case Learn:
        if (Learning()) { Learning() = false; SetDlgItemTextW(window_, Learn, L"学习按键"); Status(L"学习已取消。"); break; }
        if (!Midi().Connected()) { Status(L"请先连接 MIDI 设备。"); break; }
        if (Surface().Touched()) { Status(L"先松开推子，再开始学习。"); break; }
        { const auto i = Choice(window_, CommandChoice); if (i < 0) break;
          const auto* command = reinterpret_cast<const MackieCommand*>(SendDlgItemMessageW(window_, CommandChoice, CB_GETITEMDATA, i, 0));
          LearningCommand() = command->id; Learning() = true; SetDlgItemTextW(window_, Learn, L"取消学习");
          Status(L"请按要分配的设备按键。此按键本次不会执行任何命令。"); }
        break;
    case RemoveBinding:
        if (Surface().Touched()) { Status(L"先松开推子，再更改按键分配。"); break; }
        { const auto row = SendDlgItemMessageW(window_, BindingList, LB_GETCURSEL, 0, 0); if (row == LB_ERR) break;
          const int key = static_cast<int>(SendDlgItemMessageW(window_, BindingList, LB_GETITEMDATA, row, 0));
          Midi().Send({static_cast<std::uint8_t>(0x90 | key / 128), static_cast<std::uint8_t>(key % 128), 0});
          Settings().bindings.erase(key); CustomFeedback().erase(key); Surface().InvalidateFeedback(); FillBindings(); Save(); }
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
    const bool zh = workspace_.language != L"en";
    AppendMenuW(menu, MF_STRING, 1, zh ? L"打开 Windows Fader Bridge" : L"Open Windows Fader Bridge");
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 2, Midi().Connected() ? (zh ? L"状态：已连接" : L"Status: connected") : (zh ? L"状态：未连接" : L"Status: disconnected"));
    AppendMenuW(menu, MF_STRING, Diagnostics, zh ? L"打开诊断目录" : L"Open diagnostics folder");
    AppendMenuW(menu, MF_STRING, RestartApp, zh ? L"重新启动" : L"Restart");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ExitApp, zh ? L"退出" : L"Quit");
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(window_);
    const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command == 1) { if (desktop_) desktop_->Show(); else { ShowWindow(window_, SW_RESTORE); SetForegroundWindow(window_); } }
    else if (command == RestartApp) Restart();
    else if (command) Command(command, 0);
}
void MackieApplication::Restart()
{
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path))
    {
        Status(L"Windows 无法确定应用位置。");
        return;
    }
    const auto parameters = std::wstring(L"--background --restart-from ") +
        std::to_wstring(GetCurrentProcessId());
    const auto result = ShellExecuteW(window_, L"open", path, parameters.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        Status(L"Windows 无法重新启动应用。");
        return;
    }
    Quit();
}
void MackieApplication::Quit()
{
    if (closing_) return;
    closing_ = true; KillTimer(window_, 1);
    for(auto& device:devices_){DeviceScope scope(*this,device.get());Save();Disconnect(false);device->media.reset();}
    audio_.reset(); emptyDevice_->media.reset();
    { std::scoped_lock lock(commandMutex_); commandRunning_ = false; commandQueue_.clear(); }
    commandWake_.notify_all(); if (commandWorker_.joinable()) commandWorker_.join();
    MSG message{};
    while (PeekMessageW(&message, window_, AudioMessage, AudioMessage, PM_REMOVE)) delete reinterpret_cast<AudioFrame*>(message.lParam);
    if (tray_.hWnd) Shell_NotifyIconW(NIM_DELETE, &tray_);
    if (desktop_) desktop_->Close();
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
    if (message == bridge::SummonMessage())
    {
        if (desktop_) desktop_->Show();
        else { ShowWindow(window_, SW_RESTORE); SetForegroundWindow(window_); }
        return 0;
    }
    switch (message)
    {
    case AudioMessage: OnFrame(std::unique_ptr<AudioFrame>(reinterpret_cast<AudioFrame*>(l))); return 0;
    case WM_TIMER: Tick(); return 0;
    case WM_HOTKEY:
        if (w == 1) { if (desktop_) desktop_->Show(); else { ShowWindow(window_, SW_RESTORE); SetForegroundWindow(window_); } return 0; }
        break;
    case WM_COMMAND:
        if (LOWORD(w) == RestartApp) Restart();
        else Command(LOWORD(w), HIWORD(w));
        return 0;
    case CommandResult: if (!w) Status(L"Windows 命令执行失败；请查看日志或目标窗口的权限。"); return 0;
    case WM_NOTIFY:
        if (const auto header = reinterpret_cast<NMHDR*>(l); header->idFrom == Tracks && !renderGuard_)
        { if (header->code == LVN_ITEMCHANGED && (reinterpret_cast<NMLISTVIEW*>(l)->uNewState & LVIS_SELECTED)) Surface().Select(RowKey()); }
        break;
    case WM_DEVICECHANGE:
        deviceScanDue_=0;return 0;
    case WM_APP + 13: if (desktop_) desktop_->Show(); return 0;
    case TrayMessage: if (l == WM_RBUTTONUP) TrayMenu(); else if (l == WM_LBUTTONDBLCLK) { if (desktop_) desktop_->Show(); else { ShowWindow(window_, SW_RESTORE); SetForegroundWindow(window_); } } return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(w), RGB(222, 233, 245)); SetBkColor(reinterpret_cast<HDC>(w), RGB(22, 27, 35));
        return reinterpret_cast<LRESULT>(background_);
    case WM_CLOSE: if (tray_.hWnd && !smoke_) ShowWindow(window_, SW_HIDE); else Quit(); return 0;
    case WM_ENDSESSION: if (w) Quit(); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window_, message, w, l);
}
