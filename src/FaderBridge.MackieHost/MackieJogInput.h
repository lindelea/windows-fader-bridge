#pragma once
#include "CommandCatalog.h"
#include "MackieJog.h"
#include <Windows.h>
#include <array>
#include <vector>

namespace mackie
{
enum class CursorInput { ScrollUp, ScrollDown, ScrollLeft, ScrollRight, Up, Down, Left, Right };
struct CursorCommand { const wchar_t* id; const wchar_t* label; CursorInput input; };
inline constexpr std::array<CursorCommand, 8> CursorCommands{{
    {L"Cursor.ScrollUp", L"滚轮向上", CursorInput::ScrollUp},
    {L"Cursor.ScrollDown", L"滚轮向下", CursorInput::ScrollDown},
    {L"Cursor.ScrollLeft", L"滚轮向左", CursorInput::ScrollLeft},
    {L"Cursor.ScrollRight", L"滚轮向右", CursorInput::ScrollRight},
    {L"Cursor.Up", L"键盘方向键 ↑", CursorInput::Up},
    {L"Cursor.Down", L"键盘方向键 ↓", CursorInput::Down},
    {L"Cursor.Left", L"键盘方向键 ←", CursorInput::Left},
    {L"Cursor.Right", L"键盘方向键 →", CursorInput::Right}
}};
inline const CursorCommand* FindCursorCommand(std::wstring_view id)
{
    for (const auto& command : CursorCommands) if (id == command.id) return &command;
    return nullptr;
}
inline bool ValidCursorCommand(std::wstring_view id)
{
    const auto command = FindMackieCommand(id);
    return id.empty() || FindCursorCommand(id) || (command && command->special == 0);
}
inline int JogSeekDirection(std::wstring_view id)
{
    return id == L"Jog.SeekBack" ? -1 : (id == L"Jog.SeekForward" ? 1 : 0);
}
inline bool ValidDirectionCommand(int axis, std::wstring_view id)
{
    return ValidCursorAxis(axis) && (ValidCursorCommand(id) || (axis == 4 && JogSeekDirection(id) != 0));
}
// Tests construct packets without injecting global input.
inline std::vector<INPUT> CursorInputs(CursorInput action)
{
    const int kind = static_cast<int>(action);
    if (kind < 0 || kind > 7) return {};
    INPUT input{};
    if (kind < 4)
    {
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = kind < 2 ? MOUSEEVENTF_WHEEL : MOUSEEVENTF_HWHEEL;
        input.mi.mouseData = static_cast<DWORD>((kind == 0 || kind == 3 ? 1 : -1) * WHEEL_DELTA);
        return {input};
    }
    constexpr WORD keys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT};
    input.type = INPUT_KEYBOARD; input.ki.wVk = keys[kind - 4]; input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    auto release = input; release.ki.dwFlags |= KEYEVENTF_KEYUP;
    return {input, release};
}
inline bool CursorModifiersHeld()
{
    for (int key : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(key) & 0x8000) return true;
    return false;
}
inline bool SendCursorInput(CursorInput action)
{
    if (CursorModifiersHeld()) return false;
    auto inputs = CursorInputs(action);
    if (inputs.empty()) return false;
    const auto sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent == inputs.size()) return true;
    if (sent == 1 && inputs.size() == 2) SendInput(1, &inputs[1], sizeof(INPUT));
    return false;
}
}
