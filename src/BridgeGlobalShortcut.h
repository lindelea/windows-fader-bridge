#pragma once

#include <Windows.h>
#include <dwmapi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bridge
{
enum ShortcutModifier : std::uint32_t
{
    ShortcutControl = 1U,
    ShortcutAlt = 2U,
    ShortcutShift = 4U,
    ShortcutWindows = 8U
};

struct Shortcut
{
    std::uint32_t modifiers = ShortcutControl | ShortcutAlt | ShortcutShift;
    std::uint32_t key = 0;

    bool operator==(const Shortcut &other) const
    {
        return modifiers == other.modifiers && key == other.key;
    }
    bool operator!=(const Shortcut &other) const { return !(*this == other); }
};

inline bool IsModifierKey(const UINT key)
{
    return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL || key == VK_MENU ||
           key == VK_LMENU || key == VK_RMENU || key == VK_SHIFT || key == VK_LSHIFT ||
           key == VK_RSHIFT || key == VK_LWIN || key == VK_RWIN;
}

inline bool ValidShortcut(const Shortcut &shortcut)
{
    constexpr std::uint32_t all = ShortcutControl | ShortcutAlt | ShortcutShift | ShortcutWindows;
    return shortcut.modifiers && !(shortcut.modifiers & ~all) && shortcut.key >= 1 &&
           shortcut.key <= 0xFE && !IsModifierKey(shortcut.key);
}

inline UINT NativeModifiers(const Shortcut &shortcut)
{
    UINT value = MOD_NOREPEAT;
    if (shortcut.modifiers & ShortcutControl) value |= MOD_CONTROL;
    if (shortcut.modifiers & ShortcutAlt) value |= MOD_ALT;
    if (shortcut.modifiers & ShortcutShift) value |= MOD_SHIFT;
    if (shortcut.modifiers & ShortcutWindows) value |= MOD_WIN;
    return value;
}

inline Shortcut CurrentShortcut(const UINT key)
{
    Shortcut result;
    result.modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) result.modifiers |= ShortcutControl;
    if (GetKeyState(VK_MENU) & 0x8000) result.modifiers |= ShortcutAlt;
    if (GetKeyState(VK_SHIFT) & 0x8000) result.modifiers |= ShortcutShift;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
        result.modifiers |= ShortcutWindows;
    result.key = key;
    return result;
}

inline std::wstring KeyName(const UINT key)
{
    if (key >= 'A' && key <= 'Z') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= '0' && key <= '9') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
    switch (key)
    {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Esc";
    case VK_TAB: return L"Tab";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    default: break;
    }
    const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    wchar_t name[64]{};
    if (scan && GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64) > 0) return name;
    return L"Key " + std::to_wstring(key);
}

inline std::wstring ShortcutText(const Shortcut &shortcut)
{
    std::wstring result;
    const auto add = [&](const wchar_t *part) {
        if (!result.empty()) result += L"+";
        result += part;
    };
    if (shortcut.modifiers & ShortcutControl) add(L"Ctrl");
    if (shortcut.modifiers & ShortcutAlt) add(L"Alt");
    if (shortcut.modifiers & ShortcutShift) add(L"Shift");
    if (shortcut.modifiers & ShortcutWindows) add(L"Win");
    if (shortcut.key) add(KeyName(shortcut.key).c_str());
    return result;
}

inline INPUT ShortcutKeyInput(const WORD key, const bool keyUp)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0U;
    return input;
}

