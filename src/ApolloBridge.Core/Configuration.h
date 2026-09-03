#pragma once
#include "Model.h"
#include "Protocol.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>

namespace apollo
{
enum class ConfigKind
{
    Global,
    Device,
    Cue,
    Headphone,
    DigitalMirror,
    Plugin,
    Preset
};
struct ConfigOption
{
    Json value;
    std::string label, catalog, categories;
};
struct ConfigSetting
{
    ConfigKind kind = ConfigKind::Global;
    std::string key, node, property, label, owner, context;
    Json value;
    std::vector<ConfigOption> options;
    std::vector<std::string> reads;
};
struct Configuration
{
    std::map<std::string, ConfigSetting> settings;
    std::string shape;
    std::string systemIdentity;
};
inline bool ConfigValueEqual(const Json &a, const Json &b)
{
    return a.kind == b.kind && a.scalar == b.scalar;
}
inline bool ConfigText(const std::string &s, bool empty = false)
{
    return (empty || !s.empty()) && s.size() <= 512 &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
inline std::string ConfigDisplayText(std::string text)
{
    // EUCON OLED configuration text is intentionally uppercase. Preserve
    // non-ASCII bytes and the exact underlying Json value used for writes.
    for (char &c : text)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return text;
}
inline std::string ConfigScalar(const Json &v)
{
    if (v.kind == Json::Kind::Boolean && (v.scalar == "true" || v.scalar == "false"))
        return v.scalar;
    if (v.kind == Json::Kind::Number && std::isfinite(v.Number(NAN)))
        return Json::Parse(v.scalar).kind == Json::Kind::Number
                   ? v.scalar
                   : throw std::invalid_argument("Invalid number");
    if (v.kind != Json::Kind::String || !ConfigText(v.scalar, true))
        throw std::invalid_argument("Unsupported configuration value");
    std::string result = "\"";
    for (char c : v.scalar)
    {
        if (c == '\\' || c == '"')
            result += '\\';
        result += c;
    }
    return result + '"';
}
inline Json ConfigString(const std::string &s)
{
    Json value;
    value.kind = Json::Kind::String;
    value.scalar = s;
    return value;
}
inline std::string PluginShortLabel(const std::string &title)
{
    std::vector<std::string> tokens;
    std::string token;
    for (const unsigned char c : title)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            token += static_cast<char>(std::toupper(c));
        else if (!token.empty())
        {
            tokens.push_back(std::move(token));
            token.clear();
        }
    }
    if (!token.empty())
        tokens.push_back(std::move(token));
    if (!tokens.empty() && (tokens.front() == "UAD" || tokens.front() == "UADX"))
        tokens.erase(tokens.begin());
    if (tokens.size() >= 2 && tokens[0] == "UNIVERSAL" && tokens[1] == "AUDIO")
        tokens.erase(tokens.begin(), tokens.begin() + 2);
    if (tokens.empty())
        return "PLUGIN";

    const auto generic = [](const std::string &s) {
        static const std::set<std::string> words{
            "PLUGIN", "PLUG", "IN", "COLLECTION", "CHANNEL", "STRIP", "PREAMP", "COMPRESSOR",
            "LIMITER", "EQUALIZER", "EQ", "REVERB", "DELAY", "MASTERING", "TAPE", "STEREO",
            "MONO", "EMULATION", "BUNDLE", "PROCESSOR"};
        return words.count(s) != 0;
    };
    const auto modifier = [](const std::vector<std::string> &v, size_t after) {
        for (size_t i = after; i < v.size(); ++i)
        {
            if (v[i] == "SILVER") return std::string("SIL");
            if (v[i] == "GRAY" || v[i] == "GREY") return std::string("GRY");
            if (v[i] == "LEGACY") return std::string("LEG");
            if (v[i] == "CLASSIC") return std::string("CLS");
            if (v[i] == "EXPANDED") return std::string("EXP");
            if (v[i] == "REV" && i + 1 < v.size()) return v[i + 1].substr(0, 3);
            if (v[i].rfind("MK", 0) == 0) return v[i].substr(0, 3);
        }
        return std::string{};
    };
    const auto compact = [](const std::string &s, size_t limit) {
        static const std::map<std::string, std::string> known{{"CAPITOL", "CAP"},
                                                               {"CHAMBERS", "CHMB"},
                                                               {"VISION", "VISN"},
                                                               {"STUDIOS", "STD"},
                                                               {"MASSIVE", "MASS"},
                                                               {"PASSIVE", "PAS"},
                                                               {"NEVE", "NVE"},
                                                               {"TELETRONIX", "TEL"},
                                                               {"MANLEY", "MANLY"},
                                                               {"AMPEX", "AMPX"},
                                                               {"STUDER", "STDR"},
                                                               {"SHADOW", "SHDW"},
                                                               {"HILLS", "HILS"},
                                                               {"CULTURE", "CLTR"},
                                                               {"VULTURE", "VLTR"},
                                                               {"CENTURY", "CNTR"}};
        const auto named = known.find(s);
        if (named != known.end())
            return named->second.substr(0, limit);
        if (s.size() <= limit)
            return s;
        std::string result;
        for (const char c : s)
            if (result.empty() || (c != 'A' && c != 'E' && c != 'I' && c != 'O' && c != 'U'))
            {
                result += c;
                if (result.size() == limit)
                    break;
            }
        if (result.size() < limit)
            result = s.substr(0, limit);
        return result;
    };

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (tokens[i].find_first_of("0123456789") == std::string::npos)
            continue;
        std::string model = tokens[i];
        if (!model.empty() && std::isdigit(static_cast<unsigned char>(model.front())) && i > 0 &&
            tokens[i - 1].size() <= 3 &&
            tokens[i - 1].find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos &&
            !generic(tokens[i - 1]))
            model = tokens[i - 1] + model;
        const auto edition = modifier(tokens, i + 1);
        if (!edition.empty() && model.size() + edition.size() + 1 <= 8)
            return model + " " + edition;
        size_t brandIndex = i;
        while (brandIndex > 0)
        {
            --brandIndex;
            if (!generic(tokens[brandIndex]) && tokens[brandIndex] != model)
                break;
        }
        const auto brand = brandIndex < i && !generic(tokens[brandIndex]) ? compact(tokens[brandIndex], 3) : "";
        if (!brand.empty() && brand.size() + model.size() + 1 <= 8)
            return brand + " " + model;
        return model.substr(0, 8);
    }

