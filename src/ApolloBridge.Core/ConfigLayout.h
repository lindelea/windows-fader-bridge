#pragma once
#include "ChannelLayout.h"
#include "Configuration.h"
#include <array>
#include <cctype>

namespace apollo
{
struct GlobalConfigEntry
{
    const char *property;
    const char *label;
};
inline constexpr std::array<GlobalConfigEntry, 11> GlobalConfigEntries{{{"ClipHold", "CLIP HLD"},
                                                                        {"PeakHold", "PEAK HLD"},
                                                                        {"PostFaderMetering", "METER AT"},
                                                                        {"MaxDelayComp", "DELAYCMP"},
                                                                        {"CueBusCount", "CUE CNT"},
                                                                        {"SampleRate", "RATE"},
                                                                        {"ClockSource", "CLOCK"},
                                                                        {"ControlsMode", "EDITMODE"},
                                                                        {"BufferSize", "BUFFER"},
                                                                        {"Enable24dBMode", "HEADROOM"},
                                                                        {"MidiInputDevice", "MIDI DEV"}}};
inline std::string ConfigReadout(const Json &property)
{
    const auto &value = property.At("value");
    if (value.kind == Json::Kind::Boolean)
        return value.Bool() ? "ON" : "OFF";
    if (value.kind != Json::Kind::String && value.kind != Json::Kind::Number)
        return "N/A";
    auto text = value.scalar;
    for (const auto &choice : property.At("values").array)
        if (choice.kind == Json::Kind::Object && choice.At("value").kind == value.kind &&
            choice.At("value").scalar == value.scalar && choice.At("string").kind == Json::Kind::String)
            text = choice.At("string").scalar;
    if (text.empty() || text.size() > 128 ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return "N/A";
    return ConfigDisplayText(text);
}
inline std::map<std::string, std::string> ReadGlobalConfig(const Json &root)
{
    std::map<std::string, std::string> values;
    for (const auto &entry : GlobalConfigEntries)
    {
        const auto &p = root.At("properties").At(entry.property);
        auto text = ConfigReadout(p);
        if (std::string(entry.property) == "PostFaderMetering" && p.At("value").kind == Json::Kind::Boolean)
            text = p.At("value").Bool() ? "POST" : "PRE";
        if (std::string(entry.property) == "Enable24dBMode" && p.At("value").kind == Json::Kind::Boolean)
            text = p.At("value").Bool() ? "+24 DBU" : "+20 DBU";
        if (std::string(entry.property) == "MidiInputDevice" && p.At("value").kind == Json::Kind::String &&
            p.At("value").scalar.empty())
            text = "NONE";
        values.emplace(entry.property, std::move(text));
    }
    return values;
}
inline void PrefixConfigCell(ChannelCell &cell)
{
    cell.key = "Config." + cell.key;
    for (auto &child : cell.children)
        PrefixConfigCell(child);
}
inline ChannelCell ConfigCell(const ConfigSetting &setting, bool list)
{
    ChannelCell cell;
    cell.key = "Setting." + StableToken(setting.key);
    cell.label = setting.label;
    if (!list)
        cell.configKey = setting.key;
    else
    {
        // One indexed selector per slot, not hundreds of duplicated knob cells
        // on every track. Enter via native AddChild; turn to browse, In to load.
        ChannelCell selector;
        selector.key = cell.key + ".Select";
        selector.label = setting.kind == ConfigKind::Plugin ? "PLUGIN" : "PRESET";
        selector.configKey = setting.key;
        cell.children.push_back(std::move(selector));
    }
    return cell;
}
inline std::string PluginCategory(const ConfigOption &option)
{
    if (!option.categories.empty())
    {
        const auto hasCategory = [&](const char *name) {
            return option.categories.find(name) != std::string::npos;
        };
        if (hasCategory("Channel Strip & Preamp")) return "CHANNEL";
        if (hasCategory("Compressors & Limiters")) return "DYNAMICS";
        if (hasCategory("Equalization")) return "EQ";
        if (hasCategory("Reverb & Room")) return "REVERB";
        if (hasCategory("Delay") || hasCategory("Modulation")) return "DELAY MOD";
        if (hasCategory("Tape & Saturation")) return "TAPE SAT";
        if (hasCategory("Guitar & Bass")) return "GUITAR";
        if (hasCategory("Mastering")) return "MASTER";
        if (hasCategory("Pitch Correction") || hasCategory("Special Processing")) return "UTILITY";
    }
    std::string name = option.value.scalar + " " + option.label;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto has = [&](std::initializer_list<const char *> words) {
        return std::any_of(words.begin(), words.end(),
                           [&](const char *word) { return name.find(word) != std::string::npos; });
    };
    if (has({"channel strip", "preamp", "pre amp", "voxbox", "century tube", "610-b", "610-a",
             "api vision", "ssl e channel", "neve 1073", "neve 1084"}))
        return "CHANNEL";
    if (has({"compress", "limiter", "leveler", "leveller", "gate", "expander", "1176", "la-2a",
             "la2a", "fairchild", "distressor", "dbx 160", "api 2500", "shadow hills"}))
        return "DYNAMICS";
    if (has({"equalizer", " eq", "eq ", "pultec", "massive passive", "maag", "harrison 32c",
             "bx_digital"}))
        return "EQ";
    if (has({"reverb", "chamber", "plate", "room", "lexicon", "ocean way", "sound city"}))
        return "REVERB";
    if (has({"delay", "echo", "chorus", "flanger", "phaser", "rotary", "tremolo", "vibrato",
             "dimension d"}))
        return "DELAY MOD";
    if (has({"tape", "studer", "ampex", "oxide", "saturat", "culture vulture"}))
        return "TAPE SAT";
    if (has({"guitar", "bass amp", "amplifier", "marshall", "fender", "friedman", "engl", "suhr",
             "gallien", "pedal"}))
        return "GUITAR";
    if (has({"precision", "maximizer", "mastering", "multiband", "k-stereo"}))
        return "MASTER";
    if (has({"auto-tune", "autotune", "pitch", "harmon", "c-vox", "noise", "de-esser", "deesser"}))
        return "UTILITY";
    return "OTHER";
}
inline ChannelCell PluginConfigCell(const ConfigSetting &setting)
{
    ChannelCell cell;
    cell.key = "Setting." + StableToken(setting.key);
    cell.label = setting.label;
    if (setting.options.empty())
        return cell;

    // EUCON's documented Inserts Config hierarchy is folder/category first,
    // then one plug-in choice per knob cell. Keep unload at the first position.
    ChannelCell none;
    none.key = cell.key + ".None";
    none.label = "NONE";
    none.configKey = setting.key;
    none.configChoice = 0;
    cell.children.push_back(std::move(none));

    const std::array<const char *, 10> order{{"CHANNEL", "DYNAMICS", "EQ", "REVERB", "DELAY MOD",
                                               "TAPE SAT", "GUITAR", "MASTER", "UTILITY", "OTHER"}};
    std::map<std::string, std::vector<size_t>> categories;
    for (size_t i = 1; i < setting.options.size(); ++i)
        categories[PluginCategory(setting.options[i])].push_back(i);
    for (const auto *name : order)
    {
        const auto found = categories.find(name);
        if (found == categories.end())
            continue;
        ChannelCell category;
        category.key = cell.key + ".Category." + StableToken(name);
        category.label = name;
        for (const auto index : found->second)
            category.configChoices.push_back(static_cast<int>(index));
        category.configKey = setting.key;
        cell.children.push_back(std::move(category));
    }
    return cell;
}
inline void AppendPresetConfigCells(ChannelCell &parent, const ConfigSetting &setting)
{
    // A preset is a direct fixed action, not a value to browse on one encoder.
    // Put every advertised file on its own cell; EUCON/S3 Page navigation owns
    // pagination across the available OLEDs.
    for (size_t i = 0; i < setting.options.size(); ++i)
    {
        ChannelCell choice;
        choice.key = "Setting." + StableToken(setting.key) + ".Preset." + std::to_string(i);
        choice.label = ConfigDisplayText(setting.options[i].label);
        choice.configKey = setting.key;
        choice.configChoice = static_cast<int>(i);
        choice.configStart = i == 0;
        PrefixConfigCell(choice);
        parent.children.push_back(std::move(choice));
    }
}
inline std::vector<ChannelKnobSet> DescribeConfiguredChannel(const Channel &c, size_t limit, bool enabled)
{
    auto result = DescribeChannel(c, limit);
    if (!enabled || !ControlEligible(c))
        return result;
    if (std::none_of(result.begin(), result.end(),
                     [](const auto &set) { return set.function == ChannelFunction::Console; }))
    {
        ChannelCell entry;
        entry.key = "Console.ConfigHint";
        entry.label = "CONSOLE";
        entry.readOnlyGlobal = "@hint";
        result.push_back({ChannelFunction::Console, {entry}});
    }
    for (auto &set : result)
    {
        std::vector<ChannelCell> config;
        if (set.function == ChannelFunction::Mix)
            config = set.cells;
        if (set.function == ChannelFunction::Input)
            for (const auto &cell : set.cells)
            {
                const bool setting = cell.key == "Input" ||
                                     (cell.lower && (cell.lower->kind == ChannelField::Phantom ||
                                                     cell.lower->kind == ChannelField::Reference ||
                                                     cell.lower->kind == ChannelField::SampleRateConvert ||
                                                     cell.lower->kind == ChannelField::SendPostFader ||
                                                     cell.lower->kind == ChannelField::Mono ||
                                                     cell.lower->kind == ChannelField::TalkToMonitor));
                if (setting)
                    config.push_back(cell);
            }
        if (set.function == ChannelFunction::Console)
        {
            for (const auto &entry : GlobalConfigEntries)
            {
                if (FindConfig(c.configuration, std::string("/") + entry.property))
                    continue;
                ChannelCell cell;
                cell.key = std::string("Global.") + entry.property;
                cell.label = entry.label;
                cell.readOnlyGlobal = entry.property;
                config.push_back(std::move(cell));
            }
            if (c.configuration)
                for (const auto &entry : c.configuration->settings)
                {
                    const auto &s = entry.second;
                    const auto device = c.path.substr(0, c.path.find('/', 9));
                    if (s.kind == ConfigKind::Global ||
                        ((s.kind == ConfigKind::Device || s.kind == ConfigKind::DigitalMirror) &&
                         s.owner == device))
                        config.push_back(ConfigCell(s, false));
                }
        }
        if (set.function == ChannelFunction::Inserts && c.configuration)
            for (const auto &slot : c.insertSlots)
            {
                const auto path = c.path + "/effects/" + slot;
                if (const auto *s = FindConfig(c.configuration, path + "/EffectName"))
                    config.push_back(PluginConfigCell(*s));
                if (const auto *s = FindConfig(c.configuration, path + "/Preset"))
                    for (auto &cell : set.cells)
                        if (cell.lower && cell.lower->kind == ChannelField::InsertPower &&
                            cell.lower->slot == slot)
                            AppendPresetConfigCells(cell, *s);
            }
        if (set.function == ChannelFunction::Unison && c.configuration)
        {
            for (const auto &preamp : c.preamps)
            {
                const auto prefix = preamp.path + "/effects/";
                for (const auto &entry : c.configuration->settings)
                {
                    const auto &s = entry.second;
                    if (s.kind == ConfigKind::Plugin && s.owner == c.path &&
                        s.node.rfind(prefix, 0) == 0 && s.property == "EffectName")
                        config.push_back(PluginConfigCell(s));
                }
            }
            for (auto &cell : set.cells)
                if (cell.lower && cell.lower->kind == ChannelField::UnisonPower)
                    for (const auto &preamp : c.preamps)
                        if (preamp.slot == cell.lower->preamp)
                            if (const auto *s = FindConfig(c.configuration,
                                                          preamp.path + "/effects/" + cell.lower->slot +
                                                              "/Preset"))
                                AppendPresetConfigCells(cell, *s);
        }
        for (auto &cell : config)
            PrefixConfigCell(cell);
        if (!config.empty())
            config.front().configStart = true;
        set.cells.insert(set.cells.end(), config.begin(), config.end());
    }
    return result;
}
} // namespace apollo
