#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace apollo
{
// Semantic keys are not directory positions or SDK focus-area indices.
enum class UpperFunction
{
    Inserts,
    Dynamics,
    Eq,
    Input,
    Aux,
    Pan,
    Group,
    Mix,
    Unison,
    Reserved10,
    Console,
    Reserved12,
    Reserved13,
    Reserved14,
    Reserved15,
    ControlRoom
};
struct UpperDirectoryEntry
{
    UpperFunction function;
    const wchar_t *label;
    const wchar_t *shortLabel = nullptr;
};
// Explicit parent-tree order, separate from native layout/focus enumeration.
// See APOLLO_UPPER_CONTROL.md for the guide's conflicting order tables.
inline constexpr std::array<UpperDirectoryEntry, 16> UpperDirectoryEntries{{
    {UpperFunction::Inserts, L"INSERTS"},
    {UpperFunction::Input, L"INPUT"},
    {UpperFunction::Dynamics, L"DYNAMICS"},
    {UpperFunction::Eq, L"EQ"},
    {UpperFunction::Aux, L"AUX"},
    {UpperFunction::Pan, L"PAN"},
    {UpperFunction::Group, L"GROUP"},
    {UpperFunction::Mix, L"MIX"},
    {UpperFunction::Unison, L"UNISON"},
    {UpperFunction::Reserved10, L""},
    {UpperFunction::Console, L"CONSOLE"},
    {UpperFunction::Reserved12, L""},
    {UpperFunction::Reserved13, L""},
    {UpperFunction::Reserved14, L""},
    {UpperFunction::Reserved15, L""},
    {UpperFunction::ControlRoom, L"CONTROL ROOM", L"CR"},
}};
constexpr std::size_t UpperDirectoryIndex(UpperFunction function)
{
    for (std::size_t i = 0; i < UpperDirectoryEntries.size(); ++i)
        if (UpperDirectoryEntries[i].function == function)
            return i;
    throw std::invalid_argument("Unknown upper directory function");
}
// Ordered as the documented 4-character, 8-character and full text variants.
using UpperLabelText = std::array<std::wstring, 3>;
inline UpperLabelText UpperDirectoryLabels(UpperFunction function, bool available = true)
{
    const auto &entry = UpperDirectoryEntries[UpperDirectoryIndex(function)];
    if (!available)
        return {};
    const std::wstring brief = entry.shortLabel ? entry.shortLabel : entry.label;
    return {brief.substr(0, 4), brief.substr(0, 8), entry.label};
}
} // namespace apollo
