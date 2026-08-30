#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace mackie
{
// Ordinary CC 3C defaults to playback; its two directions can be assigned.
// Native Navi/Focus are not host modes.
struct JogSettings
{
    int seekSeconds = 1;
    bool Valid() const { return seekSeconds >= 1 && seekSeconds <= 10; }
};
class JogControl
{
public:
    JogSettings settings;
    int Rotate(int delta, std::uint64_t) const
    {
        if (!delta || delta < -63 || delta > 63 || !settings.Valid()) return 0;
        return std::clamp(delta, -10, 10) * settings.seekSeconds;
    }
};

// Direction 0 = up/left, 1 = down/right. Axes 0..3: Cursor + Zoom; 4: ordinary Jog.
inline constexpr std::array<const wchar_t*, 5> CursorLabels{
    L"Move ↑↓", L"Move ←→", L"Zoom ↑↓", L"Zoom ←→", L"普通 Jog ←→"};
inline bool ValidCursorAxis(int axis) { return axis >= 0 && axis < 5; }
inline bool ValidCursorTicks(int ticks) { return ticks == 1 || ticks == 2 || ticks == 4 || ticks == 8; }
class CursorControl
{
public:
    std::array<int, 5> ticks{1, 1, 1, 1, 1};
    void ResetGesture() { axis_ = -1; remainder_ = 0; last_ = due_ = 0; }
    int Rotate(int axis, int direction, std::uint64_t now)
    {
        if (!ValidCursorAxis(axis) || (direction != -1 && direction != 1) || !ValidCursorTicks(ticks[axis])) return 0;
        if (axis != axis_ || now < last_) { ResetGesture(); axis_ = axis; }
        if (now - last_ > 400 || (remainder_ && ((remainder_ < 0) != (direction < 0)))) remainder_ = 0;
        last_ = now;
        if (now < due_) { remainder_ = 0; return 0; }
        remainder_ += direction;
        if (std::abs(remainder_) < ticks[axis]) return 0;
        remainder_ = 0; due_ = now + 100;
        return direction;
    }
private:
    int axis_ = -1, remainder_ = 0;
    std::uint64_t last_ = 0, due_ = 0;
};
}