    std::vector<std::string> useful;
    for (const auto &part : tokens)
        if (!generic(part))
            useful.push_back(part);
    if (useful.empty())
        useful = tokens;
    if (useful.size() == 1)
        return useful.front().substr(0, 8);
    if (useful[0] == "OCEAN" && useful[1] == "WAY")
        return "OCEANWAY";
    const auto first = compact(useful[0], 4);
    const auto second = compact(useful[1], first.size() < 7 ? 7 - first.size() : 1);
    return (first + " " + second).substr(0, 8);
}
inline const Json &ConfigNode(const NodeMap &nodes, const std::string &path)
{
    static const Json empty;
    const auto it = nodes.find(path);
    return it == nodes.end() ? empty : it->second;
}
inline std::vector<ConfigOption> ConfigChoices(const Json &p)
{
    std::vector<ConfigOption> result;
    if (p.At("readonly").Bool() || !p.At("enabled").Bool(true))
        return result;
    const auto &current = p.At("value");
    if (p.At("type").String() == "bool" && current.kind == Json::Kind::Boolean)
        return {{Json::Parse("false"), "OFF", {}}, {Json::Parse("true"), "ON", {}}};
    if ((p.At("type").String() != "string" || current.kind != Json::Kind::String) &&
        (p.At("type").String() != "int" || current.kind != Json::Kind::Number))
        return result;
    for (const auto &entry : p.At("values").array)
    {
        const auto &v = entry.kind == Json::Kind::Object ? entry.At("value") : entry;
        if (v.kind != current.kind || !entry.At("enabled").Bool(true))
            continue;
        try
        {
            ConfigScalar(v);
        }
        catch (...)
        {
            continue;
        }
        if (v.kind == Json::Kind::Number && (std::floor(v.Number(NAN)) != v.Number(NAN) ||
                                             (p.Has("min") && v.Number() < p.At("min").Number()) ||
                                             (p.Has("max") && v.Number() > p.At("max").Number())))
            continue;
        auto label = ConfigDisplayText(entry.At("string").String(v.scalar));
        if (!ConfigText(label))
            continue;
        if (std::none_of(result.begin(), result.end(),
                         [&](const auto &o) { return ConfigValueEqual(v, o.value); }))
            result.push_back({v, label, {}});
        if (result.size() >= 512)
            break;
    }
    return result;
}
inline void PopulatePluginChoices(const NodeMap &nodes, ConfigSetting &load, const Json &currentName,
                                  const std::string &instance, bool unisonOnly,
                                  ConfigSetting *preset = nullptr)
{
    const auto node = [&](const std::string &path) -> const Json & { return ConfigNode(nodes, path); };
    // Removing a loaded effect is explicit, never an automatic cleanup.
    load.options.push_back({ConfigString(""), "NONE", {}});
    for (const auto &plugin : Children(node("/plugins")))
    {
        if (!NumericSlot(plugin))
            continue;
        const auto catalog = "/plugins/" + plugin;
        const auto &info = node(catalog);
        const auto title = Property(info, "Name").At("value").String();
        if (Property(info, "Type").At("value").String() != "plugin" ||
            !Property(info, "Authorized").At("value").Bool() || !ConfigText(title))
            continue;
        const auto &unison = Property(info, "Unison");
        if (unisonOnly && (unison.At("type").String() != "bool" || !unison.At("value").Bool()))
            continue;
        const auto categories = Property(info, "Categories").At("value").String();
        load.options.push_back({ConfigString(title), PluginShortLabel(title), catalog, categories});
        if (!preset || title != currentName.scalar || instance == "0")
            continue;
        for (const auto &item : Property(info, "Preset").At("values").array)
        {
            const auto v = item.At("value");
            if (item.At("type").String() != "file" || !item.At("enabled").Bool(true) ||
                v.kind != Json::Kind::String || !ConfigText(v.scalar))
                continue;
            preset->options.push_back({v, ConfigDisplayText(v.scalar), catalog});
            if (preset->options.size() >= 512)
                break;
        }
    }
    std::set<std::string> usedLabels;
    for (auto option = load.options.begin() + 1; option != load.options.end(); ++option)
    {
        const auto base = option->label.substr(0, 8);
        auto candidate = base;
        for (size_t suffix = 2; usedLabels.count(candidate); ++suffix)
        {
            const auto number = std::to_string(suffix);
            candidate = base.substr(0, 8 - std::min<size_t>(7, number.size())) + number;
        }
        option->label = candidate;
        usedLabels.insert(candidate);
    }
    std::sort(load.options.begin() + 1, load.options.end(),
              [](const auto &a, const auto &b) { return a.label < b.label; });
}
inline std::shared_ptr<const Configuration> BuildConfiguration(const NodeMap &nodes)
{
    auto result = std::make_shared<Configuration>();
    const auto node = [&](const std::string &p) -> const Json & { return ConfigNode(nodes, p); };
    std::vector<std::string> devices;
    std::string system;
    for (const auto &slot : Children(node("/devices")))
        system += "slot=" + slot + ";";
    for (const auto &slot : Children(node("/devices")))
    {
        const auto path = "/devices/" + slot;
        const auto &id = Property(node(path), "DeviceHwID").At("value");
        if (!NumericSlot(slot) || !DeviceOnline(node(path)) || id.scalar.empty() || id.scalar == "0")
            continue;
        devices.push_back(path);
        system += path + ":" + StableToken(id.scalar) + ";";
    }
    if (devices.empty())
        return result;
    result->systemIdentity = StableToken(system);
    const auto add = [&](ConfigKind kind, const std::string &path, const std::string &property,
                         const std::string &label, const std::string &owner, const std::string &context,
                         std::vector<std::string> reads) -> ConfigSetting * {
        const auto &p = Property(node(path), property);
        auto choices = ConfigChoices(p);
        if (choices.empty())
            return nullptr;
        const auto key = (path == "/" ? "" : path) + "/" + property;
        ConfigSetting s{kind, key, path, property, label, owner, context, p.At("value"), choices, reads};
        return &result->settings.emplace(key, std::move(s)).first->second;
    };
    std::vector<std::string> globalReads{"/", "/devices"};
    globalReads.insert(globalReads.end(), devices.begin(), devices.end());
    for (const auto &entry :
         std::vector<std::pair<std::string, std::string>>{{"SampleRate", "RATE"},
                                                          {"ClockSource", "CLOCK"},
                                                          {"BufferSize", "BUFFER"},
                                                          {"MaxDelayComp", "DELAYCMP"},
                                                          {"CueBusCount", "CUE CNT"},
                                                          {"ClipHold", "CLIP HLD"},
                                                          {"PeakHold", "PEAK HLD"},
                                                          {"PostFaderMetering", "METER AT"},
                                                          {"ControlsMode", "EDITMODE"},
                                                          {"Enable24dBMode", "HEADROOM"},
                                                          {"MidiInputDevice", "MIDI DEV"}})
        if (auto *s = add(ConfigKind::Global, "/", entry.first, entry.second, {}, system, globalReads))
        {
            if (entry.first == "PostFaderMetering")
                for (auto &o : s->options)
                    o.label = o.value.Bool() ? "POST" : "PRE";
            if (entry.first == "Enable24dBMode")
                for (auto &o : s->options)
                    o.label = o.value.Bool() ? "+24 DBU" : "+20 DBU";
            // The live engine reports an empty string for DEVICE=NONE.
            if (entry.first == "MidiInputDevice")
                s->options.insert(s->options.begin(), {ConfigString(""), "NONE", {}});
        }
    for (const auto &device : devices)
    {
        const auto context = StableToken(Property(node(device), "DeviceHwID").At("value").scalar);
        auto reads = globalReads;
        add(ConfigKind::Device, device, "FuncSwitchMode", "FCN SW", device, context, reads);
        const auto outputs = device + "/outputs";
        reads.push_back(outputs);
        for (const auto &slot : Children(node(outputs)))
        {
            if (!NumericSlot(slot))
                continue;
            const auto path = outputs + "/" + slot;
            auto outputReads = reads;
            outputReads.push_back(path);
            const auto io = Property(node(path), "IOType").At("value").String();
            const auto name = Property(node(path), "Name").At("value").String();
            const auto shape = context + ":" + io + ":" + Property(node(path), "Stereo").At("value").scalar;
            if (io == "Monitor" && Property(node(path), "DigitalMirrorAvailable").At("value").Bool())
                add(ConfigKind::DigitalMirror, path, "MirrorsToDigital", "DIG MIR", device, shape,
                    outputReads);
            if (io == "Cue" && Property(node(path), "Active").At("value").Bool() && ConfigText(name))
                for (const auto &p : std::vector<std::pair<std::string, std::string>>{
                         {"MixToMono", "MONO"}, {"MixInSource", "SOURCE"}, {"OutputDestination", "MIRROR"}})
                {
                    const auto cueLabel = name.size() == 5 && name.rfind("CUE ", 0) == 0 &&
                                                  name.back() >= '1' && name.back() <= '4'
                                              ? "C" + name.substr(4) +
                                                    (p.second == "SOURCE"   ? "SRC"
                                                     : p.second == "MIRROR" ? "MIRR"
                                                                            : "MONO")
                                              : name + " " + p.second;
                    auto *s = add(ConfigKind::Cue, path, p.first, cueLabel, device, shape, outputReads);
                    if (s && p.first == "MixInSource")
                        for (auto &o : s->options)
                            o.label = o.value.scalar == "mon" ? "MIX" : "CUE";
                }
            if (io == "Headphone" && ConfigText(name))
                add(ConfigKind::Headphone, path, "MixInSource", name + " SRC", device, shape, outputReads);
        }
        for (const std::string group : {"inputs", "auxs"})
        {
            const auto container = device + "/" + group;
            for (const auto &slot : Children(node(container)))
            {
                if (!NumericSlot(slot))
                    continue;
                const auto channel = container + "/" + slot;
                const auto &c = node(channel);
                if (!Property(c, "Active").At("value").Bool() ||
                    Property(c, "ChannelHidden").At("value").Bool() ||
                    !Property(c, "EnabledByUser").At("value").Bool(true))
                    continue;
                const auto channelContext = context + ":" + Property(c, "IOType").At("value").scalar + ":" +
                                            Property(c, "Stereo").At("value").scalar;
                for (const auto &fx : Children(node(channel + "/effects")))
                {
                    if (!NumericSlot(fx))
                        continue;
                    const auto path = channel + "/effects/" + fx;
                    const auto &effect = node(path);
                    if (Property(effect, "Type").At("value").String() != "effect")
                        continue;
                    const auto name = Property(effect, "EffectName").At("value");
                    const auto instance = Property(effect, "EffectInstance").At("value").scalar;
                    const auto effectContext = channelContext + ":" + name.scalar + ":" + instance;
                    auto fxReads = globalReads;
                    fxReads.insert(fxReads.end(),
                                   {container, channel, channel + "/effects", path, "/plugins"});
                    ConfigSetting load{ConfigKind::Plugin,
                                       path + "/EffectName",
                                       path,
                                       "EffectName",
                                       "INSERT " + std::to_string(std::stoul(fx) + 1),
                                       channel,
                                       effectContext,
                                       name,
                                       {},
                                       fxReads};
                    ConfigSetting preset{ConfigKind::Preset,
                                         path + "/Preset",
                                         path,
                                         "Preset",
                                         "PRESET",
                                         channel,
                                         effectContext,
                                         Property(effect, "Preset").At("value"),
                                         {},
                                         fxReads};
                    if (Property(effect, "EffectName").At("type").String() != "string" ||
                        name.kind != Json::Kind::String || !ConfigText(name.scalar, true) ||
                        Property(effect, "EffectInstance").At("type").String() != "pointer" ||
                        instance.empty() || instance.size() > 20 ||
                        instance.find_first_not_of("0123456789") != instance.npos)
                        continue;
                    PopulatePluginChoices(nodes, load, name, instance, false, &preset);
                    if (!Property(effect, "EffectName").At("readonly").Bool() &&
                        Property(effect, "EffectName").At("enabled").Bool(true))
                        result->settings.emplace(load.key, std::move(load));
                    if (!preset.options.empty() && preset.value.kind == Json::Kind::String &&
                        Property(effect, "Preset").At("type").String() == "string" &&
                        !Property(effect, "Preset").At("readonly").Bool() &&
                        Property(effect, "Preset").At("enabled").Bool(true))
                        result->settings.emplace(preset.key, std::move(preset));
                }
                const auto preampSlots = Children(node(channel + "/preamps"));
                for (const auto &preampSlot : preampSlots)
                {
                    if (!NumericSlot(preampSlot))
                        continue;
                    const auto preamp = channel + "/preamps/" + preampSlot;
                    if (Property(node(preamp), "Type").At("value").String() != "preamp")
                        continue;
                    const auto effects = preamp + "/effects";
                    for (const auto &fx : Children(node(effects)))
                    {
                        if (!NumericSlot(fx))
                            continue;
                        const auto path = effects + "/" + fx;
                        const auto &effect = node(path);
                        if (Property(effect, "Type").At("value").String() != "effect")
                            continue;
                        const auto name = Property(effect, "EffectName").At("value");
                        const auto instance = Property(effect, "EffectInstance").At("value").scalar;
                        if (Property(effect, "EffectName").At("type").String() != "string" ||
                            name.kind != Json::Kind::String || !ConfigText(name.scalar, true) ||
                            Property(effect, "EffectInstance").At("type").String() != "pointer" ||
                            instance.empty() || instance.size() > 20 ||
                            instance.find_first_not_of("0123456789") != instance.npos)
                            continue;
                        const auto effectContext = channelContext + ":unison:" + preampSlot + ":" + fx + ":" +
                                                   name.scalar + ":" + instance;
                        auto fxReads = globalReads;
                        fxReads.insert(fxReads.end(), {container, channel, channel + "/preamps", preamp, effects,
                                                       path, "/plugins"});
                        const auto label = preampSlots.size() > 1
                                               ? "UNISON " + std::to_string(std::stoul(preampSlot) + 1)
                                               : "UNISON";
                        ConfigSetting load{ConfigKind::Plugin,
                                           path + "/EffectName",
                                           path,
                                           "EffectName",
                                           label,
                                           channel,
                                           effectContext,
                                           name,
                                           {},
                                           fxReads};
                        ConfigSetting preset{ConfigKind::Preset,
                                             path + "/Preset",
                                             path,
                                             "Preset",
                                             "PRESET",
                                             channel,
                                             effectContext,
                                             Property(effect, "Preset").At("value"),
                                             {},
                                             fxReads};
                        PopulatePluginChoices(nodes, load, name, instance, true, &preset);
                        if (!Property(effect, "EffectName").At("readonly").Bool() &&
                            Property(effect, "EffectName").At("enabled").Bool(true))
                            result->settings.emplace(load.key, std::move(load));
                        if (!preset.options.empty() && preset.value.kind == Json::Kind::String &&
                            Property(effect, "Preset").At("type").String() == "string" &&
                            !Property(effect, "Preset").At("readonly").Bool() &&
                            Property(effect, "Preset").At("enabled").Bool(true))
                            result->settings.emplace(preset.key, std::move(preset));
                    }
                }
            }
        }
    }
    std::string shape;
    for (const auto &entry : result->settings)
    {
        const auto &s = entry.second;
        shape += s.key + ":" + s.label + ":" + s.context + ";";
        for (const auto &o : s.options)
            shape += ConfigScalar(o.value) + ":" + o.label + ":" + o.catalog + ":" + o.categories + ";";
    }
    result->shape = StableToken(shape);
    return result;
}
inline const ConfigSetting *FindConfig(const std::shared_ptr<const Configuration> &configuration,
                                       const std::string &key)
{
    if (!configuration)
        return nullptr;
    const auto it = configuration->settings.find(key);
    return it == configuration->settings.end() ? nullptr : &it->second;
}
inline bool SameConfiguration(const std::shared_ptr<const Configuration> &a,
                              const std::shared_ptr<const Configuration> &b)
{
    return !a || !b ? !a && !b : a->shape == b->shape;
}
inline bool SameConfigShape(const std::shared_ptr<const Configuration> &a,
                            const std::shared_ptr<const Configuration> &b, const std::string &key)
{
    const auto *x = FindConfig(a, key), *y = FindConfig(b, key);
    if (!x || !y)
        return !x && !y;
    if (x->context != y->context || x->kind != y->kind || x->owner != y->owner || x->label != y->label ||
        x->options.size() != y->options.size())
        return false;
    for (size_t i = 0; i < x->options.size(); ++i)
        if (!ConfigValueEqual(x->options[i].value, y->options[i].value) ||
            x->options[i].label != y->options[i].label || x->options[i].catalog != y->options[i].catalog ||
            x->options[i].categories != y->options[i].categories)
            return false;
    return true;
}
} // namespace apollo
