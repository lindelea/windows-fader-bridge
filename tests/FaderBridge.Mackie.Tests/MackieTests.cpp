#include "MackieSurface.h"
#include "MackieSettings.h"
#include "CommandCatalog.h"
#include "Windows80Preset.h"
#include "AudioTrackCommandQueue.h"
#include "WindowsMediaTimeline.h"
#include "MackieDisplayClock.h"
#include "MackieJogInput.h"
#include "MackieMediaSeek.h"
#include "MackieCommandText.h"
#include "MackieWorkspace.h"
#include "MackieConnectionPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <random>
#include <set>
#include <fstream>

using namespace mackie;
namespace
{
int checks = 0;
void Check(bool condition, const char* name)
{
    ++checks;
    if (!condition) throw std::runtime_error(name);
}
bool Near(float a, float b) { return std::abs(a - b) < .0002F; }
std::uint32_t Raw(int status, int a, int b = 0) { return status | (a << 8) | (b << 16); }
std::uint32_t Raw(const Bytes& bytes) { return Raw(bytes[0], bytes[1], bytes.size() > 2 ? bytes[2] : 0); }
Track Make(const wchar_t* key = L"app", float volume = .5F)
{
    Track t; t.key = key; t.name = key; t.active = true; t.application = true; t.canPan = true; t.volume = volume;
    return t;
}
struct Harness
{
    std::vector<Bytes> output;
    std::vector<Action> actions;
    Surface surface{[&](const Bytes& b) { output.push_back(b); }, [&](const Action& a) { actions.push_back(a); }};
    Harness() { surface.Update({Make()}, 1000); }
    void Press(int note, int channel = 0, std::uint64_t time = 1000)
    { surface.Input(Raw(0x90 | channel, note, 127), time); surface.Input(Raw(0x80 | channel, note), time); }
    void Touch(int channel, bool down = true) { surface.Input(Raw(0x90, 0x68 + channel, down ? 127 : 0), 1000); }
    bool Has(const Bytes& b) const { return std::find(output.begin(), output.end(), b) != output.end(); }
    bool HasFader(int channel) const { return std::any_of(output.begin(), output.end(), [&](const auto& b) { return b[0] == (0xE0 | channel); }); }
};
void MultipleDevices()
{
    struct Port{unsigned index;std::wstring name;};
    std::vector<Port> ports{{4,L"MCU A"},{9,L"MCU B"}};
    Check(UniquePort(ports,L"MCU A")==4U&&!UniquePort(ports,L"missing"),"auto connection matches exact saved names only");
    ports.push_back({20,L"MCU A"});Check(!UniquePort(ports,L"MCU A"),"ambiguous duplicate port names never auto connect");
    Check(!SafePair(L"EuMidi",L"MCU",L"mcu")&&!SafePair(L"MIDIIN4 (iCON P1-Nano)",L"MIDIOUT4 (iCON P1-Nano)",L"mcu"),"reserved runtime and maintenance ports blocked");
    Check(!SafePair(L"MIDIIN2 (iCON P1-Nano)",L"MIDIOUT3 (iCON P1-Nano)",L"p1-nano")&&SafePair(L"MIDIIN2 (iCON P1-Nano)",L"MIDIOUT2 (iCON P1-Nano)",L"p1-nano"),"saved DAW pairing remains validated");
    Check(SafePair(L"MCU Input",L"MCU Output",L"mcu")&&!SafePair(L"MCU Input",L"MCU Output",L"p1-nano"),"generic profile does not acquire manufacturer behavior");
    ConnectionPolicy a,b;
    Check(a.Due(true,true,100)&&!a.Due(false,true,100)&&!a.Due(true,false,100),"automatic mode is explicit per device and requires saved configuration");
    a.ManualDisconnect();Check(!a.Due(true,true,10000)&&b.Due(true,true,10000),"manual disconnect pauses only that device");
    a.Resume();a.Failed(100);Check(!a.Due(true,true,5099)&&a.Due(true,true,5100),"bounded reconnect retry backoff");
    Harness left,right;std::vector<Track> tracks;
    for(int i=0;i<12;++i)tracks.push_back(Make((L"device-track-"+std::to_wstring(i)).c_str()));
    left.surface.Update(tracks,1000);right.surface.Update(tracks,1000);left.surface.Bank(8);
    Check(left.surface.BankStart()==8&&right.surface.BankStart()==0,"two controllers bank independently");
    left.Touch(0);left.actions.clear();right.actions.clear();
    left.surface.Input(Raw(Fader(0,.3F)),1001);
    Check(left.actions.size()==1&&left.actions[0].key==L"device-track-8"&&right.actions.empty(),"input is routed to its surface and stable identity only");
    right.output.clear();left.output.clear();right.surface.Feedback(1002,true);
    Check(!right.output.empty()&&left.output.empty(),"output callbacks are isolated per surface");
    right.surface.DeferTopology=left.surface.Touched();
    tracks.erase(tracks.begin());right.surface.Update(tracks,1010);
    Check(right.surface.Order().front()==L"device-track-0"&&!right.surface.At(0),"other surface touch defers topology without retargeting offline tracks");
    right.surface.ResetConnection();Check(left.surface.Touched(),"disconnect cannot release another surface touch");
    left.Touch(0,false);right.surface.DeferTopology=false;right.surface.Update(tracks,1100);
    Check(right.surface.Order().front()==L"device-track-1","all-release permits dense online topology again");

    auto root=std::filesystem::temp_directory_path()/(L"wfb-multi-device-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    MackieSettings legacy;legacy.input=L"MCU A";legacy.output=L"MCU A Out";legacy.language=L"en";legacy.ApplyWindows80Preset();
    Check(legacy.autoConnect&&legacy.Save(root/L"settings.txt"),"legacy settings default to automatic mode");
    auto workspace=MackieWorkspace::Load(root);
    Check(workspace.devices==std::vector<std::string>{"primary"}&&workspace.language==L"en","single-device configuration migrates without losing preferences");
    workspace.devices.push_back("device-second");workspace.selected="device-second";workspace.language=L"zh";
    Check(workspace.Save(),"atomic multi-device registry save");
    auto second=MackieSettings::Load(workspace.DevicePath("device-second"));second.deviceName=L"副控台";second.autoConnect=false;second.bindings[15*128]=L"OpenSettings";
    Check(second.Save(),"device settings default save uses their own loaded path");
    auto primary=MackieSettings::Load(root/L"settings.txt");auto restored=MackieSettings::Load(workspace.DevicePath("device-second"));
    Check(primary.bindings==legacy.bindings&&primary.bindings.size()==80&&restored.bindings!=primary.bindings,"editing a second device cannot overwrite primary mappings");
    Check(restored.deviceName==L"副控台"&&!restored.autoConnect&&restored.storage==workspace.DevicePath("device-second"),"device identity automatic mode and storage roundtrip");
    auto loaded=MackieWorkspace::Load(root);Check(loaded.devices==workspace.devices&&loaded.selected=="device-second"&&loaded.language==L"zh","multi-device selection persists");
    Check(loaded.DevicePath("../../outside").empty()&&!MackieWorkspace::ValidId("C:\\file")&&!MackieWorkspace::ValidId(".."),"manifest cannot escape settings directory");
    loaded.devices.clear();loaded.selected.clear();Check(loaded.Save()&&MackieWorkspace::Load(root).devices.empty(),"removing all devices does not remigrate old primary configuration");
    Check(std::filesystem::exists(root/L"settings.txt")&&std::filesystem::exists(workspace.DevicePath("device-second")),"removed device files remain recoverable");
    std::filesystem::remove_all(root);
}
void Codec()
{
    for (int i = 0; i <= FaderMax; ++i)
    {
        const auto b = Fader(7, FaderScalar(i));
        Check(b.size() == 3 && b[0] == 0xE7 && (b[1] | (b[2] << 7)) == i, "14-bit fader roundtrip");
    }
    Check(Fader(8, 1) == Bytes({0xE8, 127, 127}), "master pitch channel");
    Check(Fader(-1, 1).empty() && Fader(9, 1).empty(), "reject invalid fader");
    Check(FaderValue(-1) == 0 && FaderValue(2) == FaderMax && FaderValue(std::numeric_limits<float>::quiet_NaN()) == 0, "safe scalar bounds");
    for (int i = 0; i < 64; ++i) { Check(Delta(i) == i, "positive delta"); Check(Delta(64 | i) == -i, "negative signed magnitude"); }
    Check(Delta(0x40) == 0, "negative zero is not minus one");
    Check(Led(16, true) == Bytes({0x90, 16, 127}) && Led(128, true).empty(), "LED encoding");
    Check(Ring(0, 0, true) == Bytes({0xB0, 0x30, 0x46}), "center ring");
    Check(Ring(7, -1, true) == Bytes({0xB0, 0x37, 1}), "left ring");
    Check(Ring(8, 0, true).empty() && Ring(0, 0, false)[2] == 0, "unavailable ring");
    Check(Lcd(0, "ABC") == Bytes({0xF0, 0, 0, 0x66, 0x14, 0x12, 0, 'A', 'B', 'C', 0xF7}), "MCU LCD envelope");
    Check(Lcd(111, "ABC").size() == 9 && Lcd(112, "A").empty() && Lcd(-1, "A").empty(), "LCD bounds");
    Check(Lcd(0, std::string("\xFF\n"))[7] == '?' && Label(L"苹果音乐").size() == 7, "LCD seven-bit sanitation");
    Check(Label(L"Apple Music") == "Apple  ", "LCD fixed width");
    for (int i = -120; i <= 20; ++i) Check(MeterLevel(static_cast<float>(i)) >= 0 && MeterLevel(static_cast<float>(i)) <= 12, "meter never emits reserved level");
    Check(Meter(7, 12) == Bytes({0xD0, 0x7C}) && Meter(0, 13).empty(), "meter channel and reserved level");
    Check(!ValidShort(Raw(0xF0, 0)) && !ValidShort(Raw(0x90, 128)) && !ValidShort(Raw(0x90, 1, 128)), "malformed data rejected");
    Check(ValidShort(Raw(0xD0, 127)) && ValidShort(Raw(0x80, 16)), "two-byte and noteoff accepted");
}
void PlaybackClock()
{
    constexpr std::int64_t second = 10000000;
    WindowsMediaTimeline t;
    Check(!PlaybackTime(t, true).available, "missing timeline is not a made-up clock");
    t.available = true; t.start = 10 * second; t.end = 310 * second; t.position = 75 * second;
    auto p = PlaybackTime(t, false);
    Check(p.available && p.elapsedSeconds == 65 && p.durationSeconds == 300, "elapsed uses media start/end, not seek bounds");
    t.updatedUtc = 1000 * second; t.sampledUtc = 1003 * second;
    Check(PlaybackTime(t, true).elapsedSeconds == 65, "unknown rate uses reported position without guessing");
    t.rate = 1;
    Check(PlaybackTime(t, true).elapsedSeconds == 68, "playing extrapolates from official update timestamp");
    Check(PlaybackTime(t, false).elapsedSeconds == 65, "pause freezes at authoritative position");
    t.rate = 2;
    Check(PlaybackTime(t, true).elapsedSeconds == 71, "playback speed is respected");
    t.rate = 0;
    Check(PlaybackTime(t, true).elapsedSeconds == 65, "zero rate does not advance");
    t.rate = -1;
    Check(PlaybackTime(t, true).elapsedSeconds == 62, "reverse rate remains bounded");
    t.rate = std::numeric_limits<double>::quiet_NaN();
    Check(PlaybackTime(t, true).elapsedSeconds == 65, "NaN rate cannot contaminate time");
    t.rate = std::numeric_limits<double>::infinity();
    Check(PlaybackTime(t, true).elapsedSeconds == 65, "infinite rate ignored");
    t.rate = 1; t.updatedUtc = t.sampledUtc + second;
    Check(PlaybackTime(t, true).elapsedSeconds == 65, "future timestamp cannot rewind clock");
    t.updatedUtc = 0;
    Check(PlaybackTime(t, true).elapsedSeconds == 65, "missing timestamp is not elapsed since 1601");
    t.updatedUtc = second;
    Check(PlaybackTime(t, true).elapsedSeconds == 300, "projection stops at track end");
    t.position = 5 * second;
    Check(PlaybackTime(t, false).elapsedSeconds == 0, "position before media start clamped");
    t.position = 400 * second;
    Check(PlaybackTime(t, false).elapsedSeconds == 300, "position beyond media end clamped");
    t.position = -1;
    Check(!PlaybackTime(t, true).available, "negative provider position rejected");
    t.position = 75 * second; t.end = t.start;
    Check(!PlaybackTime(t, true).available, "zero-length timeline is unavailable");
    t.end = 0;
    Check(!PlaybackTime(t, true).available, "reversed bounds are unavailable");
    t.start = -1; t.end = 300 * second;
    Check(!PlaybackTime(t, true).available, "invalid negative media start rejected");
    t.start = 0; t.position = std::numeric_limits<std::int64_t>::max(); t.end = t.position;
    t.updatedUtc = 1; t.sampledUtc = std::numeric_limits<std::int64_t>::max();
    Check(std::isfinite(PlaybackTime(t, true).elapsedSeconds), "extreme provider values cannot integer-overflow");
    // No state carries across tracks: seek and replacement are authoritative.
    t = {}; t.available = true; t.end = 300 * second; t.position = 120 * second;
    Check(PlaybackTime(t, false).elapsedSeconds == 120, "paused seek adopts new time");
    t.position = 5 * second;
    Check(PlaybackTime(t, false).elapsedSeconds == 5, "backward seek is not hidden");
    t.end = 50 * second; t.position = 0;
    Check(PlaybackTime(t, false).durationSeconds == 50 && PlaybackTime(t, false).elapsedSeconds == 0, "new song resets duration and time");
}
void TimeDisplay()
{
    Check(TimeText(0) == L"00:00:00" && TimeText(65.9) == L"00:01:05", "whole elapsed seconds, not frames");
    Check(TimeText(3599) == L"00:59:59" && TimeText(3600) == L"01:00:00", "hour carry");
    Check(TimeText(359999.9) == L"99:59:59", "maximum display time");
    for (double value : {-1.0, 360000.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        Check(TimeText(value) == L"--:--:--", "invalid or overflowing time cannot wrap");
    const auto digits = TimeDigits(3661);
    Check(digits == std::array<std::uint8_t, 10>{0x31, 0x30, 0x71, 0x30, 0x71, 0x30, 0x20, 0x20, 0x20, 0x20}, "01.01.01 uses right-to-left digits and decimal flags");
    Check(TimeDigit(0, digits[0]) == Bytes({0xB0, 0x40, 0x31}) && TimeDigit(9, 0x20) == Bytes({0xB0, 0x49, 0x20}), "documented time CC endpoints");
    Check(TimeDigit(-1, 0x20).empty() && TimeDigit(10, 0x20).empty() && TimeDigit(0, 128).empty(), "reject out-of-range digit and MIDI data");
    for (int seconds = 0; seconds < 360000; seconds += 17)
        for (auto digit : TimeDigits(seconds)) Check(digit < 128, "time digit always 7-bit MIDI data");
    Harness h; h.surface.MetersEnabled = false;
    h.surface.MediaAvailable = true; h.surface.TimeDisplaySeconds = 59;
    h.surface.Feedback(1000); h.output.clear();
    h.surface.Feedback(1200);
    Check(h.output.empty(), "stable time not resent every polling tick");
    h.surface.TimeDisplaySeconds = 60; h.surface.Feedback(1300);
    Check(h.output.empty(), "time changes respect 5 Hz cap");
    h.surface.Feedback(1400);
    Check(h.output.size() == 3 && h.Has(TimeDigit(0, 0x30)) && h.Has(TimeDigit(1, 0x30)) && h.Has(TimeDigit(2, 0x71)), "minute carry sends only three changed digits");
    h.output.clear(); h.surface.TimeDisplaySeconds = 60.5; h.surface.Feedback(1600);
    Check(h.output.empty(), "fractional seconds do not flood display");
    h.surface.TimeDisplaySeconds = -1; h.surface.Feedback(1800);
    Check(h.Has(TimeDigit(0, 0x20)) && h.Has(TimeDigit(4, 0x20)), "invalid display source clears old time to blanks");
    h.output.clear(); h.surface.ResetConnection(); h.surface.Feedback(1801);
    for (int i = 0; i < 10; ++i) Check(h.Has(TimeDigit(i, TimeDigits(-1)[i])), "reconnect refreshes all time digits including blanks");
    h.output.clear(); h.Touch(0); h.surface.MediaAvailable = true; h.surface.TimeDisplaySeconds = 120;
    h.surface.Feedback(2001);
    Check(!h.HasFader(0) && h.Has(TimeDigit(0, 0x30)), "clock feedback never bypasses motor touch protection");
    Check(h.actions.empty(), "time feedback never issues playback or audio commands");
}
void IdleClock()
{
    constexpr std::int64_t second = 10000000;
    DisplayClock clock;
    WindowsMediaState media;
    auto shown = clock.Update(media, 0, 86399);
    Check(shown.systemClock && TimeText(shown.seconds) == L"23:59:59", "startup with no media uses local 24-hour clock");
    shown = clock.Update(media, 1000, 0);
    Check(shown.systemClock && TimeText(shown.seconds) == L"00:00:00", "local midnight wraps without affecting timeout");
    Check(clock.Update(media, 2000, 7200).seconds == 7200, "system or timezone adjustment immediately follows Windows");
    Check(clock.Update(media, 2000, -1).seconds == -1 && clock.Update(media, 2000, 86400).seconds == -1, "invalid local input cannot masquerade as elapsed time");
    media.available = true; media.playing = true;
    Check(clock.Update(media, 3000, 12345).systemClock, "playing without readable timeline uses clock");
    media.timeline.available = true; media.timeline.end = 300 * second; media.timeline.position = 65 * second;
    media.sourceAppId = L"player"; media.title = L"Song"; media.artist = L"Artist";
    shown = clock.Update(media, 4000, 12346);
    Check(!shown.systemClock && shown.seconds == 65, "read-only media progress takes priority over clock");
    Check(!media.canSeek && !clock.Update(media, 60000, 12400).systemClock, "playing does not time out even without updates or seek capability");
    media.playing = false;
    Check(!clock.Update(media, 61000, 12401).systemClock, "pause starts its own ten-second hold");
    for (int elapsed = 200; elapsed < 10000; elapsed += 200)
    {
        media.timeline.sampledUtc += second; media.timeline.updatedUtc += second;
        Check(!clock.Update(media, 61000 + elapsed, 12401 + elapsed / 1000).systemClock, "paused snapshot polling cannot shorten or prolong the hold");
    }
    Check(!clock.Update(media, 70999, 12410).systemClock && clock.Update(media, 71000, 12411).systemClock, "pause switches at exactly ten seconds");
    shown = clock.Update(media, 72000, 100);
    Check(shown.systemClock && shown.seconds == 100, "wall-clock correction cannot restart media hold");
    media.timeline.position = 20 * second + second / 4;
    shown = clock.Update(media, 73000, 101);
    Check(!shown.systemClock && shown.seconds == 20.25, "acknowledged paused seek immediately restores progress");
    Check(clock.Update(media, 83000, 111).systemClock, "seek holds progress for a fresh ten seconds");
    media.playing = true;
    Check(!clock.Update(media, 83001, 111).systemClock, "resume immediately switches back even at unchanged position");
    media.playing = false; clock.Update(media, 84000, 112);
    Check(clock.Update(media, 94000, 122).systemClock, "second pause also expires");
    media.title = L"Next song";
    Check(!clock.Update(media, 95000, 123).systemClock, "paused track change starts a fresh hold");
    Check(clock.Update(media, 105000, 133).systemClock, "new track timeout expires");
    media.sourceAppId = L"other player";
    Check(!clock.Update(media, 106000, 134).systemClock, "switching player restores progress despite matching position");
    media.timeline.available = false;
    Check(clock.Update(media, 106001, 134).systemClock, "losing timeline immediately falls back instead of holding stale progress");
    media.timeline.available = true;
    Check(!clock.Update(media, 106002, 134).systemClock, "timeline recovery resets hold");
    media.available = false;
    Check(clock.Update(media, 106003, 134).systemClock, "session unavailable ignores stale populated timeline");
    media.available = true; media.timeline.end = 400000 * second; media.timeline.position = 360000 * second;
    Check(clock.Update(media, 106004, 134).systemClock, "unrepresentable elapsed time falls back to system clock");

    Harness h; h.surface.MediaAvailable = false; h.surface.MetersEnabled = false;
    media = {};
    h.surface.TimeDisplaySeconds = clock.Update(media, 200000, 45296).seconds;
    h.surface.Feedback(200000);
    for (int i = 0; i < 10; ++i) Check(h.Has(TimeDigit(i, TimeDigits(45296)[i])), "clock feedback works without a media session");
    Check(h.Has(Led(0x5E, false)) && h.Has(Led(0x5D, false)), "system clock does not invent Transport state");
    h.output.clear(); h.surface.Feedback(200200); Check(h.output.empty(), "unchanged clock second sends no traffic");
    h.surface.TimeDisplaySeconds = clock.Update(media, 201000, 45297).seconds;
    h.surface.Feedback(201000);
    Check(h.output == std::vector<Bytes>{TimeDigit(0, 0x37)}, "next clock second sends only changed digit");
    h.output.clear(); h.Touch(0); h.surface.TimeDisplaySeconds = 45298; h.surface.Feedback(202000, true);
    Check(!h.HasFader(0) && h.Has(TimeDigit(0, 0x38)) && h.actions.empty(), "idle clock respects touch and never changes audio or playback");
}
void Interaction()
{
    Harness h;
    h.surface.Input(Raw(Fader(0, .8F)), 1000);
    Check(h.actions.empty(), "motor echo without touch cannot write audio");
    h.Touch(0); h.surface.Input(Raw(Fader(0, .8F)), 1001);
    Check(h.actions.size() == 1 && h.actions.back().kind == ActionKind::Volume && Near(h.actions.back().value, .8F), "touched fader writes");
    h.surface.Feedback(1001, true);
    Check(!h.HasFader(0) && h.HasFader(1), "motor excluded only for touched strip");
    Check(!h.surface.Bank(8) && !h.surface.Select(L"app"), "bank and selection locked while touching");
    h.Press(0x32); h.surface.Input(Raw(Fader(0, .8F)), 1001);
    Check(h.actions.back().kind == ActionKind::Volume, "flip cannot change touched fader role");
    h.Touch(0, false); h.output.clear(); h.surface.Feedback(1002);
    Check(h.Has(Fader(0, .8F)), "release reconciles pending position");
    h.output.clear(); h.surface.Feedback(1300);
    Check(h.Has(Fader(0, .5F)), "failed write expires to Windows truth");
    h.surface.RequireTouch = false; h.surface.Input(Raw(Fader(0, .6F)), 1400);
    Check(Near(h.actions.back().value, .6F), "explicit touchless mode");
    h.surface.ResetConnection(); Check(!h.surface.Touched(), "reconnect clears interaction");

    Harness e;
    e.surface.Input(Raw(0xB0, 0x11, 1), 1000); e.surface.Input(Raw(0xB0, 0x11, 1), 1001);
    Check(e.actions.size() == 2 && Near(e.actions.back().value, .04F), "rapid encoders accumulate before audio acknowledgement");
    e.surface.Input(Raw(0xB0, 0x11, 0x41), 1002);
    Check(Near(e.actions.back().value, .02F), "encoder reversal preserves signed delta");
    e.surface.Input(Raw(0xB0, 0x11, 0x40), 1003); Check(e.actions.size() == 3, "zero delta ignored");
    e.Press(0x21); Check(e.actions.back().kind == ActionKind::Pan && e.actions.back().value == 0, "pan push centers");
    e.Press(0x28); e.surface.Input(Raw(0xB0, 0x11, 2), 1100);
    Check(e.actions.back().kind == ActionKind::Pan && Near(e.actions.back().value, .04F), "track assignment cannot change encoder 2 Pan role");
    e.Press(0x2A); e.Press(0x32); e.Touch(0); e.surface.Input(Raw(Fader(0, 1)), 1100);
    Check(e.actions.back().kind == ActionKind::Volume && e.actions.back().value == 1, "flip cannot change volume fader role");
    e.surface.Input(Raw(0xB0, 0x11, 1), 1101);
    Check(e.actions.back().kind == ActionKind::Pan, "encoder stays Pan after Flip");
    e.Press(0x21); Check(e.actions.back().kind == ActionKind::Pan && e.actions.back().value == 0, "push still centers after assignment keys");
    e.Touch(0, false); e.surface.Feedback(1200, true);
    Check(e.Has(Led(0x32, false)) && e.Has(Led(0x28, false)) && e.Has(Led(0x2A, true)), "fixed-role LEDs keep Flip off");
    auto mono = Make(); mono.canPan = false; e.surface.Update({mono}, 1300);
    const auto count = e.actions.size(); e.Press(0x28); e.surface.Input(Raw(0xB0, 0x11, 1), 1301); e.Press(0x21);
    Check(e.actions.size() == count, "unsupported Pan stays inactive, never falls back to volume");
}
void Buttons()
{
    Harness h;
    h.surface.Input(Raw(0x90, 16, 127), 1000); h.surface.Input(Raw(0x90, 16, 127), 1001);
    Check(h.actions.size() == 1 && h.actions.back().kind == ActionKind::Mute && h.actions.back().value == 1, "debounced mute press");
    h.surface.Input(Raw(0x90, 16, 0), 1002); h.surface.Input(Raw(0x90, 16, 127), 1003);
    Check(h.actions.size() == 2 && h.actions.back().value == 0, "note velocity zero releases and rapid retoggle");
    h.Press(0); Check(h.actions.size() == 2, "application record does not switch endpoint");
    h.Press(8); Check(h.actions.back().kind == ActionKind::Solo, "application solo");
    h.Press(24); Check(h.actions.back().kind == ActionKind::Focus, "select focuses stable key");
    h.Press(0x5A); Check(h.actions.back().kind == ActionKind::ClearSolo, "rude solo clears");
    h.Press(0x5E); Check(h.actions.back().kind == ActionKind::PlayPause, "transport play");
    h.Press(0x5D); Check(h.actions.back().kind == ActionKind::Stop, "transport stop");
    h.Press(0x5B); Check(h.actions.back().kind == ActionKind::Previous, "transport previous");
    h.Press(0x5C); Check(h.actions.back().kind == ActionKind::Next, "transport next");
    h.Press(0x56); Check(h.actions.back().kind == ActionKind::Repeat, "cycle repeat");
    h.surface.Input(Raw(0xB0, 0x3C, 0x42), 1100); Check(h.actions.back().kind == ActionKind::SeekSeconds && Near(h.actions.back().value, -2.F), "jog seeks relative seconds");
    const auto count = h.actions.size(); h.Press(16, 15); Check(h.actions.size() == count, "non-MCU channels left to binding layer");
    auto device = Make(L"mic"); device.application = false; device.defaultSelectable = true; device.defaultDevice = false;
    Harness d; d.surface.RestoreOrder({}); d.surface.Update({device}, 1000);
    d.Press(0); d.Press(0);
    Check(d.actions.size() == 2 && d.actions[0].kind == ActionKind::DefaultDevice && d.actions[1].kind == ActionKind::DefaultDevice, "record is idempotent selection, never deselection");
    d.surface.Feedback(1100, true); Check(d.Has(Led(0, false)) && !d.Has(Led(0, true)), "default LED waits for Windows confirmation");
    d.Press(8); Check(d.actions.size() == 2, "endpoint does not solo-mute other endpoints");
    device.defaultDevice = true; device.peakDb = -10; d.surface.Update({device}, 1200); d.output.clear(); d.surface.Feedback(1200);
    Check(d.Has(Led(0, true)), "external default update reflected");
    device.defaultDevice = false; d.surface.Update({device}, 1300); d.output.clear(); d.surface.Feedback(1300);
    Check(d.Has(Meter(0, MeterLevel(-10))), "non-default input still meters");
}
void CurrentEncoders()
{
    Harness h;
    auto a = Make(L"a", .2F), b = Make(L"b", .7F); b.pan = -.3F;
    h.surface.Update({a, b}, 1000); h.surface.Select(L"b");
    h.surface.Input(Raw(0xB0, 0x10, 2), 1001);
    Check(h.actions.back().key == L"b" && h.actions.back().kind == ActionKind::Volume && Near(h.actions.back().value, .72F), "encoder 1 volume targets selected identity not physical strip 1");
    h.surface.Input(Raw(0xB0, 0x10, 0x41), 1002);
    Check(Near(h.actions.back().value, .71F), "volume relative reversal retains pending target");
    h.surface.Input(Raw(0xB0, 0x11, 2), 1003);
    Check(h.actions.back().key == L"b" && h.actions.back().kind == ActionKind::Pan && Near(h.actions.back().value, -.26F), "encoder 2 Pan serves the same selected channel");
    h.output.clear(); h.surface.Feedback(1004, true);
    Check(h.Has(Ring(0, .71F, true, true)) && h.Has(Ring(1, -.26F, true)), "rings use current-channel values and distinct display modes");
    for (int i = 2; i < 8; ++i) Check(h.Has(Ring(i, 0, false)), "command encoders never display another track Pan");
    const auto count = h.actions.size(); h.Press(0x20);
    Check(h.actions.size() == count, "volume push never jumps to full volume");
    h.Press(0x21); Check(h.actions.back().key == L"b" && h.actions.back().value == 0, "Pan push centers current channel");
    h.Touch(0); Check(h.surface.Selected()->key == L"a", "touch selects actual fader without focus action");
    h.Touch(1); Check(h.surface.Selected()->key == L"a", "second touch cannot retarget an active encoder gesture");
    h.surface.Input(Raw(0xB0, 0x10, 1), 1010);
    Check(h.actions.back().key == L"a" && Near(h.actions.back().value, .21F), "encoder uses first touched stable identity");
    h.surface.Update({b}, 1011); const auto offline = h.actions.size();
    h.surface.Input(Raw(0xB0, 0x10, 1), 1012); h.Press(0x21);
    Check(h.actions.size() == offline && !h.surface.Selected(), "offline touched current target is inert");
    h.Touch(0, false); h.Touch(1, false);
    Check(h.surface.Selected()->key == L"b", "last release resolves remaining online selection");
    h.surface.RequireTouch = false; h.surface.Update({a, b}, 1200);
    h.surface.Input(Raw(Fader(1, .4F)), 1201);
    Check(h.surface.Selected()->key == L"a", "explicit touchless fader input also supplies current identity");
    h.surface.Input(Raw(0xB0, 0x10, 63), 1202); Check(h.actions.back().value == 1, "volume encoder upper clamp");
    h.surface.Input(Raw(0xB0, 0x10, 127), 1203);
    h.surface.Input(Raw(0xB0, 0x10, 127), 1204); Check(h.actions.back().value == 0, "volume encoder lower clamp");
    std::vector<Track> many; for (int i = 0; i < 18; ++i) many.push_back(Make((L"ch" + std::to_wstring(i)).c_str()));
    h.surface.Update(many, 1400); h.surface.Select(L"ch2"); h.surface.Bank(8);
    Check(h.surface.Selected()->key == L"ch10", "bank retains selected strip offset for current controls");
    h.surface.Input(Raw(0xB0, 0x10, 1), 1401); Check(h.actions.back().key == L"ch10", "encoder follows bank target");
    h.surface.Bank(8); Check(h.surface.Selected()->key == L"ch17", "partial last bank selects an online channel");
    Check(!h.surface.ChannelCommand(ActionKind::DefaultDevice, 0, 1402), "app cannot become default endpoint");
    Check(h.surface.ChannelCommand(ActionKind::PlayPause, 0, 1402) && h.actions.back().strictTarget && h.actions.back().key == L"ch17",
        "current-channel media explicitly forbids global fallback");
    auto mono = Make(L"mono"); mono.application = false; mono.canPan = false; mono.defaultSelectable = true;
    h.surface.Update({mono}, 1500);
    Check(!h.surface.ChannelCommand(ActionKind::Pan, .02F, 1501) && !h.surface.ChannelCommand(ActionKind::Solo, 0, 1501) &&
        !h.surface.ChannelCommand(ActionKind::PlayPause, 0, 1501), "unsupported channel command never falls back to another app");
    Check(h.surface.ChannelCommand(ActionKind::DefaultDevice, 0, 1501), "device command targets selected endpoint");
    Check(!h.surface.ChannelCommand(ActionKind::Volume, std::numeric_limits<float>::quiet_NaN(), 1501), "reject nonfinite channel delta");

    Harness custom;
    for (int i = 2; i < 8; ++i)
    {
        custom.surface.Input(Raw(0xB0, 0x10 + i, 63), 2000);
        Check(custom.actions.back().kind == ActionKind::Encoder && custom.actions.back().encoder == i && custom.actions.back().value == 1, "custom right is one gesture, not accelerated command burst");
        const auto n = custom.actions.size(); custom.surface.Input(Raw(0xB0, 0x10 + i, 63), 2099);
        Check(custom.actions.size() == n, "rotary discrete command rate bounded");
        custom.surface.Input(Raw(0xB0, 0x10 + i, 0x41), 2100);
        Check(custom.actions.back().encoder == i && custom.actions.back().value == -1, "custom left dispatch");
        const auto zero = custom.actions.size(); custom.surface.Input(Raw(0xB0, 0x10 + i, 0x40), 2200);
        Check(custom.actions.size() == zero, "custom negative zero is inert");
        custom.surface.Input(Raw(0x90, 0x20 + i, 127), 2201);
        Check(custom.actions.back().encoder == i && custom.actions.back().value == 0, "custom encoder push separated from turn");
        const auto down = custom.actions.size(); custom.surface.Input(Raw(0x90, 0x20 + i, 127), 2202);
        Check(custom.actions.size() == down, "encoder push debounced");
        custom.surface.Input(Raw(0x90, 0x20 + i, 0), 2203); custom.Press(0x20 + i);
        Check(custom.actions.size() == down + 1, "zero-velocity release rearms encoder push");
    }
    custom.surface.ResetConnection(); custom.surface.Input(Raw(0xB0, 0x12, 1), 1);
    Check(custom.actions.back().encoder == 2 && custom.actions.back().value == 1, "reconnect clears rotary throttling");
    MackieEncoderBindings bindings{};
    Check(!EncoderBinding(bindings, 2, 1), "custom encoders default unassigned");
    bindings[0] = {L"Channel.Previous", L"Channel.Next", L"Channel.PlayPause"};
    Check(*EncoderBinding(bindings, 2, -1) == L"Channel.Previous" && *EncoderBinding(bindings, 2, 1) == L"Channel.Next" &&
        *EncoderBinding(bindings, 2, 0) == L"Channel.PlayPause", "independent left right push binding resolution");
    Check(!EncoderBinding(bindings, 0, 1) && !EncoderBinding(bindings, 1, 0) && !EncoderBinding(bindings, 8, 1), "fixed encoders cannot be overridden");
    bindings[1][0] = L"OpenTaskManager"; bindings[1][1] = L"run-any-script";
    Check(EncoderBinding(bindings, 3, -1) && !EncoderBinding(bindings, 3, 1), "Windows commands allowed but arbitrary text rejected");
    std::set<std::wstring> ids;
    for (const auto& command : MackieChannelCommands)
        Check(ids.insert(command.id).second && !FindMackieCommand(command.id) && FindMackieChannelCommand(command.id) == &command, "current-channel command IDs unique and separate from existing 186 commands");
}
void Identity()
{
    Harness h; std::vector<Track> tracks;
    for (int i = 0; i < 25; ++i) tracks.push_back(Make((L"key" + std::to_wstring(i)).c_str()));
    h.surface.RestoreOrder({}); h.surface.Update(tracks, 1000);
    Check(h.surface.Order().size() == 25 && h.surface.At(0)->key == L"key0", "eight strips not application capacity");
    h.surface.Bank(8); Check(h.surface.At(0)->key == L"key8", "bank eight");
    h.surface.Bank(1); Check(h.surface.At(0)->key == L"key9", "channel one");
    std::reverse(tracks.begin(), tracks.end()); h.surface.Update(tracks, 1100);
    Check(h.surface.At(0)->key == L"key9", "enumeration reversal preserves assignment");
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](auto& t) { return t.key == L"key9"; }), tracks.end());
    h.surface.Update(tracks, 1200); Check(h.surface.At(0)->key == L"key10" && h.surface.Order().size() == 24, "disappearance removes gap automatically");
    tracks.push_back(Make(L"new")); tracks.push_back(Make(L"key9")); h.surface.Update(tracks, 1300);
    Check(h.surface.At(0)->key == L"key10" && h.surface.Order().back() == L"key9", "returning app appends without moving existing tracks");
    h.surface.Select(L"key24"); Check(h.surface.BankStart() == 16 && h.surface.At(7)->key == L"key24", "selection reveals correct bank after compaction");
    h.surface.Bank(100); Check(h.surface.BankStart() == 24 && !h.surface.At(7), "last page bounded");
    h.surface.Bank(-100); Check(h.surface.BankStart() == 0, "first page bounded");
    h.surface.RestoreOrder({L"x", L"x", L"", L"key0"}); Check(h.surface.Order().size() == 2, "persistent identities deduplicated");
    h.surface.Update({Make(L"key0"), Make(L"new")}, 1400);
    Check(h.surface.Order() == std::vector<std::wstring>{L"key0", L"new"}, "first snapshot prunes stale persisted keys");

    Harness c; auto first = Make(L"a"), second = Make(L"b"), third = Make(L"c");
    third.pan = -.7F; third.volume = .3F; third.muted = third.solo = true;
    c.surface.Update({first, second, third}, 1000); c.surface.Select(L"c");
    c.surface.Input(Raw(0xB0, 0x11, 1), 1001); // Encoder 2 targets selected c, not strip 2.
    c.Touch(1); c.Touch(2);
    c.surface.Update({third}, 1050);
    Check(c.surface.Order().size() == 3 && !c.surface.At(1) && c.surface.At(2)->key == L"c", "touch freezes layout while offline targets become inert");
    const auto offlineCount = c.actions.size(); c.surface.Input(Raw(Fader(1, 1)), 1051);
    Check(c.actions.size() == offlineCount, "offline touched strip cannot write another track");
    c.surface.Input(Raw(Fader(2, .6F)), 1052);
    Check(c.actions.back().key == L"c", "surviving touch keeps its logical target");
    c.Touch(1, false); Check(c.surface.Order().size() == 3, "first release waits for remaining touched faders");
    c.output.clear(); c.surface.Feedback(1053, true); Check(!c.HasFader(2), "compaction wait still suppresses touched motor");
    c.Touch(2, false);
    Check(c.surface.Order() == std::vector<std::wstring>{L"c"} && c.surface.Selected()->key == L"c", "last release compacts immediately and selection follows identity");
    c.output.clear(); c.surface.Feedback(1054);
    Check(c.Has(Ring(1, -.68F, true)) && c.Has(Fader(0, .6F)) && c.Has(Led(16, true)) && c.Has(Led(8, true)), "Pan pending volume mute and solo feedback move with track");
    c.surface.Input(Raw(0xB0, 0x11, 1), 1055);
    Check(c.actions.back().key == L"c" && Near(c.actions.back().value, -.66F), "next encoder gesture targets compacted identity");
    c.surface.Update({}, 1100); c.output.clear(); c.surface.Feedback(1100);
    Check(c.surface.Order().empty() && !c.surface.Selected() && c.surface.BankStart() == 0 && c.Has(Fader(0, 0)) && c.Has(Ring(0, 0, false)), "all offline clears selection bank and feedback");
    c.surface.MetersEnabled = false; c.surface.Update({}, 1101); c.output.clear(); c.surface.Feedback(1101);
    Check(c.output.empty(), "empty snapshots do not force repeated repaint");
    c.surface.Update({Make(L"c")}, 1200); c.output.clear(); c.surface.Feedback(1200);
    Check(c.Has(Ring(1, 0, true)) && c.Has(Fader(0, .5F)), "returning track does not inherit stale pending values");

    Harness page; page.surface.Update(tracks, 1000); page.surface.Bank(24);
    page.surface.Update({Make(L"key0"), Make(L"key1")}, 1100);
    Check(page.surface.BankStart() == 0 && page.surface.At(0), "deleting last bank returns to a populated page");
    page.Touch(0); page.surface.Update({Make(L"key1")}, 1200); page.surface.ResetConnection();
    Check(page.surface.Order() == std::vector<std::wstring>{L"key1"}, "disconnect releases touch and completes deferred cleanup");

    Harness m; auto a = Make(L"output-a"), b = Make(L"output-b"); a.master = true;
    m.surface.RestoreOrder({}); m.surface.Update({a, b}, 1000); m.Touch(8);
    a.master = false; b.master = true; m.surface.Update({b, a}, 1050);
    m.surface.Input(Raw(Fader(8, .8F)), 1060);
    Check(m.actions.back().key == L"output-a", "master gesture freezes logical endpoint");
    m.output.clear(); m.surface.Feedback(1060, true); Check(!m.HasFader(8), "changed master never drives touched motor");
    m.Touch(8, false); m.Touch(8); m.surface.Input(Raw(Fader(8, .7F)), 1200);
    Check(m.actions.back().key == L"output-b", "next master gesture follows current default");
    m.surface.Update({a}, 1250); const auto count = m.actions.size(); m.surface.Input(Raw(Fader(8, 1)), 1251);
    Check(m.actions.size() == count, "disconnected touched endpoint never retargets");
}
void Feedback()
{
    Harness h; h.surface.Feedback(1000, true); h.output.clear(); h.surface.Feedback(1001);
    Check(h.output.empty(), "unchanged feedback is silent before meter refresh");
    h.surface.Feedback(1050); Check(h.output.size() == 8, "meter packets refresh on schedule");
    auto t = Make(); t.volume = .7F; t.muted = true; t.solo = true; t.peakDb = 0;
    h.surface.Update({t}, 1100); h.surface.AnySolo = true; h.output.clear(); h.surface.Feedback(1100);
    Check(h.Has(Fader(0, .7F)) && h.Has(Led(16, true)) && h.Has(Led(8, true)), "Windows changes return to motor and LEDs");
    Check(h.Has(Meter(0, 14)) && h.Has(Meter(0, 12)), "overload separate from peak");
    t.peakDb = -20; h.surface.Update({t}, 1200); h.output.clear(); h.surface.Feedback(1200);
    Check(h.Has(Meter(0, 15)), "overload clear");
    h.surface.LcdEnabled = false; h.surface.MetersEnabled = false; h.output.clear(); h.surface.Feedback(1300, true);
    Check(std::none_of(h.output.begin(), h.output.end(), [](auto& b) { return b[0] == 0xF0 || b[0] == 0xD0; }), "capability switches suppress optional packets");
    h.Touch(0); h.surface.Input(Raw(Fader(0, .6F)), 1400); h.Touch(0, false);
    t.volume = .6F; h.surface.Update({t}, 1410); h.surface.Feedback(1410);
    t.volume = .2F; h.surface.Update({t}, 1420); h.output.clear(); h.surface.Feedback(1420);
    Check(h.Has(Fader(0, .2F)), "acknowledged pending write cannot hide later external changes");
    Pending p{.8F, 200}; Check(p.Resolve(.2F, 100, .001F) == .8F && p.Resolve(.3F, 201, .001F) == .3F, "pending expiry");
}
void Settings()
{
    for (int channel = 0; channel < 16; ++channel) for (int note = 0; note < 128; ++note)
        Check(MackieSettings::Bindable(channel, note) == (channel != 0 || (note >= 0x36 && note < 0x68 && !(note >= 0x60 && note <= 0x65))), "binding cannot override core MCU controls");
    Check(IconDawPort(L"iCON P1-Nano") == 1 && IconDawPort(L"MIDIIN4 (iCON P1-Nano)") == 4 &&
        IconDawPort(L"MIDIOUT3 (iCON P1-Nano)") == 3 && IconDawPort(L"Mackie Control") == 0, "profile port roles isolated from codec");
    std::set<std::wstring> ids;
    for (const auto& c : MackieCommands) Check(ids.insert(c.id).second && FindMackieCommand(c.id) == &c, "command IDs unique and resolvable");
    Check(ids.size() == 186 && !FindMackieCommand(L"arbitrary shell command"), "catalog is allowlisted");
    for (const auto& command : MackieCommands)
    {
        const auto translated = ui::Translated(command.id);
        Check(translated && *translated->name && *translated->description, "every Windows command has a project-owned Chinese name and explanation");
        Check(ui::Name(command.id, false) == command.label && !ui::Name(command.id, true).empty(), "language selection preserves canonical command identity");
        Check(ui::Category(command.category, true) != command.category, "every command category is localized");
        Check(ui::Matches(command.id, command.label) && ui::Matches(command.id, translated->name), "catalog is searchable in both languages");
    }
    Check(ui::Matches(L"OpenTaskManager", L"TASK manager") && ui::Matches(L"OpenTaskManager", L"任务 Manager") &&
        !ui::Matches(L"OpenTaskManager", L"不存在的命令"), "search is case-insensitive, token-based and bilingual");
    Check(ui::Name(L"",true)==L"未分配" && ui::Name(L"",false)==L"Unassigned" &&
        ui::Name(L"Channel.PlayPause",false)==L"Play / pause", "custom control names and empty mappings are localized");
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    const auto path = std::filesystem::path(temp) / (L"wfb-mackie-settings-test-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    MackieSettings original; original.input = L"输入 \"MCU\""; original.output = L"Output"; original.profile = L"p1-nano";
    original.language = L"en"; original.closeToTray = false;
    original.trackOrder = {L"应用:Apple", L"ENDPOINT:one"}; original.bindings[15 * 128 + 1] = L"MonoAudio"; original.bindings[0] = L"OpenRun";
    original.encoders[0] = {L"Channel.Previous", L"Channel.Next", L"Channel.PlayPause"};
    original.encoders[5] = {L"ZoomOut", L"ZoomIn", L"ZoomReset"};
    original.jog = {3}; original.cursorTicks = {1, 2, 4, 8, 2};
    original.cursorCommands[4] = {L"PreviousVirtualDesktop", L"NextVirtualDesktop"};
    for (int axis = 0; axis < 4; ++axis) for (int dir = 0; dir < 2; ++dir)
        original.cursorCommands[axis][dir] = CursorCommands[axis * 2 + dir].id;
    Check(original.Save(path), "atomic configuration save");
    const auto restored = MackieSettings::Load(path);
    Check(restored.input == original.input && restored.trackOrder == original.trackOrder && restored.profile == original.profile, "UTF8 settings roundtrip");
    Check(restored.language==L"en" && !restored.closeToTray, "desktop language and close-to-tray preferences survive restart");
    Check(restored.bindings.size() == 1 && restored.bindings.at(15 * 128 + 1) == L"MonoAudio", "unsafe binding ignored when loading");
    Check(restored.encoders == original.encoders, "six encoder mappings persist without changing MIDI button bindings");
    Check(restored.jog.seekSeconds == 3 && restored.cursorTicks == original.cursorTicks &&
        restored.cursorCommands == original.cursorCommands, "all eight cursor commands and four sensitivities roundtrip");
    { std::ofstream file(path, std::ios::trunc); file << "version 1\njog 0 0 0 0\njogZoom 0 \"JogNavigate\"\njogZoom 9 \"ZoomIn\"\njogZoom 1 \"arbitrary-shell\"\nbind 15 100 \"JogFocus\"\n"; }
    const auto invalidJog = MackieSettings::Load(path);
    Check(invalidJog.jog.Valid() && invalidJog.jog.seekSeconds == 1 && invalidJog.cursorCommands == MackieSettings{}.cursorCommands, "invalid Jog settings rejected without recursion");
    Check(invalidJog.bindings.empty(), "retired Focus command does not re-enable a custom mode");
    { std::ofstream file(path, std::ios::trunc); file <<
        "jog 3 2 8 4\nbind 0 96 \"OpenRun\"\nbind 0 97 \"OpenSettings\"\nbind 0 100 \"OpenRun\"\nbind 0 101 \"OpenRun\"\n"
        "jogZoom 0 \"ZoomOut\"\njogZoom 1 \"ZoomIn\"\ncursor 0 0 \"\"\ncursor 3 1 \"Cursor.Right\"\n"
        "cursor -1 0 \"OpenRun\"\ncursor 5 0 \"OpenRun\"\ncursor 0 9 \"OpenRun\"\ncursor 1 0 \"JogNavigate\"\n"
        "cursorTicks 0 0\ncursorTicks 9 4\ncursorTicks 2 8\nbind 15 79 \"ClipboardHistory\"\n"; }
    const auto migrated = MackieSettings::Load(path);
    Check(migrated.jog.seekSeconds == 3 && migrated.cursorCommands[0][0].empty() &&
        migrated.cursorCommands[0][1] == L"OpenSettings" && migrated.cursorCommands[3][0] == L"ZoomOut" &&
        migrated.cursorCommands[3][1] == L"Cursor.Right", "legacy mappings migrate but explicit directions and empty choices win");
    Check(migrated.cursorCommands[1][0].empty() && migrated.cursorTicks == std::array<int, 5>{1, 1, 8, 1, 1} &&
        migrated.bindings.size() == 1 && migrated.bindings.at(15 * 128 + 79) == L"ClipboardHistory", "protected Zoom/Scrub and malformed records cannot shadow native input");
    Check(migrated.Save(path) && MackieSettings::Load(path).cursorCommands == migrated.cursorCommands, "migration roundtrip preserves explicit empty choices");
    Check(migrated.cursorCommands[4] == std::array<std::wstring, 2>{L"Jog.SeekBack", L"Jog.SeekForward"},
        "older four-axis settings keep default playback Jog");
    { std::ofstream file(path, std::ios::trunc); file <<
        "cursor 4 0 \"\"\ncursor 4 1 \"Jog.SeekBack\"\ncursor 0 0 \"Jog.SeekForward\"\n"
        "cursor 4 9 \"OpenRun\"\ncursor 5 0 \"OpenRun\"\ncursorTicks 4 8\n"; }
    const auto customJog = MackieSettings::Load(path);
    Check(customJog.cursorCommands[4][0].empty() && customJog.cursorCommands[4][1] == L"Jog.SeekBack" &&
        customJog.cursorTicks[4] == 8 && customJog.cursorCommands[0][0].empty(), "Jog supports explicit empty and reversed seek; seek commands cannot leak to Move");
    Check(customJog.Save(path) && MackieSettings::Load(path).cursorCommands == customJog.cursorCommands, "Jog empty and seek assignments persist");
    { std::ofstream file(path, std::ios::trunc); file << "version 1\nencoder 1 0 \"OpenRun\"\nencoder 9 0 \"OpenRun\"\nencoder 3 3 \"OpenRun\"\nencoder 3 -1 \"OpenRun\"\nencoder 3 0 \"arbitrary-shell\"\n"; }
    Check(MackieSettings::Load(path).encoders == MackieEncoderBindings{}, "malformed and fixed-role encoder assignments ignored");
    { std::ofstream file(path, std::ios::trunc); file << "version 1\nbind 15 0 \"OpenTaskManager\"\n"; }
    const auto legacy = MackieSettings::Load(path);
    Check(legacy.encoders == MackieEncoderBindings{} && legacy.bindings.size() == 1, "legacy configuration gains empty encoder bindings without losing buttons");
    { std::ofstream file(path, std::ios::trunc); file << "language \"zh\"\ncloseToTray 1\nlanguage \"invalid\"\ncloseToTray 9\n"; }
    const auto localized=MackieSettings::Load(path);
    Check(localized.language==L"zh" && localized.closeToTray, "invalid preferences cannot overwrite valid choices");
    std::filesystem::remove(path); // Only the explicit per-process test file, never user configuration.
}
void Fuzz()
{
    Harness h; std::mt19937 random(0x4D4355);
    for (int i = 0; i < 50000; ++i)
    {
        h.surface.Input(random(), i + 1000);
        if (i % 100 == 0) h.surface.Feedback(i + 1000);
    }
    for (const auto& a : h.actions)
    {
        if (a.kind == ActionKind::Volume || a.kind == ActionKind::Mute) Check(std::isfinite(a.value) && a.value >= 0 && a.value <= 1, "fuzz bounded scalar");
        if (a.kind == ActionKind::Pan) Check(std::isfinite(a.value) && a.value >= -1 && a.value <= 1, "fuzz bounded pan");
    }
    for (const auto& b : h.output)
    {
        Check(!b.empty(), "fuzz output nonempty");
        if (b[0] == 0xF0) Check(b.back() == 0xF7 && std::all_of(b.begin() + 1, b.end() - 1, [](auto v) { return v < 128; }), "fuzz SysEx framing");
        else Check(ValidShort(Raw(b)), "fuzz legal output MIDI");
    }
}
void WindowsPreset()
{
    MackieSettings settings;
    settings.input = L"Chosen input"; settings.output = L"Chosen output";
    settings.profile = L"mcu"; settings.touch = false;
    settings.trackOrder = {L"stable-app", L"stable-device"};
    settings.bindings[0x36] = L"OpenCalculator";
    settings.bindings[15 * 128 + 90] = L"OpenNotepad";
    Check(settings.ApplyWindows80Preset(), "install generic 80-key preset");
    Check(settings.bindings.size() == 82, "preset preserves other bindings");
    for (int note = 0; note < 80; ++note)
        Check(settings.bindings.at(15 * 128 + note) == Windows80Commands[note] &&
            FindMackieCommand(Windows80Commands[note]), "preset Note maps to allowlisted command");
    Check(settings.input == L"Chosen input" && settings.output == L"Chosen output" &&
        settings.profile == L"mcu" && !settings.touch && settings.trackOrder.size() == 2,
        "preset never changes device routing, profile, touch or track identity");
    const auto installed = settings.bindings;
    Check(settings.ApplyWindows80Preset() && settings.bindings == installed, "preset installation is idempotent");
    Check(settings.bindings.at(15 * 128) == L"OpenTaskManager" &&
        settings.bindings.at(15 * 128 + 55) == L"MonoAudio" &&
        settings.bindings.at(15 * 128 + 56) == L"ClearSolo" &&
        settings.bindings.at(15 * 128 + 79) == L"ClipboardHistory", "preset page boundaries and audio controls");
    MackieSettings conflict;
    conflict.bindings[15 * 128 + 79] = L"OpenCalculator";
    const auto before = conflict.bindings;
    Check(!conflict.ApplyWindows80Preset() && conflict.bindings == before, "late conflict aborts atomically without overwriting user's custom key");
}
void AudioIdentityQueue()
{
    AudioTrackCommandQueue queue;
    Check(!queue.Push(AudioTrackControl::Volume, L"", .5F) && !queue.Push(AudioTrackControl::Pan, L"app", std::numeric_limits<float>::quiet_NaN()), "audio queue rejects invalid identity/value");
    queue.Push(AudioTrackControl::Volume, L"old", .2F);
    queue.Push(AudioTrackControl::SetDefault, L"z-output", 0);
    queue.Push(AudioTrackControl::SetDefault, L"a-output", 0);
    queue.Push(AudioTrackControl::Volume, L"old", .7F);
    const auto commands = queue.Take();
    Check(commands.size() == 3 && commands.back().key == L"old" && Near(commands.back().value, .7F), "queue coalesces rapid fader writes");
    Check(commands[0].key == L"z-output" && commands[1].key == L"a-output", "record selection uses press order");
    std::vector<Track> before{Make(L"old"), Make(L"other")};
    Check(ResolveAudioTrackSlot(before, commands.back().key) == 0, "original slot resolution");
    before[0] = Make(L"replacement");
    Check(ResolveAudioTrackSlot(before, commands.back().key) == -1, "recycled Windows slot cannot receive old command");
    before.push_back(Make(L"old"));
    Check(ResolveAudioTrackSlot(before, commands.back().key) == 2, "queued command follows stable key to current slot");
    Check(queue.Take().empty(), "drain once");
    for (int i = 0; i < 512; ++i) Check(queue.Push(AudioTrackControl::Volume, L"key" + std::to_wstring(i), 2), "bounded queue accepts capacity");
    Check(!queue.Push(AudioTrackControl::Volume, L"overflow", .5F), "bounded queue rejects overflow");
    Check(queue.Push(AudioTrackControl::Volume, L"key1", -.5F), "existing entry can coalesce at capacity");
    const auto full = queue.Take(); Check(full.size() == 512 && full.back().value == 0, "queue clamps volume");
}
void JogModes()
{
    JogControl jog;
    Check(jog.Rotate(2, 1000) == 2 && jog.Rotate(-63, 1100) == -10, "ordinary Jog is bounded seconds");
    Check(jog.Rotate(0, 1000) == 0 && jog.Rotate(64, 1000) == 0, "invalid Jog delta rejected");
    jog.settings.seekSeconds = 3; Check(jog.Rotate(-2, 1000) == -6, "playback sensitivity retained");
    jog.settings.seekSeconds = 0; Check(jog.Rotate(1, 1000) == 0, "invalid seek settings inert");
    CursorControl cursor;
    for (int axis = 0; axis < 4; ++axis)
    {
        cursor.ResetGesture(); cursor.ticks[axis] = 4;
        for (int i = 0; i < 3; ++i) Check(cursor.Rotate(axis, 1, 1000 + i * 10) == 0, "four-axis custom sensitivity");
        Check(cursor.Rotate(axis, 1, 1030) == 1, "fourth event triggers single command");
        for (int i = 1040; i < 1130; i += 10) Check(cursor.Rotate(axis, 1, i) == 0, "cooldown discards backlog");
        cursor.Rotate(axis, 1, 1140); cursor.Rotate(axis, 1, 1150);
        Check(cursor.Rotate(axis, -1, 1160) == 0 && cursor.Rotate(axis, -1, 1170) == 0 &&
            cursor.Rotate(axis, -1, 1180) == 0 && cursor.Rotate(axis, -1, 1190) == -1, "reverse resets partial command");
        cursor.Rotate(axis, 1, 1300); cursor.Rotate(axis, 1, 1310);
        Check(cursor.Rotate(axis, 1, 1800) == 0 && cursor.Rotate(axis, 1, 1810) == 0, "idle discards partial command");
    }
    Check(cursor.Rotate(-1, 1, 2000) == 0 && cursor.Rotate(5, 1, 2000) == 0 &&
        cursor.Rotate(0, 0, 2000) == 0 && cursor.Rotate(0, 63, 2000) == 0, "cursor cannot accept arbitrary axes or acceleration");
    cursor.ticks[0] = 0; Check(cursor.Rotate(0, 1, 2000) == 0, "invalid cursor divisor is safe");
    Harness h;
    for (bool zoom : {false, true})
    {
        h.surface.SetCursorZoom(zoom); h.actions.clear();
        h.surface.Input(Raw(0xB0, 0x3C, 2), 2500);
        Check(h.actions.size() == 1 && h.actions[0].kind == ActionKind::SeekSeconds && h.actions[0].value == 2,
            "native Zoom layer cannot capture ordinary Jog");
        h.actions.clear(); h.Press(0x65);
        Check(h.actions.empty(), "native scrub/push has no invented Focus/Navi action");
    }
    h.surface.ResetConnection(); h.actions.clear(); h.surface.JogRotate(-1, 3000);
    Check(h.actions.size() == 1 && h.actions[0].kind == ActionKind::SeekSeconds, "reconnect retains playback semantics");
    Check(!FindMackieCommand(L"JogFocus") && !FindMackieCommand(L"JogNavigate") && !FindMackieCommand(L"JogPush"),
        "Navi and Focus cannot be customized by old mode commands");
    h.surface.JogSeekDirections = {0, 1}; h.surface.CursorGestures.ticks[4] = 2;
    h.actions.clear(); h.surface.JogRotate(-63, 4000);
    Check(h.actions.empty(), "custom Jog ignores accelerated magnitude and uses configured ticks");
    h.surface.JogRotate(-63, 4010);
    Check(h.actions.size() == 1 && h.actions[0].kind == ActionKind::CursorCommand &&
        h.actions[0].encoder == 4 && h.actions[0].value == -1, "left Jog dispatches its own custom command");
    h.surface.JogRotate(-63, 4020); Check(h.actions.size() == 1, "custom Jog has no burst backlog");
    h.surface.JogRotate(2, 4030);
    Check(h.actions.back().kind == ActionKind::SeekSeconds && h.actions.back().value == 2, "right Jog can retain playback while left is custom");
    h.surface.JogSeekDirections = {1, -1}; h.surface.JogRotate(-2, 4200);
    Check(h.actions.back().kind == ActionKind::SeekSeconds && h.actions.back().value == 2, "seek command direction follows assigned command");
    h.surface.JogSeekDirections = {0, 0}; h.surface.CursorGestures.ticks[4] = 1;
    h.surface.ResetConnection(); h.actions.clear(); h.surface.SetCursorZoom(true); h.surface.JogRotate(63, 4500);
    Check(h.actions.size() == 1 && h.actions[0].kind == ActionKind::CursorCommand && h.actions[0].encoder == 4 &&
        h.actions[0].value == 1, "right custom Jog survives reconnect and stays independent of native Zoom");
    Check(ValidDirectionCommand(4, L"Jog.SeekBack") && ValidDirectionCommand(4, L"Jog.SeekForward") &&
        ValidDirectionCommand(4, L"Cursor.ScrollDown") && !ValidDirectionCommand(0, L"Jog.SeekBack") &&
        !ValidDirectionCommand(4, L"JogFocus") && !ValidDirectionCommand(5, L"OpenRun"), "Jog command allowlist remains bounded without native mode overrides");
}
void JogWindowsPackets()
{
    for (int kind = 0; kind < 8; ++kind)
    {
        const auto inputs = CursorInputs(static_cast<CursorInput>(kind));
        if (kind < 4)
        {
            Check(inputs.size() == 1 && inputs[0].type == INPUT_MOUSE, "wheel command sends one mouse packet");
            Check(inputs[0].mi.dwFlags == static_cast<DWORD>(kind < 2 ? MOUSEEVENTF_WHEEL : MOUSEEVENTF_HWHEEL) &&
                static_cast<LONG>(inputs[0].mi.mouseData) == (kind == 0 || kind == 3 ? 120 : -120), "wheel axis and sign correct");
        }
        else
        {
            constexpr WORD keys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT};
            Check(inputs.size() == 2 && inputs[0].type == INPUT_KEYBOARD && inputs[0].ki.wVk == keys[kind - 4], "direction key packet correct");
            Check(inputs[0].ki.dwFlags == KEYEVENTF_EXTENDEDKEY && inputs[1].ki.wVk == inputs[0].ki.wVk &&
                inputs[1].ki.dwFlags == (KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP), "direction key balanced down/up");
        }
    }
    Check(CursorInputs(static_cast<CursorInput>(-1)).empty() && CursorInputs(static_cast<CursorInput>(8)).empty(), "invalid cursor packet kind inert");
    for (const auto& c : CursorCommands) Check(ValidCursorCommand(c.id) && FindCursorCommand(c.id) == &c, "cursor allowlist resolves");
    Check(ValidCursorCommand(L"") && ValidCursorCommand(L"ZoomIn") && ValidCursorCommand(L"NextVirtualDesktop"), "custom gestures can use Windows commands");
    Check(!ValidCursorCommand(L"JogFocus") && !ValidCursorCommand(L"MonoAudio") &&
        !ValidCursorCommand(L"arbitrary-shell") && !ValidCursorCommand(L"Channel.Focus"), "unsupported commands cannot recurse or change native Focus");
}
void NativeJogMessages()
{
    Harness h; std::vector<Track> tracks;
    for (int i = 0; i < 12; ++i) tracks.push_back(Make((L"native" + std::to_wstring(i)).c_str()));
    h.surface.Update(tracks, 1000);
    // Absolute SELECT announcements may omit release. Do not turn this stream
    // into a series of foreground activations, or drop previously seen IDs.
    for (int note : {0x18, 0x19, 0x1A, 0x19, 0x18, 0x19}) h.surface.Input(Raw(0x90, note, 127), 1100);
    Check(h.surface.Selected()->key == L"native1" && h.actions.empty(), "native Navi reselects without release and never steals focus");
    h.Press(0x2F); h.surface.Input(Raw(0x90, 0x18, 127), 1200);
    Check(h.surface.Selected()->key == L"native8" && h.actions.empty(), "native Bank then Select resolve against new bank");
    h.surface.Input(Raw(0x90, 0x19, 0), 1201);
    Check(h.actions.empty(), "old select release after banking cannot focus a replacement");
    h.Press(0x18);
    Check(h.actions.size() == 1 && h.actions.back().key == L"native8" && h.actions.back().kind == ActionKind::Focus, "real Select release requests one foreground action");
    h.actions.clear(); h.surface.Input(Raw(0x90, 0x18, 0), 1202);
    Check(h.actions.empty(), "duplicate Select release inert");
    h.Touch(0); h.surface.Input(Raw(0x90, 0x19, 127), 1203); h.surface.Input(Raw(0x90, 0x19, 0), 1204);
    Check(h.surface.Selected()->key == L"native8" && h.actions.empty(), "native navigation cannot retarget touched strip"); h.Touch(0, false);
    h.Press(0x61, 0, 1300);
    Check(h.actions.size() == 1 && h.actions.back().kind == ActionKind::CursorCommand && h.actions.back().encoder == 0 && h.actions.back().value == 1, "native down maps Move vertical positive");
    h.Press(0x62, 0, 1400);
    Check(h.actions.back().encoder == 1 && h.actions.back().value == -1, "native left maps Move horizontal negative");
    h.Press(0x64, 0, 1500); h.Press(0x60, 0, 1510);
    Check(h.surface.CursorZoom() && h.actions.back().encoder == 2 && h.actions.back().value == -1, "native Zoom up maps vertical negative independently");
    h.output.clear(); h.surface.Feedback(1560, true);
    Check(h.Has(Led(0x64, true)) && !h.Has(Led(0x60, true)) && !h.Has(Led(0x60, false)), "only documented Zoom LED has feedback");
    h.Press(0x62, 0, 1700);
    Check(h.actions.back().encoder == 3 && h.actions.back().value == -1, "native Zoom left maps horizontal negative independently");
    h.Press(0x64, 0, 1800); h.Press(0x60, 0, 1810);
    Check(!h.surface.CursorZoom() && h.actions.back().encoder == 0 && h.actions.back().value == -1, "native Zoom off restores Move map");
    h.Press(0x63, 0, 1900);
    Check(h.actions.back().encoder == 1 && h.actions.back().value == 1, "native Move right maps positive");
    h.Press(0x64, 0, 2000); h.surface.ResetConnection();
    Check(!h.surface.CursorZoom(), "reconnect resets native Zoom switch state");
    h.surface.Input(Raw(0x90, 0x64, 127), 2100); h.surface.ResetJogInput();
    h.surface.Input(Raw(0x90, 0x64, 127), 2101);
    Check(h.surface.CursorZoom(), "editor gesture reset preserves held Zoom switch debounce");
    // Replay all eight directions, release duplicate, bank/touch identity and
    // ordinary CC Jog independence without any Windows input or MIDI ports.
    std::uint64_t time = 3000;
    for (int axis = 0; axis < 4; ++axis) for (int direction = 0; direction < 2; ++direction)
    {
        h.surface.SetCursorZoom(axis >= 2); h.actions.clear();
        const int note = 0x60 + (axis % 2) * 2 + direction;
        h.Press(note, 0, time); time += 200;
        Check(h.actions.size() == 1 && h.actions[0].encoder == axis && h.actions[0].value == (direction ? 1 : -1),
            "every physical direction has a distinct custom slot");
        h.surface.Input(Raw(0x80, note), time);
        Check(h.actions.size() == 1, "release never executes a custom command");
    }
}
void JogMediaSeconds()
{
    constexpr std::int64_t second = 10000000;
    WindowsMediaState s;
    Check(!MackieSeekSeconds(s, 1), "unavailable media cannot seek");
    s.available = s.canSeek = s.hasPosition = true;
    s.timeline.start = 0; s.timeline.end = 1000 * second;
    s.timeline.seekStart = 100 * second; s.timeline.seekEnd = 200 * second; s.timeline.position = 150 * second;
    Check(Near(*MackieSeekSeconds(s, 2), .52F), "seconds seek uses seek window, not full media duration");
    Check(*MackieSeekSeconds(s, -500) == 0 && *MackieSeekSeconds(s, 500) == 1, "seconds seeks clamp both endpoints");
    s.playing = true; s.timeline.rate = 1; s.timeline.updatedUtc = 1000 * second; s.timeline.sampledUtc = 1003 * second;
    Check(Near(*MackieSeekSeconds(s, 2), .55F), "seek projects running playback timestamp");
    s.canSeek = false; Check(!MackieSeekSeconds(s, 1), "read-only position never sends seek");
    s.canSeek = true;
    Check(!MackieSeekSeconds(s, std::numeric_limits<double>::infinity()), "non-finite seek rejected");
    s.timeline.seekEnd = s.timeline.seekStart; Check(!MackieSeekSeconds(s, 1), "zero seek interval rejected");
}
}
int main()
{
    try
    {
        MultipleDevices(); Codec(); PlaybackClock(); TimeDisplay(); IdleClock(); Interaction(); Buttons(); CurrentEncoders(); Identity(); Feedback(); Settings(); Fuzz(); AudioIdentityQueue(); WindowsPreset(); JogModes(); JogWindowsPackets(); JogMediaSeconds(); NativeJogMessages();
        std::cout << "PASS: 18 test groups, " << checks << " assertions; no MIDI ports opened, no audio changed, no global input injected.\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " assertions: " << e.what() << '\n'; return 1; }
}
