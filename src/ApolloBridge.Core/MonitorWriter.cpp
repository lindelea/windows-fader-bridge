#include "MonitorWriter.h"
#include <stdexcept>

namespace apollo
{
using Clock = std::chrono::steady_clock;
void MonitorWriteClient::DrainReplies(int milliseconds)
{
    bool first = true;
    while (auto reply = client_.Poll(first ? milliseconds : 0))
    {
        first = false;
        if (reply->Has("error"))
            throw std::runtime_error("Apollo rejected monitor write");
    }
}

std::optional<Json> MonitorWriteClient::ApplyRealtime(
    const MonitorRequest &request, const std::function<bool(const Monitor &)> &authorize)
{
    const auto command = MonitorCommand(request.target, request.field, request.value, request.ceiling);
    if (Clock::now() - request.created >= std::chrono::seconds(5))
        return {};
    if (!authorize || !authorize(request.target))
        return {};

    DrainReplies(0);
    client_.SendBytes(command);
    DrainReplies(request.field == MonitorField::Level ? 0 : 5);
    return request.value;
}

std::optional<Json> MonitorWriteClient::Apply(const MonitorRequest &request,
                                              const std::function<bool(const Monitor &)> &authorize)
{
    // Validate before issuing even a read derived from the requested route.
    MonitorCommand(request.target, request.field, request.value, request.ceiling);
    const auto &path = request.target.path;
    const auto devicePath = path.substr(0, path.find('/', 9));
    const auto groupPath = path.substr(0, path.rfind('/'));
    NodeMap nodes;
    for (const auto &p : {std::string("/"), std::string("/devices"), devicePath, groupPath, path})
        nodes[p] = client_.Get(p);
    // Discover the identified talkback mic afresh, not from a retained device index.
    if (!request.target.talkbackMicPath.empty())
    {
        const auto inputs = devicePath + "/inputs";
        nodes[inputs] = client_.Get(inputs);
        const auto children = Children(nodes[inputs]);
        if (children.size() > 512)
            throw std::runtime_error("Talkback discovery limit exceeded");
        for (const auto &child : children)
            nodes[inputs + "/" + child] = client_.Get(inputs + "/" + child);
    }
    const auto fresh = BuildSnapshot(nodes);
    const Monitor *monitor = nullptr;
    for (const auto &c : fresh.monitors)
        if (c.key == request.target.key)
            monitor = &c;
    if (!monitor || !SameMonitorTarget(request.target, *monitor))
        throw std::runtime_error("Monitor identity or capabilities changed; request canceled");
    const auto command = MonitorCommand(*monitor, request.field, request.value, request.ceiling);
    if (Clock::now() - request.created >= std::chrono::seconds(5))
        return {}; // Known pre-write cancellation; permission remains valid.
    if (!authorize || !authorize(*monitor))
        return {};
    client_.SendBytes(command);
    // A set response may echo its scalar path. Request a different, complete
    // monitor-node path to distinguish explicit readback from that echo.
    const auto &fieldPath = FieldParameter(*monitor, request.field)->path;
    const auto property = fieldPath.rfind('/', fieldPath.size() - 7);
    const auto readbackPath = property == 0 ? std::string("/") : fieldPath.substr(0, property);
    nodes[readbackPath] = client_.Get(readbackPath, [](const Json &reply) {
        if (reply.Has("error"))
            throw std::runtime_error("Apollo rejected monitor write");
    });
    // Refresh monitor level/context too after a global/device property write.
    if (readbackPath != path)
        nodes[path] = client_.Get(path);
    const auto p = ReadParameter(nodes, readbackPath, FieldName(request.field));
    const auto after = BuildSnapshot(nodes);
    const Monitor *checked = nullptr;
    for (const auto &c : after.monitors)
        if (c.key == request.target.key)
            checked = &c;
    if (!checked || !SameMonitorTarget(request.target, *checked) || !p ||
        !SameMonitorValue(request.field, request.value, p->value))
        throw std::runtime_error("Apollo readback did not confirm the write (not retried)");
    MonitorCommand(*checked, request.field, p->value, request.ceiling);
    return p->value;
}
MonitorController::MonitorController(Observer &observer, uint16_t port)
    : observer_(observer), port_(port), worker_([this] { Run(); })
{
}
MonitorController::~MonitorController()
{
    Disarm();
    stop_ = true;
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void MonitorController::Arm(const std::string &key, std::optional<double> ceiling)
{
    const auto snapshot = observer_.Latest();
    std::lock_guard<std::mutex> lock(mutex_);
    epoch_ = 0;
    pending_.clear();
    lastDispatch_.clear();
    error_.clear();
    epoch_ = queue_.Arm(snapshot, key, ceiling);
    wake_.notify_all();
}
void MonitorController::Disarm()
{
    std::lock_guard<std::mutex> lock(mutex_);
    epoch_ = 0;
    queue_.Disarm();
    pending_.clear();
    lastDispatch_.clear();
    wake_.notify_all();
}
void MonitorController::Fail(const std::string &message)
{
    // Caller owns mutex_. An already transmitted write cannot be undone here.
    epoch_ = 0;
    queue_.Disarm();
    pending_.clear();
    error_ = message;
}
void MonitorController::Validate()
{
    const auto snapshot = observer_.Latest();
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.Epoch() && !queue_.Valid(snapshot))
        Fail("Apollo state changed or became stale; pending control cleared");
}
bool MonitorController::Submit(const std::string &key, MonitorField field, const Json &value, uint64_t epoch)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (key != queue_.Target().key || epoch != queue_.Epoch())
        return false;
    try
    {
        const auto sequence = queue_.Submit(field, value, epoch);
        if (!sequence)
            return false;
        pending_[field] = {sequence, ConstrainMonitorValue(queue_.Target(), field, value, queue_.Ceiling()), false,
                           Clock::now()};
        wake_.notify_all();
        return true;
    }
    catch (const std::exception &e)
    {
        Fail(e.what());
        return false;
    }
}
MonitorStatus MonitorController::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t pending = 0;
    for (const auto &p : pending_)
        if (!p.second.confirmed)
            ++pending;
    return {queue_.Epoch(), confirmed_,     queue_.Target().key, queue_.Target().name,
            error_,         lastOperation_, queue_.Ceiling(),    pending};
}
Monitor MonitorController::Feedback(Monitor monitor)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!queue_.Epoch() || monitor.key != queue_.Target().key)
        return monitor;
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        const auto &actual = FieldParameter(monitor, it->first);
        // Let genuine external changes through once feedback has caught up, or
        // after a short grace interval. Never mask external state indefinitely.
        if (it->second.confirmed && (!actual || SameMonitorValue(it->first, actual->value, it->second.value) ||
                                     Clock::now() - it->second.at > std::chrono::milliseconds(250)))
        {
            it = pending_.erase(it);
            continue;
        }
        if (actual)
        {
            auto value = *actual;
            value.value = it->second.value;
            switch (it->first)
            {
            case MonitorField::Level:
                monitor.level = value;
                break;
            case MonitorField::Mute:
                monitor.mute = value;
                break;
            case MonitorField::Dim:
                monitor.dim = value;
                break;
            case MonitorField::Mono:
                monitor.mono = value;
                break;
            case MonitorField::DimAmount:
                monitor.dimAttenuation = value;
                break;
            case MonitorField::Source:
                monitor.sourceSelect = value;
                monitor.source = value.value.String();
                break;
            case MonitorField::Talk:
                monitor.talk = value;
                break;
            }
        }
        ++it;
    }
    return monitor;
}
void MonitorController::Run()
{
    constexpr auto liveWriteInterval = std::chrono::milliseconds(10);
    std::unique_ptr<MonitorWriteClient> client;
    std::optional<Clock::time_point> replyCheckAt;
    uint64_t replyEpoch = 0;
    while (!stop_)
    {
        std::optional<MonitorRequest> request;
        uint64_t failureEpoch = 0;
        try
        {
            if (client && replyCheckAt && Clock::now() >= *replyCheckAt)
            {
                failureEpoch = replyEpoch;
                client->CheckRealtimeReplies();
                replyCheckAt.reset();
            }
            Validate();
            {
                std::unique_lock<std::mutex> lock(mutex_);
                while (!stop_ && !request)
                {
                    const auto now = Clock::now();
                    if (replyCheckAt && now >= *replyCheckAt)
                        break;
                    auto nextReady = Clock::time_point::max();
                    request = queue_.TakeReady([&](const MonitorRequest &r) {
                        if (r.field != MonitorField::Level)
                            return true;
                        const auto sent = lastDispatch_.find(r.field);
                        if (sent == lastDispatch_.end())
                            return true;
                        const auto ready = sent->second + liveWriteInterval;
                        if (now >= ready)
                            return true;
                        nextReady = ready;
                        return false;
                    });
                    if (request)
                        break;
                    auto wakeAt = now + std::chrono::milliseconds(50);
                    if (queue_.Size() && nextReady != Clock::time_point::max())
                        wakeAt = std::min(wakeAt, nextReady);
                    if (replyCheckAt)
                        wakeAt = std::min(wakeAt, *replyCheckAt);
                    wake_.wait_until(lock, wakeAt);
                    break; // Revalidate monitor context and replies after every wait.
                }
                if (stop_)
                    break;
                if (!queue_.Epoch())
                    client.reset();
            }
            if (!request)
                continue;
            failureEpoch = request->epoch;
            if (!client)
            {
                client = std::make_unique<MonitorWriteClient>(stop_, port_);
                client->Connect();
            }
            const auto result = client->ApplyRealtime(*request, [&](const Monitor &) {
                const auto state = observer_.Latest();
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it = pending_.find(request->field);
                return !stop_ && queue_.Valid(state) && queue_.Epoch() == request->epoch && it != pending_.end() &&
                       it->second.sequence == request->sequence;
            });
            if (!result)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.Epoch() == request->epoch)
                {
                    const auto it = pending_.find(request->field);
                    if (it != pending_.end() && it->second.sequence == request->sequence)
                        pending_.erase(it);
                    error_.clear();
                    lastOperation_ = "Pre-write gesture canceled; permission preserved";
                }
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.Epoch() != request->epoch)
                    continue;
                ++confirmed_;
                if (request->field == MonitorField::Level)
                    lastDispatch_[request->field] = Clock::now();
                lastOperation_ =
                    std::string(FieldName(request->field)) + " dispatched=" + result->scalar +
                    " request=" + std::to_string(request->sequence) + " latency-ms=" +
                    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       Clock::now() - request->created)
                                       .count());
                const auto it = pending_.find(request->field);
                if (it != pending_.end() && it->second.sequence == request->sequence)
                    it->second = {request->sequence, *result, true, Clock::now()};
            }

            // Never stall the writer after an audible change. The queue above
            // limits only repeated main-level writes for this exact address.
            if (request->field == MonitorField::Level)
            {
                client->CheckRealtimeReplies();
                replyCheckAt = Clock::now() + liveWriteInterval;
                replyEpoch = request->epoch;
            }
            else
                replyCheckAt.reset();
        }
        catch (const std::exception &e)
        {
            client.reset(); // never retry a possibly executed write
            replyCheckAt.reset();
            std::lock_guard<std::mutex> lock(mutex_);
            if (failureEpoch && queue_.Epoch() == failureEpoch)
                Fail(e.what());
        }
    }
}
} // namespace apollo
