#pragma once
#include "Model.h"
#include <deque>

namespace apollo
{
enum class MonitorField
{
    Level,
    Mute,
    Dim,
    Mono,
    DimAmount,
    Source,
    Talk
};
const char *FieldName(MonitorField field);
const std::optional<Parameter> &FieldParameter(const Monitor &monitor, MonitorField field);
bool MonitorEligible(const Monitor &monitor);
bool SameMonitorTarget(const Monitor &a, const Monitor &b);
bool MonitorFieldAvailable(const Monitor &monitor, MonitorField field);
std::vector<std::string> MonitorSources(const Monitor &monitor);
const std::vector<float> &MonitorDimTable(); // Signed dB, ascending SDK table.
// No arbitrary paths, source injection, speaker selectors or trim writes.
Json ConstrainMonitorValue(const Monitor &monitor, MonitorField field, const Json &value, double ceiling);
std::string MonitorCommand(const Monitor &monitor, MonitorField field, const Json &value, double ceiling);
bool SameMonitorValue(MonitorField field, const Json &a, const Json &b);
struct MonitorRequest
{
    Monitor target;
    MonitorField field = MonitorField::Level;
    Json value;
    double ceiling = -144;
    uint64_t epoch = 0, generation = 0, sequence = 0;
    std::chrono::steady_clock::time_point created{};
};
class MonitorQueue
{
  public:
    uint64_t Arm(const Snapshot &snapshot, const std::string &key);
    void Disarm();
    bool Valid(const Snapshot &snapshot) const;
    uint64_t Submit(MonitorField field, const Json &value, uint64_t epoch);
    std::optional<MonitorRequest> Take();
    uint64_t Epoch() const
    {
        return armed_ ? epoch_ : 0;
    }
    const Monitor &Target() const
    {
        return target_;
    }
    double Ceiling() const
    {
        return ceiling_;
    }
    size_t Size() const
    {
        return pending_.size();
    }

  private:
    Monitor target_;
    bool armed_ = false;
    double ceiling_ = -144;
    uint64_t epoch_ = 0, generation_ = 0, sequence_ = 0;
    std::deque<MonitorRequest> pending_;
};
} // namespace apollo
