#pragma once
void ConsoleFeatureTests()
{
    auto nodes = ConsoleFixture();
    const auto snapshot = BuildSnapshot(nodes);
    Check(snapshot.channels.size() == 6, "All supported channel families discovered");
    const auto find = [&](const std::string &slot, bool aux = false) -> const Channel & {
        for (const auto &c : snapshot.channels)
            if (c.path == "/devices/3/" + std::string(aux ? "auxs/" : "inputs/") + slot)
                return c;
        throw std::runtime_error("Missing console fixture channel");
    };
    const auto &mic = find("0"), &line = find("4"), &digital = find("8"), &talk = find("12"),
               &aux = find("0", true);
    std::set<uint32_t> colors;
    for (const auto &c : snapshot.channels)
    {
        Check(ControlEligible(c), "Verified channel family writable");
        colors.insert(ChannelColor(c));
        auto renamed = c;
        renamed.name = "Renamed";
        Check(ChannelColor(c) == ChannelColor(renamed), "Color independent of custom name");
        for (const auto &address : ExtensionAddresses(c))
        {
            const auto &p = FieldParameter(c, address);
            if (!p || !p->enabled || p->reportedReadOnly)
                continue;
            Check(Available(c, address), "Discovered feature has a typed command");
            auto disabled = c;
            const_cast<Parameter &>(*FieldParameter(disabled, address)).enabled = false;
            Check(!Available(disabled, address), "Disabled feature omitted");
            Reject([&] { ChannelCommand(disabled, address, p->value); });
        }
    }
    Check(colors.size() == 6 && ChannelColor(talk) == 0xFFFF00 && ChannelColor(aux) == 0x8000FF,
          "Distinct family colors including yellow TALK and purple AUX");
    Check(ChannelColor(mic) == 0x00FF00 && ChannelColor(line) == 0xFF8000 &&
              ChannelColor(digital) == 0x0000FF && ChannelColor(find("16")) == 0xFF0000,
          "Saturated mic line SPDIF virtual palette");
    auto adat = digital;
    adat.ioType = "ADAT";
    Check(ChannelColor(adat) == 0x00FFFF, "Other supported digital input is cyan");
    auto lineSourcePreamp = mic;
    lineSourcePreamp.ioType = "Line";
    Check(ChannelColor(lineSourcePreamp) == 0x00FF00,
          "Preamp-equipped input keeps its family color when selecting line source");
    const ChannelAddress phantom{ChannelField::Phantom, "0"};
    Check(mic.preamps[0].phantom && !mic.reference && line.reference && !line.input,
          "Mic phantom and plain line reference are distinct capabilities");
    Check(NeedsSafetyUnlock(mic, phantom, Json::Parse("true")) &&
              !NeedsSafetyUnlock(mic, phantom, Json::Parse("false")),
          "Phantom activation protected without blocking deactivation");
    Check(ChannelCommand(line, ChannelField::Reference, Json::Parse("true")) ==
              std::string("set /devices/3/inputs/4/Pad/value true") + '\0',
          "+4 dBu engages plain line attenuation");
    Check(digital.sampleRateConvert && !digital.reference && !aux.recordPreEffects && aux.mono &&
              aux.sendPostFader,
          "Digital/AUX feature scopes correct");
    Check(ChannelCommand(talk, ChannelField::Talk, Json::Parse("true")) ==
              std::string("set /TalkbackOn/value true") + '\0',
          "TALK maps only to verified global master");
    Check(NeedsSafetyUnlock(talk, ChannelField::TalkToMonitor, Json::Parse("true")),
          "TB to monitor protected");
    auto routedTalk = talk;
    routedTalk.talkToMonitor->value = Json::Parse("true");
    Check(NeedsSafetyUnlock(routedTalk, ChannelField::Talk, Json::Parse("true")),
          "TALK into speakers protected");
    auto unknownTalk = talk;
    unknownTalk.talkContext.clear();
    Check(!ControlEligible(unknownTalk), "Unverified talkback master remains locked");
    auto unverified = nodes;
    unverified["/"].object["properties"].object["TalkbackMicSelect"].object["value"] = Json::Parse("1");
    Check(!ControlEligible(BuildSnapshot(unverified).channels[3]),
          "Unknown alternate talkback source blocked");
    unverified = nodes;
    unverified["/"].object["properties"].object["TalkbackMaster"] =
        Json::Parse(R"({"type":"string","value":"3"})");
    Check(!ControlEligible(BuildSnapshot(unverified).channels[3]), "Malformed talkback identity blocked");
    const auto auxLayout = DescribeChannel(aux);
    Check(auxLayout.size() == 1 && auxLayout[0].function == ChannelFunction::Input &&
              auxLayout[0].cells.size() == 3 && auxLayout[0].cells[0].lowerInverted,
          "AUX Input offers PRE POST MONO, no duplicate MUTE");
    auto summed = aux;
    summed.mono->value = Json::Parse("true");
    Check(summed.stereo && summed.outputFormat == AudioFormat::Stereo,
          "AUX mono sum does not fabricate mono bus format");
    const auto layout = DescribeChannel(line);
    const auto inserts = std::find_if(layout.begin(), layout.end(),
                                      [](const auto &s) { return s.function == ChannelFunction::Inserts; });
    Check(inserts != layout.end() && inserts->cells.size() == 2,
          "Completely empty rack still publishes slot positions");
    for (size_t i = 0; i < inserts->cells.size(); ++i)
    {
        const auto &cell = inserts->cells[i];
        Check(cell.label == std::to_string(std::stoul(line.insertSlots[i]) + 1) + " None" && !cell.knob &&
                  !cell.lower && cell.children.empty(),
              "Empty slot inert and clearly labeled");
    }
    auto sparse = line;
    sparse.insertSlots = {"0", "3", "9"};
    const auto sparseLayout = DescribeChannel(sparse);
    const auto sparseInserts = std::find_if(sparseLayout.begin(), sparseLayout.end(), [](const auto &s) {
        return s.function == ChannelFunction::Inserts;
    });
    Check(sparseInserts != sparseLayout.end() && sparseInserts->cells[1].label == "4 None" &&
              sparseInserts->cells[2].label == "10 None",
          "Empty insert numbering follows physical slots, not compacted array positions");
    auto &route =
        nodes["/devices/3/inputs/4"].object["properties"].object["OutputDestination"].object["value"];
    for (const auto &pair :
         std::vector<std::pair<std::string, AudioFormat>>{{"Line 1-2", AudioFormat::Stereo},
                                                          {"Line 1", AudioFormat::Mono},
                                                          {"S/PDIF", AudioFormat::Stereo},
                                                          {"Line 7-8", AudioFormat::Unknown},
                                                          {"Invented", AudioFormat::Unknown}})
    {
        route.scalar = pair.first;
        const auto updated = BuildSnapshot(nodes);
        Check(updated.channels[1].outputFormat == pair.second,
              "Output format from verified destination legs, not input width");
    }
    route.scalar = "Line 1-2";
    nodes.erase("/devices/3/outputs/29");
    Check(BuildSnapshot(nodes).channels[1].outputFormat == AudioFormat::Unknown,
          "Missing output leg cannot invent stereo");
    nodes["/devices/3/inputs/12"].object["properties"].object["TalkbackEnabled"].object["value"] =
        Json::Parse("false");
    Check(!ControlEligible(BuildSnapshot(nodes).channels[3]), "Disabled talkback master not controllable");
}
// Included in the pure test translation unit's anonymous namespace.
void ChannelFeatureTests()
{
    const std::string path = "/devices/3/inputs/0";
    auto nodes = Fixture();
    for (const auto &entry : ChannelFeatureFixture(path))
        nodes[entry.first] = entry.second;
    auto snapshot = BuildSnapshot(nodes);
    snapshot.connected = true;
    snapshot.generation = 1;
    snapshot.receivedAt = std::chrono::steady_clock::now();
    const auto c = snapshot.channels.front();
    Check(c.sends.size() == 2 && c.sends.front().slot == "5", "Sends discovered by slot, not assumed six");
    Check(c.output->choices.size() == 2, "Disabled output excluded");
    Check(c.preamps.size() == 1 && c.preamps[0].gain->minimum == 10,
          "Native gain intersects documented physical range");
    Check(c.inserts.size() == 1 && c.inserts[0].slot == "2", "Only loaded plug-ins have parameter pages");
    Check(c.inserts[0].parameters[0].display == "3.2 ms", "Preserve native plug-in display units");
    const auto layout = DescribeChannel(c);
    Check(layout.size() == 5 && layout.back().function == ChannelFunction::Unison,
          "Standard pages plus independent UNISON page");
    Check(layout[0].function == ChannelFunction::Aux && layout[0].cells[0].lowerInverted,
          "AUX lower switch is send-in not mute");
    Check(layout[0].cells[0].children.size() == 1, "Mono send has standard child panner");
    Check(layout[1].function == ChannelFunction::Mix && !layout[1].cells[0].knob &&
              layout[1].cells[0].lowerChoice == "Monitor",
          "Output selection requires switch press");
    Check(layout[3].cells.size() == 2 && layout[3].cells[0].label == "1 None",
          "Real empty insert slots keep their position");
    Check(layout[3].cells.back().children.size() == 2 &&
              layout[3].cells.back().children[1].knob->kind == ChannelField::InsertStep,
          "Discrete plug-in options use native enum, read-only meters omitted");
    Check(DescribeChannel(c, 1)[3].cells.back().children.size() == 1,
          "Respect surface-requested plug-in knob limit");
    const ChannelAddress gain{ChannelField::SendLevel, "5"}, pan{ChannelField::SendPan, "5"},
        bypass{ChannelField::SendBypass, "5"};
    Check(ChannelCommand(c, gain, ControlNumber(-11)) ==
              std::string("set " + path + "/sends/5/Gain/value -11") + '\0',
          "Typed send command");
    Check(DecodeChannelKnob(c, pan, -50).Number() == -.5, "Send pan uses native conversion");
    Check(DecodeChannelKnob(c, {ChannelField::InsertStep, "2", "1"}, 1).String() == "Fast",
          "Step index decoded to exact enum");
    for (double v : {-145.0, 12.1})
        Reject([&] { ChannelCommand(c, gain, ControlNumber(v)); });
    Reject([&] { ChannelCommand(c, bypass, ControlNumber(1)); });
    Reject([&] { ChannelCommand(c, {ChannelField::SendLevel, "../5"}, ControlNumber(0)); });
    Reject([&] { ChannelCommand(c, {ChannelField::Level, "5"}, ControlNumber(0)); });
    Reject([&] { ChannelCommand(c, {ChannelField::PreampGain, "0"}, ControlNumber(9)); });
    Reject([&] { ChannelCommand(c, {ChannelField::PreampGain, "0"}, ControlNumber(20.5)); });
    Reject([&] { ChannelCommand(c, {ChannelField::InsertValue, "2", "0"}, ControlNumber(1.01)); });
    Reject([&] { ChannelCommand(c, {ChannelField::InsertValue, "2", "2"}, ControlNumber(.5)); });
    Reject([&] { DecodeChannelKnob(c, {ChannelField::InsertStep, "2", "1"}, .5); });
    Reject([&] { ChannelCommand(c, ChannelField::Output, Json::Parse("\"Unavailable\"")); });
    Reject([&] { ChannelCommand(c, ChannelField::Input, Json::Parse("\"Dante\"")); });
    auto changed = c;
    changed.stereo = true;
    Reject([&] { ChannelCommand(changed, pan, ControlNumber(0)); });
    Check(DescribeChannel(changed)[0].cells[0].children.empty(), "No fake stereo send-pan control");
    changed = c;
    changed.sends[0].id = "different-bus";
    Check(!SameFieldTarget(c, changed, gain) && !SameExtensionShape(c, changed),
          "Send retarget invalidates gesture");
    changed = c;
    changed.inserts[0].identity = "replacement";
    Check(!SameFieldTarget(c, changed, {ChannelField::InsertValue, "2", "0"}),
          "Same slot replacement is not same plug-in");
    changed = c;
    changed.inserts[0].parameters[0].name = "Different meaning";
    Check(!SameExtensionShape(c, changed), "Parameter relabel changes semantic identity");
    changed = c;
    changed.sends[0].level->value = ControlNumber(-40);
    changed.inserts[0].parameters[0].display = "8 ms";
    Check(SameExtensionShape(c, changed), "Value feedback never rebuilds topology");
    ChannelQueue queue;
    const auto epoch = queue.Arm(snapshot, c.key);
    Check(queue.Submit(gain, ControlNumber(-20), epoch) != 0, "Send queued");
    Check(queue.Submit({ChannelField::SendLevel, "9"}, ControlNumber(-10), epoch) != 0 && queue.Size() == 2,
          "Different send addresses never coalesce together");
    Check(queue.Submit(gain, ControlNumber(-22), epoch) != 0 && queue.Size() == 2, "Same send coalesces");
    snapshot.channels[0].inserts[0].identity = "new-instance";
    Check(!queue.Valid(snapshot), "Plugin replacement revokes armed channel scope");
    auto &properties = nodes[path + "/preamps/0"].object["properties"].object;
    properties["48V"].object["value"] = Json::Parse("true");
    Check(!BuildSnapshot(nodes).channels.front().input,
          "Source switching is unavailable while phantom is active");
    properties["48V"].object["value"] = Json::Parse("false");
    properties["HiZ"].object["value"] = Json::Parse("true");
    changed = BuildSnapshot(nodes).channels.front();
    Check(!changed.input && !changed.preamps[0].pad, "Physical Hi-Z hides ineffective source/Pad controls");
    properties["HiZ"].object["value"] = Json::Parse("false");
    nodes[path].object["properties"].object["IOType"].object["value"] = Json::Parse("\"Line\"");
    nodes[path].object["properties"].object["PGADisabledInLineMode"].object["value"] = Json::Parse("true");
    Check(!BuildSnapshot(nodes).channels.front().preamps[0].gain, "Line gain bypass hides gain knob");
    nodes[path].object["properties"].object["PGADisabledInLineMode"].object["value"] = Json::Parse("false");
    nodes[path + "/preamps/0/effects"] = Json::Parse(R"({"children":{"0":{}}})");
    nodes[path + "/preamps/0/effects/0"] = nodes[path + "/effects/2"];
    Check(!BuildSnapshot(nodes).channels.front().preamps[0].gain, "Unison gain scale not guessed");
    for (const auto &address : ExtensionAddresses(c))
    {
        Check(std::string(FieldName(address)) != "PostFaderOverride", "No guessed per-send pre-post writes");
        const auto &p = FieldParameter(c, address);
        if (!p)
            continue;
        changed = c;
        const_cast<Parameter &>(*FieldParameter(changed, address)).path =
            "/devices/3/outputs/22/CRMonitorLevel/value";
        Reject([&] { ChannelCommand(changed, address, p->value); });
    }
    const auto subscriptions = SubscriptionPaths(nodes);
    for (const auto *suffix :
         {"/sends/5/Gain/value", "/sends/5/Bypass/value", "/effects/2/EffectInstance/value",
          "/effects/2/parameters/0/StringValue/value"})
        Check(std::find(subscriptions.begin(), subscriptions.end(), path + suffix) != subscriptions.end(),
              "Feature feedback and identity subscribed");
    Check(
        ApplyValue(
            nodes,
            Json::Parse(
                R"({"path":"/devices/3/inputs/0/effects/2/parameters/0/NormalizedValue/value","data":0.7})")),
        "Plugin numeric feedback accepted");
    Check(BuildSnapshot(nodes).channels.front().inserts.front().parameters.front().display.empty(),
          "A new normalized value never keeps stale engineering-unit text");
    Check(
        ApplyValue(
            nodes,
            Json::Parse(
                R"({"path":"/devices/3/inputs/0/effects/2/parameters/0/StringValue/value","data":"12 ms"})")),
        "Native display feedback accepted");
    Check(BuildSnapshot(nodes).channels.front().inserts.front().parameters.front().display == "12 ms",
          "Native display restores after authoritative update");
    Check(ApplyValue(
              nodes,
              Json::Parse(R"({"path":"/devices/3/inputs/0/effects/2/EffectInstance/value","data":444444})")),
          "Instance change accepted");
    Check(BuildSnapshot(nodes).channels.front().inserts.front().parameters.empty(),
          "Replacement cannot inherit stale parameters before discovery");
    Check(
        !ApplyValue(
            nodes,
            Json::Parse(
                R"({"path":"/devices/3/inputs/0/effects/2/parameters/0/NormalizedValue/value","data":0.8})")),
        "Old parameter subscription cannot repopulate a deleted instance");
}