// Route a surface command through the target application's registered hotkey.
// Windows then performs the same foreground transition as the user's physical
// shortcut. Modifiers that were already physically held are left untouched;
// every key introduced here has a matching release and a recovery release if
// SendInput accepts only part of the batch.
inline bool InvokeRegisteredShortcut(const Shortcut &shortcut)
{
    if (!ValidShortcut(shortcut)) return false;
    struct ModifierKey { std::uint32_t flag; WORD key; };
    constexpr ModifierKey modifierKeys[] = {
        {ShortcutControl, VK_CONTROL}, {ShortcutAlt, VK_MENU},
        {ShortcutShift, VK_SHIFT}, {ShortcutWindows, VK_LWIN}
    };
    std::vector<WORD> introduced;
    std::vector<INPUT> inputs;
    inputs.reserve(10U);
    for (const auto &modifier : modifierKeys)
    {
        if ((shortcut.modifiers & modifier.flag) &&
            !(GetAsyncKeyState(modifier.key) & 0x8000))
        {
            inputs.push_back(ShortcutKeyInput(modifier.key, false));
            introduced.push_back(modifier.key);
        }
    }
    inputs.push_back(ShortcutKeyInput(static_cast<WORD>(shortcut.key), false));
    inputs.push_back(ShortcutKeyInput(static_cast<WORD>(shortcut.key), true));
    for (auto key = introduced.rbegin(); key != introduced.rend(); ++key)
        inputs.push_back(ShortcutKeyInput(*key, true));

    const auto expected = static_cast<UINT>(inputs.size());
    const auto sent = SendInput(expected, inputs.data(), sizeof(INPUT));
    if (sent == expected) return true;

    std::vector<INPUT> releases;
    releases.reserve(introduced.size() + 1U);
    releases.push_back(ShortcutKeyInput(static_cast<WORD>(shortcut.key), true));
    for (auto key = introduced.rbegin(); key != introduced.rend(); ++key)
        releases.push_back(ShortcutKeyInput(*key, true));
    SendInput(static_cast<UINT>(releases.size()), releases.data(), sizeof(INPUT));
    return false;
}

// A surface callback is intentional user input, but Windows does not classify
// it as foreground-eligible keyboard or mouse input. Join the foreground input
// queue only for the activation call, then detach immediately. No keys are
// synthesized and the previously focused application is never minimized.
inline bool ActivateTopLevelWindow(const HWND window)
{
    if (!window) return false;
    ShowWindowAsync(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);

    const auto currentThread = GetCurrentThreadId();
    const auto foregroundWindow = GetForegroundWindow();
    const auto foregroundThread = foregroundWindow
        ? GetWindowThreadProcessId(foregroundWindow, nullptr) : 0U;
    const auto targetThread = GetWindowThreadProcessId(window, nullptr);
    bool attachedForeground = false, attachedTarget = false;
    if (foregroundThread && foregroundThread != currentThread)
        attachedForeground = AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
    if (targetThread && targetThread != currentThread && targetThread != foregroundThread)
        attachedTarget = AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;

    BringWindowToTop(window);
    SetActiveWindow(window);
    const auto activated = SetForegroundWindow(window);

    if (attachedTarget) AttachThreadInput(currentThread, targetThread, FALSE);
    if (attachedForeground) AttachThreadInput(currentThread, foregroundThread, FALSE);
    return activated != FALSE && GetForegroundWindow() == window;
}

class GlobalShortcutRegistration
{
  public:
    ~GlobalShortcutRegistration() { Reset(); }
    GlobalShortcutRegistration() = default;
    GlobalShortcutRegistration(const GlobalShortcutRegistration &) = delete;
    GlobalShortcutRegistration &operator=(const GlobalShortcutRegistration &) = delete;

    bool Apply(HWND window, int id, bool enabled, const Shortcut &shortcut)
    {
        if (!window || (enabled && !ValidShortcut(shortcut))) return false;
        const auto oldWindow = window_;
        const auto oldId = id_;
        const auto oldShortcut = shortcut_;
        const bool oldRegistered = registered_;
        if (registered_) UnregisterHotKey(window_, id_);
        registered_ = false;
        if (enabled && !RegisterHotKey(window, id, NativeModifiers(shortcut), shortcut.key))
        {
            if (oldRegistered && oldWindow)
                registered_ = RegisterHotKey(oldWindow, oldId, NativeModifiers(oldShortcut),
                                             oldShortcut.key) != FALSE;
            return false;
        }
        window_ = window;
        id_ = id;
        shortcut_ = shortcut;
        registered_ = enabled;
        return true;
    }

    void Reset()
    {
        if (registered_ && window_) UnregisterHotKey(window_, id_);
        registered_ = false;
        window_ = nullptr;
    }

  private:
    HWND window_ = nullptr;
    int id_ = 0;
    Shortcut shortcut_{};
    bool registered_ = false;
};

enum class Application
{
    WindowsEucon,
    UadEucon,
    MackieControl
};

inline UINT SummonMessage()
{
    static const UINT message = RegisterWindowMessageW(L"Lindelea.Bridges.Summon.v1");
    return message;
}

constexpr WPARAM SummonFromApplication = 0U;
constexpr WPARAM SummonFromEuconKeyCommand = 1U;

inline const wchar_t *ApplicationWindowClass(const Application application)
{
    switch (application)
    {
    case Application::WindowsEucon: return L"WindowsFaderBridge.MainWindow";
    case Application::UadEucon: return L"Lindelea.ApolloBridge.EUCON.Window";
    case Application::MackieControl: return L"WindowsFaderBridge.Mackie.Main";
    }
    return L"";
}

