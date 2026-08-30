#include "MackieSurface.h"
#include "MackieSettings.h"
#include "CommandCatalog.h"
#include "AudioTrackCommandQueue.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <random>
#include <set>

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
    h.Press(0x32); Check(!h.surface.Flipped(), "flip locked while touching");
    h.Touch(0, false); h.output.clear(); h.surface.Feedback(1002);
    Check(h.Has(Fader(0, .8F)), "release reconciles pending position");
    h.output.clear(); h.surface.Feedback(1300);
    Check(h.Has(Fader(0, .5F)), "failed write expires to Windows truth");
    h.surface.RequireTouch = false; h.surface.Input(Raw(Fader(0, .6F)), 1400);
    Check(Near(h.actions.back().value, .6F), "explicit touchless mode");
    h.surface.ResetConnection(); Check(!h.surface.Touched(), "reconnect clears interaction");

    Harness e;
    e.surface.Input(Raw(0xB0, 0x10, 1), 1000); e.surface.Input(Raw(0xB0, 0x10, 1), 1001);
    Check(e.actions.size() == 2 && Near(e.actions.back().value, .04F), "rapid encoders accumulate before audio acknowledgement");
    e.surface.Input(Raw(0xB0, 0x10, 0x41), 1002);
    Check(Near(e.actions.back().value, .02F), "encoder reversal preserves signed delta");
    e.surface.Input(Raw(0xB0, 0x10, 0x40), 1003); Check(e.actions.size() == 3, "zero delta ignored");
    e.Press(0x20); Check(e.actions.back().kind == ActionKind::Pan && e.actions.back().value == 0, "pan push centers");
    e.Press(0x28); e.surface.Input(Raw(0xB0, 0x10, 2), 1100);
    Check(e.actions.back().kind == ActionKind::Volume && Near(e.actions.back().value, .52F), "track assignment selects volume knobs");
    e.Press(0x2A); e.Press(0x32); e.Touch(0); e.surface.Input(Raw(Fader(0, 1)), 1100);
    Check(e.surface.Flipped() && e.actions.back().kind == ActionKind::Pan && e.actions.back().value == 1, "flip fader pans");
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
    h.surface.Input(Raw(0xB0, 0x3C, 0x42), 1100); Check(h.actions.back().kind == ActionKind::Seek && Near(h.actions.back().value, -.02F), "jog seeks relative");
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
    h.surface.Update(tracks, 1200); Check(!h.surface.At(0) && h.surface.At(1)->key == L"key10", "disappearance leaves tombstone, not wrong app");
    h.Touch(0); h.surface.Input(Raw(Fader(0, 1)), 1250); Check(h.actions.empty(), "empty position cannot change audio"); h.Touch(0, false);
    tracks.push_back(Make(L"new")); tracks.push_back(Make(L"key9")); h.surface.Update(tracks, 1300);
    Check(h.surface.At(0)->key == L"key9" && h.surface.Order().back() == L"new", "returning app recovers position");
    h.surface.Select(L"key24"); Check(h.surface.BankStart() == 24 && h.surface.At(0)->key == L"key24", "selection reveals correct bank");
    h.surface.Bank(100); Check(h.surface.BankStart() == 24 && !h.surface.At(7), "last page bounded");
    h.surface.Bank(-100); Check(h.surface.BankStart() == 0, "first page bounded");
    h.surface.RestoreOrder({L"x", L"x", L"", L"key0"}); Check(h.surface.Order().size() == 2, "persistent identities deduplicated");

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
        Check(MackieSettings::Bindable(channel, note) == (channel != 0 || (note >= 0x36 && note < 0x68)), "binding cannot override core MCU controls");
    Check(IconDawPort(L"iCON P1-Nano") == 1 && IconDawPort(L"MIDIIN4 (iCON P1-Nano)") == 4 &&
        IconDawPort(L"MIDIOUT3 (iCON P1-Nano)") == 3 && IconDawPort(L"Mackie Control") == 0, "profile port roles isolated from codec");
    std::set<std::wstring> ids;
    for (const auto& c : MackieCommands) Check(ids.insert(c.id).second && FindMackieCommand(c.id) == &c, "command IDs unique and resolvable");
    Check(ids.size() == 186 && !FindMackieCommand(L"arbitrary shell command"), "catalog is allowlisted");
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    const auto path = std::filesystem::path(temp) / (L"wfb-mackie-settings-test-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
    MackieSettings original; original.input = L"输入 \"MCU\""; original.output = L"Output"; original.profile = L"p1-nano";
    original.trackOrder = {L"应用:Apple", L"ENDPOINT:one"}; original.bindings[15 * 128 + 1] = L"MonoAudio"; original.bindings[0] = L"OpenRun";
    Check(original.Save(path), "atomic configuration save");
    const auto restored = MackieSettings::Load(path);
    Check(restored.input == original.input && restored.trackOrder == original.trackOrder && restored.profile == original.profile, "UTF8 settings roundtrip");
    Check(restored.bindings.size() == 1 && restored.bindings.at(15 * 128 + 1) == L"MonoAudio", "unsafe binding ignored when loading");
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
}
int main()
{
    try
    {
        Codec(); Interaction(); Buttons(); Identity(); Feedback(); Settings(); Fuzz(); AudioIdentityQueue();
        std::cout << "PASS: 8 test groups, " << checks << " assertions; no MIDI ports opened, no audio changed.\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " assertions: " << e.what() << '\n'; return 1; }
}
