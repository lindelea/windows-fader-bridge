#pragma once
#include "MackieSurface.h"
#include "CommandCatalog.h"
#include <array>

// Application mapping only: no device-specific protocol or arbitrary shell text.
struct MackieChannelCommand
{
    const wchar_t* id;
    const wchar_t* label;
    mackie::ActionKind kind;
    float value = 0;
};
inline constexpr MackieChannelCommand MackieChannelCommands[] = {
    {L"Channel.VolumeUp", L"音量 +1%", mackie::ActionKind::Volume, .01F},
    {L"Channel.VolumeDown", L"音量 -1%", mackie::ActionKind::Volume, -.01F},
    {L"Channel.PanLeft", L"Pan 向左", mackie::ActionKind::Pan, -.02F},
    {L"Channel.PanRight", L"Pan 向右", mackie::ActionKind::Pan, .02F},
    {L"Channel.PanCenter", L"Pan 回中", mackie::ActionKind::Pan},
    {L"Channel.Mute", L"静音 / 取消静音", mackie::ActionKind::Mute},
    {L"Channel.Solo", L"Solo / 取消 Solo（应用）", mackie::ActionKind::Solo},
    {L"Channel.Default", L"设为默认设备（音频设备）", mackie::ActionKind::DefaultDevice},
    {L"Channel.Focus", L"应用窗口置前", mackie::ActionKind::Focus},
    {L"Channel.PlayPause", L"播放 / 暂停（应用）", mackie::ActionKind::PlayPause},
    {L"Channel.Stop", L"停止（应用）", mackie::ActionKind::Stop},
    {L"Channel.Previous", L"上一首（应用）", mackie::ActionKind::Previous},
    {L"Channel.Next", L"下一首（应用）", mackie::ActionKind::Next},
    {L"Channel.SeekBack", L"播放进度 -1%（应用）", mackie::ActionKind::Seek, -.01F},
    {L"Channel.SeekForward", L"播放进度 +1%（应用）", mackie::ActionKind::Seek, .01F},
    {L"Channel.Repeat", L"循环模式（应用）", mackie::ActionKind::Repeat},
};
inline const MackieChannelCommand* FindMackieChannelCommand(std::wstring_view id)
{
    for (const auto& command : MackieChannelCommands) if (id == command.id) return &command;
    return nullptr;
}
inline bool ValidEncoderCommand(std::wstring_view id)
{
    return id.empty() || FindMackieCommand(id) || FindMackieChannelCommand(id);
}
// Encoders 3..8 only; each stores left, right, push. Empty means unassigned.
using MackieEncoderBindings = std::array<std::array<std::wstring, 3>, 6>;
inline const std::wstring* EncoderBinding(const MackieEncoderBindings& bindings, int encoder, float gesture)
{
    if (encoder < 2 || encoder >= 8 || !std::isfinite(gesture)) return nullptr;
    const auto& id = bindings[encoder - 2][gesture < 0 ? 0 : (gesture > 0 ? 1 : 2)];
    return !id.empty() && ValidEncoderCommand(id) ? &id : nullptr;
}
