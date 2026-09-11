#pragma once

// Project-owned, read-only SDK inspection. This does not register a node,
// change assignment or write any audio parameter.
#include "EuProcessor.h"
#include "EuControl.h"
#include "EuPrimitiveSwitch.h"
#include <sstream>

namespace ApolloBridge
{
inline void InspectMonitorObject(EuCommon &object, std::wostream &out)
{
    for (NEuCon::uint32 key = 0; key < kATRIBID_NumEuConDefinedIntAttributes; ++key)
    {
        tEuString text;
        NEuCon::int32 number = 0;
        if (object.GetAttribute(key, text) == kERR_OK)
            out << L" a" << key << L"=\"" << text << L"\"";
        else if (object.GetAttribute(key, number) == kERR_OK)
            out << L" a" << key << L"=" << number;
    }
}

inline void InspectMonitorControl(EuControl &control, std::wostream &out, int depth = 0)
{
    NEuCon::uint32 id = 0, member = 0;
    control.GetId(id);
    control.GetMemberControlId(member);
    out << std::wstring(depth, L' ') << L"control=" << id << L" member=" << member;
    InspectMonitorObject(control, out);
    out << L'\n';
    if (control.IsControlArray())
    {
        std::vector<EuControl *> children;
        if (control.GetContainedControls(children) == kERR_OK)
            for (auto *child : children)
                InspectMonitorControl(*child, out, depth + 1);
        return;
    }
    std::vector<EuPrimitiveControl *> primitives;
    if (control.GetContainedPrimitives(primitives) != kERR_OK)
        return;
    for (auto *primitive : primitives)
    {
        primitive->GetId(id);
        out << std::wstring(depth + 1, L' ') << L"primitive=" << id
            << L" initialized=" << primitive->IsInitializedByProcessorModeler();
        if (primitive->IsInitializedByProcessorModeler())
        {
            tTYP type{};
            NEuCon::uint16 size = 0, index = 0;
            primitive->GetValueType(type);
            primitive->GetValueTableSize(size);
            primitive->GetCurrentIndex(index);
            out << L" type=" << type << L" size=" << size << L" index=" << index;
            if (auto *button = dynamic_cast<EuPrimitiveSwitch *>(primitive))
            {
                tSWITCH mode{};
                button->GetSwitchMode(mode);
                out << L" mode=" << mode;
            }
        }
        InspectMonitorObject(*primitive, out);
        out << L'\n';
    }
}

inline std::wstring InspectMonitorModel(EuProcessor &processor)
{
    std::wostringstream out;
    out << L"monitor";
    InspectMonitorObject(processor, out);
    out << L'\n';
    std::vector<EuControl *> controls;
    if (processor.GetContainedControls(controls) == kERR_OK)
        for (auto *control : controls)
            InspectMonitorControl(*control, out);
    return out.str();
}
}
