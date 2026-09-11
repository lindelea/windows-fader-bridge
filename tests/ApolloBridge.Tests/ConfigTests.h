#pragma once
void ConfigTests()
{
    auto nodes = ConsoleFixture();
    AddMonitorFixture(nodes);
    auto &root = nodes["/"].object["properties"].object;
    root["SampleRate"] =
        Json::Parse(R"({"type":"int","value":96000,"values":[{"value":96000,"string":"96 kHz"}]})");
    root["ClockSource"] = Json::Parse(R"({"type":"string","value":"Internal"})");
    root["PostFaderMetering"] = Json::Parse(R"({"type":"bool","value":false})");
    root["AppCommand"] = Json::Parse(R"({"type":"int","value":123})");
    nodes["/devices/3"].object["properties"].object["FuncSwitchMode"] = Json::Parse(
        R"({"type":"string","value":"Talkback","values":[{"value":"Talkback","string":"Talkback"}]})");
    auto snapshot = BuildSnapshot(nodes);
    Check(snapshot.globalConfig.at("SampleRate") == "96 KHZ", "Config value labels use uppercase OLED text");
    Check(snapshot.globalConfig.at("PostFaderMetering") == "PRE", "Config meter position semantics");
    Check(snapshot.globalConfig.at("PeakHold") == "N/A", "Missing config metadata is not invented");
    Check(!snapshot.globalConfig.count("AppCommand"), "Config preview only exposes allowlisted properties");
    const auto *functionSwitch =
        FindConfig(snapshot.channels.front().configuration, "/devices/3/FuncSwitchMode");
    Check(functionSwitch && functionSwitch->options.size() == 1 &&
              functionSwitch->options.front().label == "TALKBACK" &&
              functionSwitch->options.front().value.scalar == "Talkback",
          "Function switch display is uppercase while the native write value is preserved");
    Check(ConfigReadout(Json::Parse(R"({"value":"bad\ntext"})")) == "N/A", "Control characters rejected");
    Check(ConfigReadout(Json::Parse(R"({"value":{}})")) == "N/A", "Unknown value types rejected");
    const auto subscriptions = SubscriptionPaths(nodes);
    Check(std::find(subscriptions.begin(), subscriptions.end(), "/SampleRate/value") != subscriptions.end(),
          "Global Config value feedback is subscribed");
    Check(std::find(subscriptions.begin(), subscriptions.end(), "/AppCommand/value") == subscriptions.end(),
          "Opaque AppCommand not added to observation");
    Check(ApplyValue(nodes, Json::Parse(R"({"path":"/PostFaderMetering/value","data":true})")),
          "External global changes enter existing observer");
    Check(BuildSnapshot(nodes).globalConfig.at("PostFaderMetering") == "POST", "Global feedback refreshed");
    for (const auto &channel : snapshot.channels)
    {
        const auto normal = DescribeChannel(channel);
        const auto disabled = DescribeConfiguredChannel(channel, 0, false);
        const auto enabled = DescribeConfiguredChannel(channel, 0, true);
        Check(normal.size() == disabled.size(), "Config opt-out preserves existing functions");
        bool consoleFound = false;
        for (const auto &set : enabled)
        {
            std::set<std::string> ids;
            size_t markerCount = 0;
            bool config = false;
            const auto original = std::find_if(normal.begin(), normal.end(),
                                               [&](const auto &s) { return s.function == set.function; });
            for (size_t i = 0; i < set.cells.size(); ++i)
            {
                const auto &cell = set.cells[i];
                Check(ids.insert(cell.key).second, "Normal and Config cells have distinct persistence IDs");
                if (cell.configStart)
                {
                    ++markerCount;
                    config = true;
                    Check(i > 0, "Normal page exists before config boundary");
                }
                if (!config && original != normal.end())
                    Check(i < original->cells.size() && original->cells[i].key == cell.key &&
                              original->cells[i].knob == cell.knob && original->cells[i].lower == cell.lower,
                          "Normal controls retained in original order");
                if (!cell.readOnlyGlobal.empty())
                    Check(!cell.knob && !cell.lower && cell.children.empty(),
                          "Global preview has no command binding");
                if (config && set.function == ChannelFunction::Input)
                    Check(!cell.knob, "Config input settings do not duplicate gain knobs");
            }
            Check(markerCount <= 1, "One entry boundary; physical page size belongs to EUCON");
            if (set.function == ChannelFunction::Console)
            {
                consoleFound = true;
                Check(markerCount == 1, "Console Config exists even without REC/MON controls");
                Check(std::none_of(set.cells.begin(), set.cells.end(),
                                   [](const auto &cell) { return cell.key == "Global.Access" || cell.label == "ACCESS"; }),
                      "Desktop permission state does not consume a Console knob cell");
            }
            if (set.function == ChannelFunction::Aux || set.function == ChannelFunction::Unison)
                Check(markerCount == 0, "No invented loading or send-destination configuration");
        }
        Check(consoleFound, "Global configuration accessible from each eligible strip");
    }
    for (const auto &monitor : snapshot.monitors)
    {
        const auto normal = DescribeMonitor(monitor);
        const auto configured = DescribeConfiguredMonitor(monitor, true);
        Check(DescribeConfiguredMonitor(monitor, false).size() == normal.size(), "CR opt-out unchanged");
        Check(configured.size() > normal.size(), "CR configuration page populated");
        for (size_t i = 0; i < configured.size(); ++i)
        {
            const auto &cell = configured[i];
            Check(cell.configuration == (i >= normal.size()), "CR normal/config boundaries");
            Check(cell.configStart == (i == normal.size()), "CR has a single configuration boundary");
            if (cell.configuration)
                Check(cell.field == MonitorField::DimAmount || cell.field == MonitorField::Source,
                      "CR Config contains monitor settings, not global/channel writes");
        }
    }
    Check(!snapshot.globalConfig.count("ClockLocked") && !snapshot.globalConfig.count("AudioStreaming") &&
              !snapshot.globalConfig.count("IOMapUsingDefault"),
          "Unpictured settings removed");
    root["BufferSize"] = Json::Parse(
        R"({"type":"int","value":64,"values":[{"value":32},{"value":64},{"value":128,"enabled":false}]})");
    auto expanded = BuildConfiguration(nodes);
    const auto *buffer = FindConfig(expanded, "/BufferSize");
    Check(buffer && buffer->options.size() == 2, "Only advertised enabled numeric choices");
    Check(ConfigScalar(ConfigString("A\"B\\C")) == "\"A\\\"B\\\\C\"", "Configuration JSON escaping");
    bool rejected = false;
    try
    {
        ConfigScalar(ConfigString("bad\nset /x 1"));
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    Check(rejected, "Config injection rejected");
    Check(!FindConfig(expanded, "/AppCommand"), "Opaque commands cannot become settings");
    root["BufferSize"].object["readonly"] = Json::Parse("true");
    Check(!FindConfig(BuildConfiguration(nodes), "/BufferSize"), "Read-only settings not writable");
    root["BufferSize"].object.erase("readonly");
    nodes["/plugins"] = Json::Parse(R"({"children":{"7":{},"8":{},"9":{}}})");
    nodes["/plugins/7"] = Json::Parse(
        R"({"properties":{"Type":{"value":"plugin"},"Name":{"value":"Fixture Processor"},"Unison":{"type":"bool","value":true},"Authorized":{"value":true},"Preset":{"values":[{"value":"Neutral","type":"file"},{"value":"Folder","type":"folder"}]}}})");
    nodes["/plugins/8"] = Json::Parse(
        R"({"properties":{"Type":{"value":"plugin"},"Name":{"value":"Not Licensed"},"Unison":{"type":"bool","value":true},"Authorized":{"value":false},"DemoAvailable":{"value":true}}})");
    nodes["/plugins/9"] = Json::Parse(
        R"({"properties":{"Type":{"value":"plugin"},"Name":{"value":"Insert Only"},"Unison":{"type":"bool","value":false},"Authorized":{"value":true},"Preset":{"values":[]}}})");
    const auto feature = ChannelFeatureFixture("/devices/3/inputs/0");
    for (const auto &n : feature)
        nodes[n.first] = n.second;
    nodes["/devices/3/inputs/0/effects/2"].object["properties"].object["Preset"] =
        Json::Parse(R"({"type":"string","value":"Neutral"})");
    const std::string unisonPath = "/devices/3/inputs/0/preamps/0/effects/0";
    nodes["/devices/3/inputs/0/preamps/0/effects"] = Json::Parse(R"({"children":{"0":{}}})");
    nodes[unisonPath] = Json::Parse(
        R"({"properties":{"Type":{"type":"string","value":"effect"},"EffectInstance":{"type":"pointer","readonly":true,"value":0},"EffectName":{"type":"string","value":""},"Preset":{"type":"string","value":""}}})");
    expanded = BuildConfiguration(nodes);
    const auto *load = FindConfig(expanded, "/devices/3/inputs/0/effects/0/EffectName");
    Check(load && load->options.size() == 3 && load->options[0].value.scalar.empty(),
          "Plugin choices include NONE and owned plugin, never trial activation");
    const auto *unisonLoad = FindConfig(expanded, unisonPath + "/EffectName");
    Check(unisonLoad && unisonLoad->label == "UNISON" && unisonLoad->options.size() == 2 &&
              unisonLoad->options[0].label == "NONE" &&
              unisonLoad->options[1].value.scalar == "Fixture Processor",
          "UNISON Config uses native capability plus authorization and keeps NONE first");
    const auto configuredSnapshot = BuildSnapshot(nodes);
    const auto configuredLayout = DescribeConfiguredChannel(configuredSnapshot.channels.front(), 0, true);
    const auto unisonSet = std::find_if(configuredLayout.begin(), configuredLayout.end(),
                                       [](const auto &set) { return set.function == ChannelFunction::Unison; });
    Check(unisonSet != configuredLayout.end() &&
              std::count_if(unisonSet->cells.begin(), unisonSet->cells.end(),
                            [](const auto &cell) { return cell.configStart; }) == 1,
          "Dedicated UNISON knob set has one native Config page boundary");
    const auto unisonConfig = std::find_if(unisonSet->cells.begin(), unisonSet->cells.end(),
                                          [](const auto &cell) { return cell.configStart; });
    Check(unisonConfig != unisonSet->cells.end() && unisonConfig->label == "UNISON" &&
              !unisonConfig->children.empty() && unisonConfig->children.front().label == "NONE",
          "UNISON Config enters the authorized plug-in browser");
    ConfigSetting catalog;
    catalog.kind = ConfigKind::Plugin;
    catalog.key = "/fixture/EffectName";
    catalog.label = "INSERT 1";
    catalog.options = {{ConfigString(""), "NONE", {}},
                       {ConfigString("LA-2A Silver"), "LA-2A", "/plugins/1"},
                       {ConfigString("Ocean Way Studios"), "OCEANWAY", "/plugins/2"},
                       {ConfigString("Studer A800"), "STUDER", "/plugins/3"},
                       {ConfigString("Mystery Processor"), "MYSTERY", "/plugins/4"}};
    const auto browser = PluginConfigCell(catalog);
    Check(browser.children.size() == 5 && browser.children.front().label == "NONE" &&
              browser.children.front().configChoice == 0,
          "Plugin browser keeps NONE first for immediate unload");
    const auto category = [&](const std::string &name) {
        return std::find_if(browser.children.begin(), browser.children.end(),
                            [&](const auto &cell) { return cell.label == name; });
    };
    Check(category("DYNAMICS") != browser.children.end() &&
              category("DYNAMICS")->configChoices.front() == 1 &&
              category("REVERB") != browser.children.end() &&
              category("TAPE SAT") != browser.children.end() &&
              category("OTHER") != browser.children.end(),
          "Plugin browser exposes one filtered selector knob per category");
    Check(PluginShortLabel("UAD Teletronix LA-2A Silver") == "LA2A SIL", "LA-2A OLED label");
    Check(PluginShortLabel("UAD 1176 Rev A") == "1176 A", "1176 OLED label");
    Check(PluginShortLabel("UAD Neve 1073") == "NVE 1073", "1073 OLED label");
    Check(PluginShortLabel("UAD Capitol Chambers") == "CAP CHMB", "Capitol OLED label");
    Check(PluginShortLabel("UAD Ocean Way Studios") == "OCEANWAY", "Ocean Way OLED label");
    ConfigOption nativeCategory{ConfigString("Fixture"), "FIXTURE", {}, "Reverb & Room,Special Processing"};
    Check(PluginCategory(nativeCategory) == "REVERB", "Native UAD category metadata takes precedence");
    const auto *preset = FindConfig(expanded, "/devices/3/inputs/0/effects/2/Preset");
    Check(preset && preset->options.size() == 1 && preset->options[0].label == "NEUTRAL",
          "Only advertised preset files, not folders or invented save commands");
    ConfigSetting manyPresets;
    manyPresets.kind = ConfigKind::Preset;
    manyPresets.key = "/fixture/Preset";
    manyPresets.label = "PRESET";
    for (int i = 0; i < 18; ++i)
        manyPresets.options.push_back({ConfigString("Preset " + std::to_string(i + 1)),
                                       "Preset " + std::to_string(i + 1), {}});
    ChannelCell presetParent;
    AppendPresetConfigCells(presetParent, manyPresets);
    Check(presetParent.children.size() == 18, "Every preset is exposed on its own Config cell");
    for (size_t i = 0; i < presetParent.children.size(); ++i)
    {
        const auto &choice = presetParent.children[i];
        Check(choice.configKey == manyPresets.key && choice.configChoice == static_cast<int>(i) &&
                  choice.configStart == (i == 0) && choice.configChoices.empty(),
              "Preset cells are fixed direct actions with one page boundary");
    }
    nodes["/devices/3/outputs"].object["children"].object["44"] = Json::Parse("{}");
    nodes["/devices/3/outputs/44"] = Json::Parse(
        R"({"properties":{"IOType":{"value":"Cue"},"Name":{"value":"CUE 2"},"Stereo":{"value":true},"Active":{"value":true},"MixToMono":{"type":"bool","value":false},"MixInSource":{"type":"string","value":"mon","values":["mon","cue"]},"OutputDestination":{"type":"string","value":"None","values":[{"value":"None"},{"value":"Line 7-8","enabled":false}]}}})");
    expanded = BuildConfiguration(nodes);
    const auto *cue = FindConfig(expanded, "/devices/3/outputs/44/OutputDestination");
    Check(cue && cue->options.size() == 1, "Unavailable or occupied cue outputs excluded");
    const auto *mono = FindConfig(expanded, "/devices/3/outputs/44/MixToMono");
    Check(mono && mono->options.size() == 2, "Cue mono separate from control-room mono");
    nodes["/devices/3/outputs/44"].object["properties"].object["Active"].object["value"] =
        Json::Parse("false");
    Check(!FindConfig(BuildConfiguration(nodes), "/devices/3/outputs/44/MixToMono"), "Inactive cue removed");
}
