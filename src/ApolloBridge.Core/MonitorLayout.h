#pragma once
#include "MonitorControl.h"

namespace apollo
{
struct MonitorCell
{
    MonitorField field;
    std::string label, source;
    bool knob = false;
};
inline std::vector<MonitorCell> DescribeMonitor(const Monitor &m)
{
    if (!MonitorEligible(m))
        return {};
    std::vector<MonitorCell> cells;
    for (const auto &cell : std::vector<MonitorCell>{{MonitorField::Level, "Level", "", true},
                                                     {MonitorField::Mute, "Mute"},
                                                     {MonitorField::Dim, "Dim"},
                                                     {MonitorField::DimAmount, "Dim dB", "", true},
                                                     {MonitorField::Mono, "Mono"},
                                                     {MonitorField::Talk, "Talk"}})
        if (MonitorFieldAvailable(m, cell.field))
            cells.push_back(cell);
    if (MonitorFieldAvailable(m, MonitorField::Source))
        for (const auto &source : MonitorSources(m))
            cells.push_back(
                {MonitorField::Source, source == "mon" ? "Monitor" : "Cue " + source.substr(3), source});
    return cells;
}
inline double MonitorKnobValue(MonitorField field, double value)
{
    // The engine stores positive attenuation; the surface displays signed dB.
    return field == MonitorField::DimAmount ? -value : value;
}
inline bool SameMonitorPage(const Monitor &a, const Monitor &b)
{
    if (!SameMonitorTarget(a, b))
        return false;
    const auto x = DescribeMonitor(a), y = DescribeMonitor(b);
    if (x.size() != y.size())
        return false;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (x[i].field != y[i].field || x[i].source != y[i].source)
            return false;
        const auto &p = FieldParameter(a, x[i].field), &q = FieldParameter(b, y[i].field);
        if (!p || !q || p->minimum != q->minimum || p->maximum != q->maximum || p->choices != q->choices ||
            p->value.kind != q->value.kind)
            return false;
    }
    return true;
}
} // namespace apollo
