#pragma once
#include "MonitorControl.h"
#include "Configuration.h"

namespace apollo
{
struct MonitorCell
{
    MonitorField field;
    std::string label, source;
    bool knob = false;
    bool configStart = false;
    bool configuration = false;
    std::string configKey;
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
inline std::vector<MonitorCell> DescribeConfiguredMonitor(const Monitor &m, bool enabled)
{
    auto cells = DescribeMonitor(m);
    if (!enabled)
        return cells;
    const auto normal = cells;
    bool first = true;
    for (auto cell : normal)
        if (cell.field == MonitorField::DimAmount || cell.field == MonitorField::Source)
        {
            cell.configuration = true;
            cell.configStart = first;
            first = false;
            cells.push_back(std::move(cell));
        }
    if (m.configuration)
        for (const auto &entry : m.configuration->settings)
        {
            const auto &s = entry.second;
            const auto device = m.path.substr(0, m.path.find('/', 9));
            if ((s.kind != ConfigKind::Cue && s.kind != ConfigKind::Headphone) || s.owner != device) continue;
            MonitorCell cell{MonitorField::Source, s.label};
            cell.configuration = true;
            cell.configStart = first;
            cell.configKey = s.key;
            first = false;
            cells.push_back(std::move(cell));
        }
    return cells;
}
inline double MonitorKnobValue(MonitorField field, double value)
{
    // The engine stores positive attenuation; the surface displays signed dB.
    return field == MonitorField::DimAmount ? -value : value;
}
inline bool SameMonitorConfig(const Monitor &a, const Monitor &b)
{
    const auto x = DescribeConfiguredMonitor(a, true), y = DescribeConfiguredMonitor(b, true);
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i].configKey != y[i].configKey ||
            (!x[i].configKey.empty() && !SameConfigShape(a.configuration, b.configuration, x[i].configKey))) return false;
    return true;
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
