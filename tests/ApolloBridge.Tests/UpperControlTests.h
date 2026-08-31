#pragma once
void UpperControlTests()
{
    // Pin actual directory positions, not the same indices used by callers.
    // The original regression put Input at EQ's parent position.
    const std::array<UpperFunction, 8> standard{
        UpperFunction::Inserts, UpperFunction::Input, UpperFunction::Dynamics, UpperFunction::Eq,
        UpperFunction::Aux,     UpperFunction::Pan,   UpperFunction::Group,    UpperFunction::Mix};
    std::set<UpperFunction> functions;
    std::set<std::wstring> labels;
    for (size_t i = 0; i < UpperDirectoryEntries.size(); ++i)
    {
        const auto &entry = UpperDirectoryEntries[i];
        Check(functions.insert(entry.function).second, "One upper entry per semantic function");
        Check(UpperDirectoryIndex(entry.function) == i, "Named function resolves to its directory slot");
        if (i < standard.size())
        {
            Check(entry.function == standard[i], "Native upper navigation order");
            Check(entry.label[0] != L'\0', "Native function retains a defined name");
        }
        else
            Check(std::find(standard.begin(), standard.end(), entry.function) == standard.end(),
                  "Custom page cannot duplicate standard functions");
        if (entry.label[0] != L'\0')
        {
            Check(labels.insert(entry.label).second, "No duplicate upper navigation labels");
            const std::wstring label(entry.label);
            Check(std::all_of(label.begin(), label.end(), [](wchar_t c) { return c < L'a' || c > L'z'; }),
                  "Upper function labels use consistent uppercase");
        }
    }
    Check(UpperDirectoryIndex(UpperFunction::Input) == 1 && UpperDirectoryIndex(UpperFunction::Eq) == 3 &&
              std::wstring(UpperDirectoryEntries[1].label) == L"INPUT" &&
              std::wstring(UpperDirectoryEntries[3].label) == L"EQ",
          "Input cannot occupy EQ's original parent position");
    Check(UpperDirectoryIndex(UpperFunction::Unison) == 8 &&
              UpperDirectoryIndex(UpperFunction::Console) == 10 &&
              UpperDirectoryIndex(UpperFunction::ControlRoom) == 15,
          "UNISON, quick controls and control room keep existing second-page positions");
    Reject([] { UpperDirectoryIndex(static_cast<UpperFunction>(999)); });
    const auto crText = UpperDirectoryLabels(UpperFunction::ControlRoom);
    Check(crText[0] == L"CR" && crText[1] == L"CR" && crText[2] == L"CONTROL ROOM",
          "Control room has explicit CR short labels and a full long name");
    for (const auto &entry : UpperDirectoryEntries)
    {
        const auto text = UpperDirectoryLabels(entry.function);
        Check(text[0].size() <= 4 && text[1].size() <= 8 && text[2] == entry.label,
              "Upper text variants fit their declared widths and retain the full name");
        Check(UpperDirectoryLabels(entry.function, false) == UpperLabelText{},
              "Unavailable upper entries clear all three text variants");
        if (entry.function != UpperFunction::ControlRoom)
            Check(text[0] == std::wstring(entry.label).substr(0, 4) &&
                      text[1] == std::wstring(entry.label).substr(0, 8),
                  "CR abbreviation leaves all other short labels unchanged");
    }
    auto nodes = ConsoleFixture();
    AddMonitorFixture(nodes);
    const std::string path = "/devices/3/inputs/0";
    AddUnisonFixture(nodes, path);
    const auto state = BuildSnapshot(nodes);
    Check(state.monitors.size() == 1 && DescribeMonitor(state.monitors.front()).size() == 11,
          "Control room includes level mute dim depth mono talk and five source choices");
    const auto monitorCells = DescribeMonitor(state.monitors.front());
    Check(monitorCells[6].label == "Monitor" && monitorCells[6].source == "mon" &&
              monitorCells.back().label == "Cue 4" && monitorCells.back().source == "cue4",
          "Friendly monitor labels retain exact native source values");
    const auto c = state.channels.front();
    Check(c.preamps.size() == 1 && c.preamps[0].unison.size() == 1 && !c.preamps[0].gain,
          "Loaded UNISON is separate from raw preamp gain");
    Check(c.inserts.size() == 1 && c.inserts.front().name == "Fixture Processor",
          "UNISON does not occupy ordinary insert slots");
    const auto layout = DescribeChannel(c);
    const auto quick = std::find_if(layout.begin(), layout.end(),
                                    [](const auto &s) { return s.function == ChannelFunction::Console; });
    Check(quick != layout.end() && quick->cells.size() == 2 && quick->cells[0].key == "UAD.REC" &&
              quick->cells[0].lower == ChannelAddress(ChannelField::RecordPreEffects) &&
              quick->cells[0].lowerInverted && quick->cells[1].key == "UAD.MON",
          "Existing quick controls unchanged by upper and UNISON expansion");
    const auto &u = layout.back();
    Check(u.function == ChannelFunction::Unison && u.cells.size() == 1 &&
              u.cells[0].label == "Fixture UNISON" && u.cells[0].children.size() == 2,
          "Dedicated UNISON page skips read-only meter and preserves native controls");
    Check(DescribeChannel(c, 1).back().cells[0].children.size() == 1, "UNISON honors knob engage limit");
    const ChannelAddress value{ChannelField::UnisonValue, "0", "0", "0"};
    const ChannelAddress step{ChannelField::UnisonStep, "0", "1", "0"};
    const ChannelAddress power{ChannelField::UnisonPower, "0", "", "0"};
    Check(ChannelCommand(c, value, ControlNumber(.75)) ==
              std::string("set " + path + "/preamps/0/effects/0/parameters/0/NormalizedValue/value 0.75") +
                  '\0',
          "UNISON exact nested parameter path");
    Check(ChannelCommand(c, step, Json::Parse("\"Fast\"")).find("StepValue/value \"Fast\"") !=
              std::string::npos,
          "UNISON enum command");
    Check(ChannelCommand(c, power, Json::Parse("false")).find("/preamps/0/effects/0/Power/value false") !=
              std::string::npos,
          "UNISON power is not ordinary insert power");
    Check(NeedsSafetyUnlock(c, value, ControlNumber(.5)) && NeedsSafetyUnlock(c, power, Json::Parse("false")),
          "Analog-coupled UNISON always requires explicit safety confirmation");
    Reject([&] { ChannelCommand(c, {ChannelField::UnisonValue, "0", "0", "../0"}, ControlNumber(.5)); });
    Reject([&] { ChannelCommand(c, {ChannelField::InsertValue, "2", "0", "0"}, ControlNumber(.5)); });
    Reject([&] { ChannelCommand(c, value, ControlNumber(1.1)); });
    Reject([&] { ChannelCommand(c, step, Json::Parse("\"Unknown\"")); });
    const auto subscriptions = SubscriptionPaths(nodes);
    Check(std::find(subscriptions.begin(), subscriptions.end(),
                    FieldNodePath(c, value) + "/StringValue/value") != subscriptions.end(),
          "UNISON engineering text subscribed");
    auto replacement = nodes;
    replacement[path + "/preamps/0/effects/0"]
        .object["properties"]
        .object["EffectInstance"]
        .object["value"]
        .scalar = "111";
    Check(!SameFieldTarget(c, BuildSnapshot(replacement).channels.front(), value),
          "Replacement instance invalidates UNISON gesture");
    auto renamed = nodes;
    renamed[FieldNodePath(c, value)].object["properties"].object["Name"].object["value"].scalar =
        "New parameter";
    Check(!SameFieldTarget(c, BuildSnapshot(renamed).channels.front(), value),
          "Renamed UNISON parameter invalidates gesture");
    auto feedback = c;
    SetFieldFeedback(feedback, value, ControlNumber(.9));
    Check(SameFieldTarget(c, feedback, value) && FieldParameter(feedback, value)->value.Number() == .9,
          "Value feedback keeps parameter identity");
    auto dry = c;
    dry.recordPreEffects->value = Json::Parse("false");
    Check(SameFieldTarget(c, dry, value), "Ordinary REC/MON does not relocate UNISON");
    auto sourceChanged = c;
    sourceChanged.ioType = "Line";
    Check(!SameFieldTarget(c, sourceChanged, value), "UNISON gesture cannot cross input source change");
    auto dual = nodes;
    const auto firstPreamp = path + "/preamps/0";
    dual[path + "/preamps"].object["children"].object["1"] = Json::Parse("{}");
    for (const auto &entry : nodes)
        if (entry.first == firstPreamp || entry.first.rfind(firstPreamp + "/", 0) == 0)
            dual[path + "/preamps/1" + entry.first.substr(firstPreamp.size())] = entry.second;
    const auto pair = BuildSnapshot(dual).channels.front();
    const ChannelAddress second{ChannelField::UnisonValue, "0", "0", "1"};
    Check(pair.preamps.size() == 2 && value != second &&
              FieldNodePath(pair, value) != FieldNodePath(pair, second),
          "Linked preamps retain distinct UNISON parameter identities");
    Check(DescribeChannel(pair).back().cells[0].label == "1 Fixture UNISON" &&
              DescribeChannel(pair).back().cells[1].label == "2 Fixture UNISON",
          "Two preamps have visibly numbered UNISON entries");
    auto disabled = nodes;
    auto bypassed = nodes;
    bypassed[path].object["properties"].object["PGADisabledInLineMode"].object["value"] = Json::Parse("true");
    bypassed[path].object["properties"].object["IOType"].object["value"].scalar = "Line";
    const auto bypassedChannel = BuildSnapshot(bypassed).channels.front();
    Check(!Available(bypassedChannel, value) && !Available(bypassedChannel, power) &&
              DescribeChannel(bypassedChannel).back().cells[0].label == "Fixture UNISON",
          "Line gain bypass preserves loaded UNISON name but exposes no writable controls");
    disabled[FieldNodePath(c, value)].object["properties"].object["NormalizedValue"].object["readonly"] =
        Json::Parse("true");
    Check(!Available(BuildSnapshot(disabled).channels.front(), value), "Read-only UNISON parameter rejected");
    auto empty = nodes;
    empty[path + "/preamps/0/effects/0"]
        .object["properties"]
        .object["EffectInstance"]
        .object["value"]
        .scalar = "0";
    const auto emptyLayout = DescribeChannel(BuildSnapshot(empty).channels.front());
    Check(emptyLayout.back().cells[0].label == "None" && emptyLayout.back().cells[0].children.empty(),
          "Removed UNISON becomes inert None");
}
