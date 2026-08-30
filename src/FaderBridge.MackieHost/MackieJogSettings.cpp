#include "MackieApplication.h"
#include "MackieJogInput.h"
#include <Dwmapi.h>

namespace
{
int SelectedValue(HWND window, int id)
{
    const auto index = SendDlgItemMessageW(window, id, CB_GETCURSEL, 0, 0);
    return index == CB_ERR ? -1 : static_cast<int>(SendDlgItemMessageW(window, id, CB_GETITEMDATA, index, 0));
}
}
void MackieApplication::ShowJogSettings()
{
    if (jogWindow_) { SetForegroundWindow(jogWindow_); return; }
    if (encoderWindow_) { SetForegroundWindow(encoderWindow_); return; }
    if (Surface().Touched() || Learning()) { Status(L"请先松开推子并结束按键学习。"); return; }
    WNDCLASSW wc{}; wc.hInstance = instance_; wc.lpfnWndProc = JogWindowProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = background_;
    wc.lpszClassName = L"WindowsFaderBridge.Mackie.Jog";
    RegisterClassW(&wc);
    const int dpi = GetDpiForSystem();
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT bounds{0, 0, MulDiv(900, dpi, 96), MulDiv(620, dpi, 96)}, owner{};
    AdjustWindowRectEx(&bounds, style, FALSE, WS_EX_CONTROLPARENT); GetWindowRect(window_, &owner);
    jogWindow_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Jog / Move / Zoom 设置", style,
        owner.left, owner.top, bounds.right - bounds.left, bounds.bottom - bounds.top, window_, nullptr, instance_, this);
    if (!jogWindow_) { Status(L"无法打开 Jog 设置。"); return; }
    Surface().ResetJogInput();
    const auto control = [&](const wchar_t* type, const wchar_t* text, DWORD flags, int id, int x, int y, int w, int h) {
        const auto c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | flags,
            MulDiv(x, dpi, 96), MulDiv(y, dpi, 96), MulDiv(w, dpi, 96), MulDiv(h, dpi, 96),
            jogWindow_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE); return c;
    };
    constexpr DWORD comboStyle = CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;
    control(L"STATIC", L"普通 Jog、Move、Zoom 均可独立分配。Navi / Focus 保留原机行为，不提供自定义。", 0, 0, 24, 20, 850, 25);
    control(L"STATIC", L"普通 Jog 默认后退 / 前进播放进度，也可改为其他命令或未分配。", 0, 0, 24, 49, 850, 25);
    control(L"STATIC", L"播放进度每格", 0, 0, 24, 95, 135, 25);
    auto seconds = control(WC_COMBOBOXW, L"", comboStyle, 300, 162, 91, 120, 280);
    for (int n = 1; n <= 10; ++n)
    {
        const auto label = std::to_wstring(n) + L" 秒";
        const auto row = SendMessageW(seconds, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(seconds, CB_SETITEMDATA, row, n);
        if (Settings().jog.seekSeconds == n) SendMessageW(seconds, CB_SETCURSEL, row, 0);
    }
    control(L"STATIC", L"方向层校准（保存时应用）", 0, 0, 444, 95, 230, 25);
    auto layer = control(WC_COMBOBOXW, L"", comboStyle, 301, 684, 91, 188, 160);
    SendMessageW(layer, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Move"));
    SendMessageW(layer, CB_SETITEMDATA, 0, 0);
    SendMessageW(layer, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Zoom"));
    SendMessageW(layer, CB_SETITEMDATA, 1, 1);
    SendMessageW(layer, CB_SETCURSEL, Surface().CursorZoom() ? 1 : 0, 0);
    control(L"STATIC", L"功能组", 0, 0, 24, 148, 110, 24);
    control(L"STATIC", L"↑ / ← 命令", 0, 0, 144, 148, 280, 24);
    control(L"STATIC", L"↓ / → 命令", 0, 0, 448, 148, 280, 24);
    control(L"STATIC", L"灵敏度", 0, 0, 754, 148, 118, 24);
    for (int axis = 0; axis < 5; ++axis)
    {
        const int y = 184 + axis * 58;
        control(L"STATIC", mackie::CursorLabels[axis], 0, 0, 24, y + 4, 115, 24);
        for (int direction = 0; direction < 2; ++direction)
        {
            auto combo = control(WC_COMBOBOXW, L"", comboStyle, 200 + axis * 2 + direction, 144 + direction * 304, y, 286, 360);
            const auto add = [&](const wchar_t* id, const std::wstring& label) {
                const auto row = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                SendMessageW(combo, CB_SETITEMDATA, row, reinterpret_cast<LPARAM>(id));
                if (Settings().cursorCommands[axis][direction] == id) SendMessageW(combo, CB_SETCURSEL, row, 0);
            };
            add(L"", L"— 未分配 —");
            if (axis == 4)
            {
                add(L"Jog.SeekBack", L"播放进度 · 后退（按每格秒数）");
                add(L"Jog.SeekForward", L"播放进度 · 前进（按每格秒数）");
            }
            for (const auto& command : mackie::CursorCommands) add(command.id, std::wstring(L"滚动 / 方向键 · ") + command.label);
            for (const auto& command : MackieCommands) if (!command.special)
                add(command.id, std::wstring(command.category) + L" · " + command.label);
        }
        const auto speed = control(WC_COMBOBOXW, L"", comboStyle, 220 + axis, 754, y, 118, 200);
        for (int n : {1, 2, 4, 8})
        {
            const auto label = std::to_wstring(n) + L" 格 / 次";
            const auto row = SendMessageW(speed, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(speed, CB_SETITEMDATA, row, n);
            if (Settings().cursorTicks[axis] == n) SendMessageW(speed, CB_SETCURSEL, row, 0);
        }
    }
    control(L"STATIC", L"灵敏度用于离散命令，最多每秒 10 次；Jog 播放进度仍按上方每格秒数连续调整。\n设置打开时暂停执行。先保存或取消、关闭此窗口，再测试实际动作。", 0, 0, 24, 476, 848, 46);
    control(L"STATIC", L"输入检测：转动 Jog / Move / Zoom 可在这里确认方向（不会执行命令）。", 0, 302, 24, 528, 848, 26);
    control(L"BUTTON", L"保存并应用", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 590, 568, 132, 30);
    control(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 738, 568, 134, 30);
    BOOL dark = TRUE; DwmSetWindowAttribute(jogWindow_, 20, &dark, sizeof(dark));
    ShowWindow(jogWindow_, SW_SHOWNORMAL); SetForegroundWindow(jogWindow_); SetFocus(seconds);
}
void MackieApplication::SaveJogSettings()
{
    auto next = Settings();
    next.jog.seekSeconds = SelectedValue(jogWindow_, 300);
    const int layer = SelectedValue(jogWindow_, 301);
    if (!next.jog.Valid() || layer < 0 || layer > 1) return;
    for (int axis = 0; axis < 5; ++axis)
    {
        next.cursorTicks[axis] = SelectedValue(jogWindow_, 220 + axis);
        if (!mackie::ValidCursorTicks(next.cursorTicks[axis])) return;
        for (int direction = 0; direction < 2; ++direction)
        {
            const int id = 200 + axis * 2 + direction;
            const auto row = SendDlgItemMessageW(jogWindow_, id, CB_GETCURSEL, 0, 0);
            const auto data = SendDlgItemMessageW(jogWindow_, id, CB_GETITEMDATA, row, 0);
            if (row == CB_ERR || !data || data == CB_ERR) return;
            const std::wstring command = reinterpret_cast<const wchar_t*>(data);
            if (!mackie::ValidDirectionCommand(axis, command)) return;
            next.cursorCommands[axis][direction] = command;
        }
    }
    next.trackOrder = Surface().Order();
    if (!next.Save()) { MessageBoxW(jogWindow_, L"保存失败，原设置没有改变。", L"Jog 设置", MB_OK | MB_ICONERROR); return; }
    Settings() = std::move(next); DirtySettings() = false;
    Surface().Jog.settings = Settings().jog; Surface().CursorGestures.ticks = Settings().cursorTicks;
    for (int direction = 0; direction < 2; ++direction)
        Surface().JogSeekDirections[direction] = mackie::JogSeekDirection(Settings().cursorCommands[4][direction]);
    Surface().SetCursorZoom(layer != 0);
    DestroyWindow(jogWindow_);
    Status(L"Jog / Move / Zoom 五组方向已保存；Navi / Focus 保持原机行为。");
}
LRESULT CALLBACK MackieApplication::JogWindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
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
        if (LOWORD(w) == IDOK) self->SaveJogSettings();
        else if (LOWORD(w) == IDCANCEL) DestroyWindow(window);
        return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetTextColor(reinterpret_cast<HDC>(w), RGB(222, 233, 245)); SetBkColor(reinterpret_cast<HDC>(w), RGB(22, 27, 35));
        return reinterpret_cast<LRESULT>(self->background_);
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        self->jogWindow_ = nullptr; self->Surface().ResetJogInput(); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
