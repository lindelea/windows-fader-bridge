#pragma once
#include "Json.h"
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace apollo
{
using NodeMap = std::map<std::string, Json>;
struct Parameter
{
    std::string path;
    Json value;
    std::optional<double> minimum, maximum;
    bool enabled = true;
    bool reportedReadOnly = false;
    std::vector<std::string> choices;
};
struct Meter
{
    std::optional<double> levelDb, peakDb;
    std::optional<bool> clip;
};
enum class AudioFormat
{
    Unknown,
    Mono,
    Stereo
};
struct Send
{
    std::string slot, path, id, name;
    std::optional<Parameter> level, bypass, pan;
};
struct InsertParameter
{
    std::string slot, name, display;
    std::optional<Parameter> normalized, step;
};
struct Insert
{
    std::string slot, path, name, identity;
    std::optional<Parameter> power;
    std::vector<InsertParameter> parameters;
};
struct Preamp
{
    std::string slot, path, context;
    std::optional<Parameter> gain, lowCut, phase, pad, phantom;
    std::vector<Insert> unison;
};
struct Channel
{
    std::string key, path, name, deviceName, ioType, destination;
    bool auxiliary = false, stereo = false, monitor = false;
    // Input/strip format and panner destination format are separate concepts.
    AudioFormat outputFormat = AudioFormat::Unknown;
    std::optional<Parameter> level, pan, panRight, mute, solo;
    std::optional<Parameter> output, input;
    std::optional<Parameter> reference, sampleRateConvert, recordPreEffects, sendPostFader, mono;
    std::optional<Parameter> talk, talkToMonitor;
    std::string talkContext;
    std::vector<Send> sends;
    std::vector<Preamp> preamps;
    std::vector<Insert> inserts;
    std::vector<std::string> insertSlots;
    std::vector<Meter> meters;
};
struct Monitor
{
    std::string key, path, deviceName, name, source, mode;
    bool stereo = false;
    std::optional<Parameter> level, mute, dim, mono;
    std::optional<Parameter> sourceSelect, talk;
    std::string talkbackMicPath;
    // Read-only context guards; dimAttenuation is separately allowlisted.
    std::optional<Parameter> speakerSelection, dimAttenuation, highHeadroom;
    std::optional<Parameter> talkbackMaster, talkbackToMonitor, talkbackMicSelect;
    // Raw indexed meter feeds are not claimed to have surround speaker roles.
    std::vector<Meter> meters;
};
struct Snapshot
{
    bool connected = false;
    uint64_t generation = 0;
    uint64_t metadataRevision = 0;
    uint64_t receivedFrames = 0;
    size_t onlineDevices = 0, offlineDevices = 0, skippedChannels = 0;
    std::chrono::steady_clock::time_point receivedAt{};
    std::string status = "Not connected";
    std::vector<Channel> channels;
    std::vector<Monitor> monitors;
};
std::vector<std::string> Children(const Json &node);
bool NumericSlot(const std::string &slot);
const Json &Property(const Json &node, std::string_view key);
bool DeviceOnline(const Json &node);
std::optional<Parameter> ReadParameter(const NodeMap &nodes, const std::string &nodePath,
                                       const std::string &property);
Snapshot BuildSnapshot(const NodeMap &nodes);
// Native input/AUX strips only. Control-room state is never a channel/fader.
std::vector<Channel> SurfaceChannels(const Snapshot &snapshot);
// No offset, linear-amplitude conversion, attenuation or synthetic peak hold.
std::optional<double> MeterMaximum(const std::vector<Meter> &meters, bool peakHold);
// Accept only updates for known, previously discovered properties.
bool ApplyValue(NodeMap &nodes, const Json &response);
std::vector<std::string> SubscriptionPaths(const NodeMap &nodes);
std::string StableToken(std::string_view identity);
uint32_t ChannelColor(const Channel &channel);
} // namespace apollo
