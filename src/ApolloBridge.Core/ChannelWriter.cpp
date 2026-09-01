#include "ChannelWriter.h"
#include <algorithm>
#include <stdexcept>

namespace apollo
{
using Clock = std::chrono::steady_clock;
namespace
{
bool ContinuousField(ChannelAddress field)
{
    switch (field.kind)
    {
    case ChannelField::Level:
    case ChannelField::PanLeft:
    case ChannelField::PanRight:
    case ChannelField::SendLevel:
    case ChannelField::SendPan:
    case ChannelField::PreampGain:
    case ChannelField::InsertValue:
    case ChannelField::InsertStep:
    case ChannelField::UnisonValue:
    case ChannelField::UnisonStep:
        return true;
    default:
        return false;
    }
}

void ReadFeature(ReadOnlyClient &client, NodeMap &nodes, const Channel &target, ChannelAddress a)
{
    const auto read = [&](const std::string &path) -> const Json & {
        nodes[path] = client.Get(path);
        return nodes.at(path);
    };
    const auto child = [&](const std::string &parent, const std::string &name) {
        const auto list = Children(read(parent));
        if (std::find(list.begin(), list.end(), name) == list.end())
            throw std::runtime_error("Apollo feature was removed");
    };
    const auto preamp = [&](const std::string &slot) {
        const auto path = target.path + "/preamps/" + slot;
        const auto data = read(path);
        if (data.At("children").Has("effects"))
        {
            const auto effects = Children(read(path + "/effects"));
            if (effects.size() > 2)
                throw std::runtime_error("Unknown Unison layout");
            for (const auto &effect : effects)
            {
                if (!NumericSlot(effect))
                    throw std::runtime_error("Invalid Unison slot");
                read(path + "/effects/" + effect);
            }
        }
    };
    if (UnisonField(a))
    {
        child(target.path + "/preamps", a.preamp);
        preamp(a.preamp);
        const auto effectsPath = target.path + "/preamps/" + a.preamp + "/effects";
        child(effectsPath, a.slot);
        if (!a.parameter.empty())
        {
            child(effectsPath + "/" + a.slot + "/parameters", a.parameter);
            read(FieldNodePath(target, a));
        }
        return;
    }
    if (a.kind == ChannelField::Input)
    {
        const auto slots = Children(read(target.path + "/preamps"));
        if (slots.size() > 2)
            throw std::runtime_error("Unknown preamp layout");
        for (const auto &slot : slots)
        {
            if (!NumericSlot(slot))
                throw std::runtime_error("Invalid preamp slot");
            preamp(slot);
        }
        return;
    }
    if (a.slot.empty())
        return;
    const auto path = FieldNodePath(target, a);
    if (a.kind == ChannelField::SendLevel || a.kind == ChannelField::SendBypass ||
        a.kind == ChannelField::SendPan)
    {
        child(target.path + "/sends", a.slot);
        read(path);
    }
    else if (a.kind == ChannelField::PreampGain || a.kind == ChannelField::LowCut ||
             a.kind == ChannelField::Phase || a.kind == ChannelField::Pad || a.kind == ChannelField::Phantom)
    {
        child(target.path + "/preamps", a.slot);
        preamp(a.slot);
    }
    else
    {
        child(target.path + "/effects", a.slot);
        const auto insertPath = target.path + "/effects/" + a.slot;
        read(insertPath);
        if (!a.parameter.empty())
        {
            child(insertPath + "/parameters", a.parameter);
            read(path);
        }
    }
}
} // namespace
void ChannelWriteClient::DrainReplies(int milliseconds)
{
    bool first = true;
    while (auto reply = client_.Poll(first ? milliseconds : 0))
    {
        first = false;
        if (reply->Has("error"))
            throw std::runtime_error("Apollo rejected channel write");
    }
}

std::optional<Json> ChannelWriteClient::ApplyRealtime(
    const ChannelRequest &request, const std::function<bool(const Channel &)> &authorize)
{
    const auto command = ChannelCommand(request.target, request.field, request.value);
    if (Clock::now() - request.created >= std::chrono::seconds(5))
        return {};
    if (!authorize || !authorize(request.target))
        return {};

    // Consume acknowledgements from earlier writes so a long gesture cannot
    // grow an unbounded reply queue. Do not put discovery/readback round trips
    // in front of audible control changes.
    DrainReplies(0);
    client_.SendBytes(command);
    DrainReplies(ContinuousField(request.field) ? 0 : 5);
    return request.value;
}

std::optional<Json> ChannelWriteClient::Apply(const ChannelRequest &request,
                                              const std::function<bool(const Channel &)> &authorize)
{
    // Validate before issuing even a read derived from the requested route.
    ChannelCommand(request.target, request.field, request.value);
    const auto &path = request.target.path;
    const auto devicePath = path.substr(0, path.find('/', 9));
    const auto groupPath = path.substr(0, path.rfind('/'));
    NodeMap nodes;
    for (const auto &p : {std::string("/devices"), devicePath, groupPath, path})
        nodes[p] = client_.Get(p);
    if (request.target.ioType == "TalkbackMic")
    {
        nodes["/"] = client_.Get("/");
        const auto siblings = Children(nodes.at(groupPath));
        if (siblings.size() > 512)
            throw std::runtime_error("Unknown talkback layout");
        for (const auto &slot : siblings)
            if (groupPath + "/" + slot != path)
                nodes[groupPath + "/" + slot] = client_.Get(groupPath + "/" + slot);
    }
    ReadFeature(client_, nodes, request.target, request.field);
    const auto fresh = BuildSnapshot(nodes);
    const Channel *channel = nullptr;
    for (const auto &c : fresh.channels)
        if (c.key == request.target.key)
            channel = &c;
    if (!channel || !SameFieldTarget(request.target, *channel, request.field))
        throw std::runtime_error("Channel identity or capabilities changed; request canceled");
    const auto command = ChannelCommand(*channel, request.field, request.value);
    if (Clock::now() - request.created >= std::chrono::seconds(5))
        return {}; // Known pre-write cancellation; permission remains valid.
    if (!authorize || !authorize(*channel))
        return {};
    client_.SendBytes(command);
    // A set response may echo its scalar path. Request a different, complete
    // channel-node path to distinguish explicit readback from that echo.
    nodes[path] = client_.Get(path, [](const Json &reply) {
        if (reply.Has("error"))
            throw std::runtime_error("Apollo rejected channel write");
    });
    ReadFeature(client_, nodes, request.target, request.field);
    if (request.target.ioType == "TalkbackMic")
        nodes["/"] = client_.Get("/");
    const auto p = ReadParameter(nodes,
                                 FieldNodePath(request.target, request.field).empty()
                                     ? "/"
                                     : FieldNodePath(request.target, request.field),
                                 FieldName(request.field));
    const auto after = BuildSnapshot(nodes);
    const Channel *checked = nullptr;
    for (const auto &c : after.channels)
        if (c.key == request.target.key)
            checked = &c;
    auto expected = request.target;
    if (request.field == ChannelField::Output)
        expected.destination = request.value.String();
    if (request.field == ChannelField::Input)
        expected.ioType = request.value.String();
    if (request.field.kind == ChannelField::Phantom)
    {
        for (auto &preamp : expected.preamps)
            if (preamp.slot == request.field.slot)
            {
                const auto suffix = preamp.context.rfind(':');
                if (suffix != std::string::npos)
                    preamp.context.erase(suffix);
                preamp.context += request.value.Bool() ? ":phantom" : ":no-phantom";
            }
    }
    if (!checked || !SameFieldTarget(expected, *checked, request.field) || !p ||
        !SameControlValue(request.field, request.value, p->value))
        throw std::runtime_error("Apollo readback did not confirm the write (not retried)");
    return p->value;
}
ChannelController::ChannelController(Observer &observer, uint16_t port)
    : observer_(observer), port_(port), worker_([this] { Run(); })
{
}
ChannelController::~ChannelController()
{
    Disarm();
    stop_ = true;
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}
void ChannelController::Arm(const std::string &key)
{
    const auto snapshot = observer_.Latest();
    // Build the complete permission before publishing it. A transiently stale
    // or incomplete observer frame must never leave a tracked-but-unarmed shell
    // that prevents the desktop reconciler from trying again.
    Permission next;
    next.queue.Arm(snapshot, key);
    std::lock_guard<std::mutex> lock(mutex_);
    next.callbackEpoch = ++nextEpoch_;
    next.metadataRevision = snapshot.metadataRevision;
    permissions_[key] = std::move(next);
    error_.clear();
    UpdateEpoch();
    wake_.notify_all();
}
void ChannelController::ArmAll()
{
    for (const auto &c : observer_.Latest().channels)
        if (ControlEligible(c))
            Arm(c.key);
}
void ChannelController::UpdateEpoch()
{
    epoch_ = 0;
    for (const auto &entry : permissions_)
        if (entry.second.queue.Epoch() || entry.second.transition)
        {
            epoch_ = nextEpoch_;
            break;
        }
}
uint64_t ChannelController::Epoch(const std::string &key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = permissions_.find(key);
    return it == permissions_.end() ? 0 : it->second.callbackEpoch;
}
bool ChannelController::Tracks(const std::string &key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return permissions_.find(key) != permissions_.end();
}
void ChannelController::UnlockSafety(const std::string &key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = permissions_.find(key);
    if (it == permissions_.end() || !it->second.queue.Epoch())
        throw std::runtime_error("Enable this channel before unlocking sensitive controls");
    it->second.safety = true;
    error_.clear();
}
void ChannelController::Disarm()
{
    std::lock_guard<std::mutex> lock(mutex_);
    epoch_ = 0;
    permissions_.clear();
    wake_.notify_all();
}
void ChannelController::Disarm(const std::string &key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    permissions_.erase(key);
    UpdateEpoch();
}
void ChannelController::Fail(const std::string &key, const std::string &message)
{
    permissions_.erase(key);
    UpdateEpoch();
    error_ = message;
}
void ChannelController::Validate()
{
    const auto snapshot = observer_.Latest();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = permissions_.begin(); it != permissions_.end();)
    {
        const auto key = it->first;
        auto &p = (it++)->second;
        if (p.transition)
        {
            const auto &r = *p.transition;
            const auto current = std::find_if(snapshot.channels.begin(), snapshot.channels.end(),
                                              [&](const auto &c) { return c.key == key; });
            auto expected = r.target;
            if (r.field == ChannelField::Output)
                expected.destination = r.value.String();
            if (r.field == ChannelField::Input)
                expected.ioType = r.value.String();
            if (!snapshot.connected || snapshot.generation != r.generation ||
                Clock::now() - p.transitionAt > std::chrono::seconds(15))
            {
                Fail(key, "Channel refresh not confirmed; pending control cleared");
                continue;
            }
            if (snapshot.metadataRevision > p.metadataRevision && current != snapshot.channels.end())
            {
                const auto &value = FieldParameter(*current, r.field);
                if (!SameControlTarget(expected, *current) || !value ||
                    !SameControlValue(r.field, r.value, value->value))
                {
                    Fail(key, "Channel changed during routing confirmation; pending control cleared");
                    continue;
                }
                try
                {
                    // A saved Full/Custom policy authorizes this stable logical
                    // channel for the connection, not only one state edge.
                    // Fresh field validation and a new callback epoch still
                    // prevent a gesture from crossing the topology refresh.
                    const bool retainSafety = p.safety;
                    p.queue.Arm(snapshot, key);
                    p.callbackEpoch = ++nextEpoch_;
                    p.metadataRevision = snapshot.metadataRevision;
                    p.transition.reset();
                    p.safety = retainSafety;
                    UpdateEpoch();
                }
                catch (const std::exception &e)
                {
                    Fail(key, e.what());
                }
            }
            continue;
        }
        // The writer may already have received the authoritative route echo.
        // Do not revoke its permission between set and explicit readback.
        if (!p.writingContext && !p.queue.Valid(snapshot))
        {
            const auto current = std::find_if(snapshot.channels.begin(), snapshot.channels.end(),
                                              [&](const auto &c) { return c.key == key; });
            // Loading, unloading or replacing an insert changes the extension
            // model but not the channel identity. Preserve an already granted
            // channel permission across that expected live topology change.
            if (snapshot.connected && snapshot.generation == p.queue.Generation() &&
                current != snapshot.channels.end() && SamePermissionTarget(p.queue.Target(), *current))
            {
                try
                {
                    const bool retainSafety = p.safety;
                    p.queue.Arm(snapshot, key);
                    p.callbackEpoch = ++nextEpoch_;
                    p.metadataRevision = snapshot.metadataRevision;
                    p.pending.clear();
                    // Input mode, routing, linking and plug-in topology may
                    // legitimately change while this remains the same logical
                    // channel. Preserve explicit authority, rebuild from fresh
                    // metadata and invalidate all callbacks from the old shape.
                    p.safety = retainSafety;
                    UpdateEpoch();
                }
                catch (const std::exception &e)
                {
                    Fail(key, e.what());
                }
            }
            else
                Fail(key, "Channel identity, capabilities or connection changed; pending control cleared");
        }
    }
}
bool ChannelController::Submit(const std::string &key, ChannelAddress field, const Json &value,
                               uint64_t epoch)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = permissions_.find(key);
    if (found == permissions_.end() || !epoch || epoch != found->second.callbackEpoch)
        return false;
    auto &p = found->second;
    try
    {
        if (p.writingContext || p.transition)
            return false;
        if (NeedsSafetyUnlock(p.queue.Target(), field, value) && !p.safety)
        {
            error_ = "Sensitive control locked; confirm safety for this channel in the app";
            return false;
        }
        const auto sequence = p.queue.Submit(field, value, p.queue.Epoch());
        if (!sequence)
            return false;
        p.pending[field] = {sequence, value, false, Clock::now()};
        wake_.notify_all();
        return true;
    }
    catch (const std::exception &e)
    {
        Fail(key, e.what());
        return false;
    }
}
ControlStatus ChannelController::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t pending = 0;
    size_t active = 0;
    std::string key, name;
    for (const auto &entry : permissions_)
    {
        if (entry.second.queue.Epoch() || entry.second.transition)
        {
            ++active;
            key = entry.first;
            name = entry.second.queue.Target().name;
        }
        for (const auto &p : entry.second.pending)
            if (!p.second.confirmed)
                ++pending;
    }
    if (active > 1)
    {
        key.clear();
        name = std::to_string(active);
    }
    return {epoch_.load(), confirmed_, key, name, error_, lastOperation_, pending};
}
Channel ChannelController::Feedback(Channel channel)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = permissions_.find(channel.key);
    if (found == permissions_.end() || !found->second.queue.Epoch())
        return channel;
    auto &pending = found->second.pending;
    for (auto it = pending.begin(); it != pending.end();)
    {
        const auto &actual = FieldParameter(channel, it->first);
        // Let genuine external changes through once feedback has caught up, or
        // after a short grace interval. Never mask external state indefinitely.
        if (it->second.confirmed &&
            (!actual || SameControlValue(it->first, actual->value, it->second.value) ||
             Clock::now() - it->second.at > std::chrono::milliseconds(250)))
        {
            it = pending.erase(it);
            continue;
        }
        // Do not pair an optimistic normalized value with a stale native
        // dB/Hz/time display string. Plug-in feedback remains engine-authored.
        if (actual && it->first.kind != ChannelField::InsertValue &&
            it->first.kind != ChannelField::InsertStep && it->first.kind != ChannelField::UnisonValue &&
            it->first.kind != ChannelField::UnisonStep)
            SetFieldFeedback(channel, it->first, it->second.value);
        ++it;
    }
    return channel;
}
void ChannelController::Run()
{
    constexpr auto liveWriteInterval = std::chrono::milliseconds(10);
    std::unique_ptr<ChannelWriteClient> client;
    std::optional<Clock::time_point> replyCheckAt;
    std::string replyKey;
    uint64_t replyAuthority = 0;
    while (!stop_)
    {
        std::optional<ChannelRequest> request;
        uint64_t authority = 0;
        std::string requestKey;
        try
        {
            if (client && replyCheckAt && Clock::now() >= *replyCheckAt)
            {
                requestKey = replyKey;
                authority = replyAuthority;
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
                    bool queued = false;
                    auto cursor = permissions_.upper_bound(lastKey_);
                    for (size_t n = 0; n < permissions_.size(); ++n)
                    {
                        if (cursor == permissions_.end())
                            cursor = permissions_.begin();
                        auto &entry = *cursor++;
                        if (!entry.second.queue.Size())
                            continue;
                        queued = true;
                        auto candidate = entry.second.queue.TakeReady([&](const ChannelRequest &r) {
                            if (!ContinuousField(r.field))
                                return true;
                            const auto sent = entry.second.lastDispatch.find(r.field);
                            if (sent == entry.second.lastDispatch.end())
                                return true;
                            const auto ready = sent->second + liveWriteInterval;
                            if (now >= ready)
                                return true;
                            nextReady = std::min(nextReady, ready);
                            return false;
                        });
                        if (!candidate)
                            continue;
                        requestKey = lastKey_ = entry.first;
                        authority = entry.second.callbackEpoch;
                        request = std::move(candidate);
                        entry.second.writingContext = ChangesChannelContext(request->field);
                        break;
                    }
                    if (request)
                        break;
                    // No artificial global sleep: a newly queued switch or a
                    // different fader wakes immediately, while only the same
                    // continuous address waits for its own 100 Hz slot.
                    auto wakeAt = now + std::chrono::milliseconds(50);
                    if (queued && nextReady != Clock::time_point::max())
                        wakeAt = std::min(wakeAt, nextReady);
                    if (replyCheckAt)
                        wakeAt = std::min(wakeAt, *replyCheckAt);
                    wake_.wait_until(lock, wakeAt);
                    break; // Revalidate topology and outstanding replies after every wait.
                }
                if (stop_)
                    break;
                if (!epoch_)
                    client.reset();
            }
            if (!request)
                continue;
            if (!client)
            {
                client = std::make_unique<ChannelWriteClient>(stop_, port_);
                client->Connect();
            }
            const auto result = client->ApplyRealtime(*request, [&](const Channel &fresh) {
                const auto state = observer_.Latest();
                std::lock_guard<std::mutex> lock(mutex_);
                const auto found = permissions_.find(requestKey);
                if (found == permissions_.end())
                    return false;
                const auto &p = found->second;
                const auto it = p.pending.find(request->field);
                return !stop_ && p.queue.Valid(state) && p.callbackEpoch == authority &&
                       p.queue.Epoch() == request->epoch && it != p.pending.end() &&
                       it->second.sequence == request->sequence &&
                       (!NeedsSafetyUnlock(fresh, request->field, request->value) || p.safety);
            });
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto found = permissions_.find(requestKey);
                if (found == permissions_.end() || found->second.callbackEpoch != authority)
                    continue;
                auto &p = found->second;
                p.writingContext = false;
                if (!result)
                {
                    const auto cancelled = p.pending.find(request->field);
                    if (cancelled != p.pending.end() && cancelled->second.sequence == request->sequence)
                        p.pending.erase(cancelled);
                    error_.clear();
                    lastOperation_ = "Pre-write gesture canceled; permission preserved";
                    continue;
                }
                ++confirmed_;
                if (ContinuousField(request->field))
                    p.lastDispatch[request->field] = Clock::now();
                lastOperation_ = std::string(FieldName(request->field)) + " dispatched=" + result->scalar +
                                 " request=" + std::to_string(request->sequence) + " latency-ms=" +
                                 std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    Clock::now() - request->created)
                                                    .count());
                if (ChangesChannelContext(request->field))
                {
                    p.transition = request;
                    p.transitionAt = Clock::now();
                    p.metadataRevision = observer_.Latest().metadataRevision;
                    p.queue.Disarm();
                    p.callbackEpoch = 0;
                    p.pending.clear();
                    error_.clear();
                    observer_.Refresh();
                    continue;
                }
                const auto it = p.pending.find(request->field);
                if (it != p.pending.end() && it->second.sequence == request->sequence)
                    it->second = {request->sequence, *result, true, Clock::now()};
            }

            // Drain any reply already available without delaying another
            // control. Per-address scheduling above provides the 100 Hz cap.
            if (ContinuousField(request->field))
            {
                client->CheckRealtimeReplies();
                replyCheckAt = Clock::now() + liveWriteInterval;
                replyKey = requestKey;
                replyAuthority = authority;
            }
            else
                replyCheckAt.reset();
        }
        catch (const std::exception &e)
        {
            client.reset(); // never retry a possibly executed write
            replyCheckAt.reset();
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = permissions_.find(requestKey);
            if (found != permissions_.end() && found->second.callbackEpoch == authority)
                Fail(requestKey, e.what());
        }
    }
}
} // namespace apollo