inline bool RequestSummon(const Application application)
{
    const auto window = FindWindowW(ApplicationWindowClass(application), nullptr);
    return window && PostMessageW(window, SummonMessage(),
                                  SummonFromEuconKeyCommand, 0);
}

namespace detail
{
struct CaptureState
{
    Shortcut result{};
    bool accepted = false;
    bool chinese = false;
};

inline LRESULT CALLBACK CaptureProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto *state = reinterpret_cast<CaptureState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        state = static_cast<CaptureState *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message)
    {
    case WM_CREATE: {
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        CreateWindowExW(0, L"BUTTON", state && state->chinese ? L"取消" : L"Cancel",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 170, 142, 120, 34, window,
                        reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        auto brush = CreateSolidBrush(RGB(17, 20, 24));
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        const auto font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        const auto smallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                           L"Segoe UI");
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(235, 237, 240));
        auto old = SelectObject(dc, font);
        RECT title{24, 22, client.right - 24, 51};
        DrawTextW(dc, state && state->chinese ? L"按下新的组合键" : L"Press the new shortcut",
                  -1, &title, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc, smallFont);
        SetTextColor(dc, RGB(159, 169, 180));
        RECT help{24, 54, client.right - 24, 79};
        DrawTextW(dc, state && state->chinese ? L"至少包含一个修饰键 · Esc 取消"
                                               : L"Include at least one modifier · Esc cancels",
                  -1, &help, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT card{116, 88, client.right - 116, 126};
        brush = CreateSolidBrush(RGB(25, 29, 34));
        auto pen = CreatePen(PS_SOLID, 1, RGB(47, 53, 61));
        auto oldBrush = SelectObject(dc, brush);
        auto oldPen = SelectObject(dc, pen);
        RoundRect(dc, card.left, card.top, card.right, card.bottom, 8, 8);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        SetTextColor(dc, RGB(224, 177, 104));
        RECT value = card;
        DrawTextW(dc, state ? ShortcutText(state->result).c_str() : L"", -1, &value,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc, old);
        DeleteObject(smallFont);
        DeleteObject(font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DRAWITEM: {
        const auto item = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        if (!item || item->CtlID != IDCANCEL) break;
        const bool pressed = (item->itemState & ODS_SELECTED) != 0;
        auto brush = CreateSolidBrush(pressed ? RGB(47, 53, 61) : RGB(25, 29, 34));
        FillRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
        brush = CreateSolidBrush(RGB(47, 53, 61));
        FrameRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, RGB(235, 237, 240));
        DrawTextW(item->hDC, state && state->chinese ? L"取消" : L"Cancel", -1,
                  const_cast<RECT *>(&item->rcItem),
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        return TRUE;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (w == VK_ESCAPE)
        {
            DestroyWindow(window);
            return 0;
        }
        if (!IsModifierKey(static_cast<UINT>(w)) && state)
        {
            const auto shortcut = CurrentShortcut(static_cast<UINT>(w));
            if (ValidShortcut(shortcut))
            {
                state->result = shortcut;
                state->accepted = true;
                DestroyWindow(window);
            }
            else
                MessageBeep(MB_ICONWARNING);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == IDCANCEL) DestroyWindow(window);
        return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
} // namespace detail

inline bool CaptureShortcut(HWND owner, const Shortcut &current, Shortcut &result, bool chinese)
{
    static constexpr wchar_t captureClass[] = L"Lindelea.Bridges.ShortcutRecorder.v1";
    WNDCLASSW wc{};
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    wc.lpfnWndProc = detail::CaptureProc;
    wc.lpszClassName = captureClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    detail::CaptureState state{current, false, chinese};
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = 460, height = 230;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    const auto window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, captureClass, chinese ? L"录制全局快捷键" : L"Record global shortcut",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, owner, nullptr, wc.hInstance, &state);
    if (!window) return false;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(window);
    MSG message{};
    bool quitReceived = false;
    int quitCode = 0;
    while (IsWindow(window))
    {
        const auto status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0)
        {
            if (status == 0)
            {
                quitReceived = true;
                quitCode = static_cast<int>(message.wParam);
            }
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (quitReceived) PostQuitMessage(quitCode);
    if (state.accepted) result = state.result;
    return state.accepted;
}
} // namespace bridge
