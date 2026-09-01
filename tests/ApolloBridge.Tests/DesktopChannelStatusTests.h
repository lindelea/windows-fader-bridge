#pragma once
void DesktopChannelStatusTests()
{
    Snapshot s;
    s.connected = true;
    s.onlineDevices = 1;
    Channel c;
    c.key = "stable-mic";
    c.name = "MIC/LINE 1";
    c.deviceName = "Test interface";
    c.ioType = "Mic";
    c.destination = "Monitor";
    const auto parameter = [](const char *json) {
        Parameter p;
        p.value = Json::Parse(json);
        return p;
    };
    c.level = parameter("-6");
    c.pan = parameter("-0.25");
    c.mute = parameter("true");
    c.solo = parameter("false");
    c.recordPreEffects = parameter("false");
    c.meters = {{-12.5, -6.0, false}, {-18.0, -4.0, true}};
    s.channels.push_back(c);
    auto rows = DesktopChannels(s, true);
    Check(rows.size() == 1 && rows[0].channel.key == c.key, "Desktop preserves channel identity and order");
    Check(rows[0].format == L"MONO" && rows[0].level == L"-6.0",
          "Desktop shows native format and fader level");
    Check(rows[0].pan == L"25 L", "Desktop pan follows native signed scale");
    Check(rows[0].signal == L"-12.5" && rows[0].peak == L"-4.0",
          "Desktop meters retain native dBFS without offset");
    Check(rows[0].clipped && rows[0].mute.value() && !rows[0].solo.value(),
          "Desktop flags reflect observed values");
    Check(rows[0].record == L"REC", "Record post-effects displays UAD REC");
    s.channels[0].recordPreEffects = parameter("true");
    s.channels[0].stereo = true;
    s.channels[0].pan = parameter("-1");
    s.channels[0].panRight = parameter("1");
    rows = DesktopChannels(s, true);
    Check(rows[0].format == L"STEREO" && rows[0].pan == L"100 L / 100 R",
          "Stereo pan keeps both independent positions");
    Check(rows[0].record == L"MON", "Dry recording displays UAD MON");
    Check(DesktopChannels(s, false).empty(), "Stale data is not presented as live channel status");
    s.connected = false;
    Check(DesktopChannels(s, true).empty(), "Offline channel readings are cleared");
    s.connected = true;
    s.onlineDevices = 0;
    Check(DesktopChannels(s, true).empty(), "No online interface means no live rows");
    s.onlineDevices = 1;
    s.channels[0] = Channel{};
    rows = DesktopChannels(s, true);
    Check(rows[0].level == L"—" && rows[0].signal == L"—" && rows[0].peak == L"—",
          "Missing values are not shown as zero");
    Check(!rows[0].mute && !rows[0].solo && rows[0].record == L"—", "Unsupported flags remain unknown");
    Check(StatusPan(parameter("2")) == L"—" && StatusPan(parameter("null")) == L"—",
          "Invalid pan values are not invented");
    Check(StatusNumber(StatusValue(parameter("\"-6\""))) == L"—",
          "Invalid numeric types are not displayed as readings");
    Check(StatusNumber(-144, true) == L"−∞", "Fader negative infinity remains meaningful");
    Check(StatusNumber(NAN) == L"—", "Non-finite display values are unavailable");
    s.channels.clear();
    for (int i = 0; i < 64; ++i)
    {
        c.key = "channel-" + std::to_string(i);
        s.channels.push_back(c);
    }
    c.monitor = true;
    s.channels.push_back(c);
    rows = DesktopChannels(s, true);
    Check(rows.size() == 64 && rows.back().channel.key == "channel-63",
          "List is not limited to a physical surface bank and excludes monitor strips");
    for (int width : {910, 960, 1240, 1920})
    {
        auto columns = DesktopChannelWidths(width);
        int total = 0;
        for (auto value : columns)
            total += value;
        Check(total == width && columns[1] >= 200, "Responsive columns preserve readable channel names");
    }
}
