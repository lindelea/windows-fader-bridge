#include "ApolloEucon.h"
#include "FeedbackUpdatePolicy.h"
#include "ChannelLayout.h"
#include "ConfigLayout.h"
#include "ConfigWriter.h"
#include "ChannelWriter.h"
#include "EuBatchedMeterWriter.h"
#include "EuCon.h"
#include "EuConManager.h"
#include "EuControlFader.h"
#include "EuControlKnob.h"
#include "EuControlKnobCellArray.h"
#include "EuControlMultiMeter.h"
#include "EuControlSwitch.h"
#include "EuControlSwitchArray.h"
#include "EuControlTextDisplay.h"
#include "EuLayoutMonitor.h"
#include "EuLayoutPan.h"
#include "EuNode.h"
#include "EuPrimitiveKnob.h"
#include "EuPrimitiveSwitch.h"
#include "EuProcessor.h"
#include "FaderScale.h"
#include "MonitorLayout.h"
#include "MonitorWriter.h"
#include "UpperDirectoryLayout.h"
#include "../BridgeGlobalShortcut.h"
// Invented model data used only by the explicit, unregistered SDK regression mode.
#include "../../tests/ApolloBridge.Tests/ChannelFeatureFixture.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <shlobj.h>
#include <stdexcept>

namespace apollo
{
std::wstring Wide(const std::string &text)
{
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (!count)
        return L"Invalid text";
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), count);
    return result;
}
void Log(const std::string &message)
{
    // Called only on the EUCON owner/UI thread, never in callbacks.
    static std::ofstream output = [] {
        PWSTR folder = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
            return std::ofstream{};
        const auto path = std::filesystem::path(folder) / L"UAD Console Bridge" / L"EUCON" / L"logs";
        CoTaskMemFree(folder);
        std::error_code error;
        std::filesystem::create_directories(path, error);
        return std::ofstream(path / (L"session-" + std::to_wstring(GetCurrentProcessId()) + L".log"));
    }();
    static size_t lines = 0;
    if (output && lines++ < 20000)
        output << GetTickCount64() << " thread=" << GetCurrentThreadId() << ' ' << message << std::endl;
}
namespace
{
void Check(tERR result, const char *operation)
{
    if (result != kERR_OK)
    {
        const auto message = std::string(operation) + " failed: " + std::to_string(result);
        Log(message);
        throw std::runtime_error(message);
    }
}
void CleanupResult(tERR result, const char *operation) noexcept
{
    try
    {
        if (result != kERR_OK)
            Log(std::string(operation) + " cleanup error=" + std::to_string(result));
    }
    catch (...)
    {
    }
}
EuPrimitiveControl &Primitive(EuControl &control, NEuCon::uint32 id)
{
    EuPrimitiveControl *primitive = nullptr;
    Check(control.GetPrimitive(id, &primitive), "GetPrimitive");
    if (!primitive)
        throw std::runtime_error("Missing EUCON primitive");
    return *primitive;
}
void Initialize(EuPrimitiveControl &p, tTYP type, NEuCon::uint16 size)
{
    Check(p.SetAttribute2(kATRIBID_OnlySendIfDifferent, 1), "OnlySendIfDifferent");
    Check(p.Initialize(type, size), "Primitive Initialize");
}
void Switch(EuPrimitiveControl &p)
{
    Initialize(p, kTYP_Int, 2);
    Check(p.LoadValueTableInterpolated(0, 1), "Switch table");
    Check(p.LoadValueAt(0, L"OFF", L"OFF", L"Off"), "Switch off text");
    Check(p.LoadValueAt(1, L"ON", L"ON", L"On"), "Switch on text");
    auto *button = dynamic_cast<EuPrimitiveSwitch *>(&p);
    if (!button)
        throw std::runtime_error("Unexpected switch primitive type");
    Check(button->SetSwitchMode(kSWITCH_MultiState), "Switch mode");
}
void LatchSwitch(EuPrimitiveControl &p)
{
    Initialize(p, kTYP_Int, 2);
    Check(p.LoadValueTableInterpolated(0, 1), "Latch switch table");
    Check(p.LoadValueAt(0, L"OFF", L"OFF", L"Off"), "Latch switch off text");
    Check(p.LoadValueAt(1, L"ON", L"ON", L"On"), "Latch switch on text");
    auto *button = dynamic_cast<EuPrimitiveSwitch *>(&p);
    if (!button)
        throw std::runtime_error("Unexpected latch switch primitive type");
    Check(button->SetSwitchMode(kSWITCH_MomentaryLatch), "Latch switch mode");
}
void OneShotSwitch(EuPrimitiveControl &p)
{
    // Avid one-shot switches have one value and no persistent state. The
    // activation callback itself is the action, regardless of whether the
    // surface chooses the physical press or release edge.
    Initialize(p, kTYP_Int, 1);
    auto *button = dynamic_cast<EuPrimitiveSwitch *>(&p);
    if (!button)
        throw std::runtime_error("Unexpected one-shot switch primitive type");
    Check(button->SetSwitchMode(kSWITCH_OneShot), "One-shot switch mode");
}
struct ValueText
{
    std::wstring short4, short8, full;
};
ValueText DbValueText(float value, float minimum)
{
    if (minimum <= -120.0F && value <= minimum + 0.001F)
        return {L"-INF", L"-INF", L"-Infinity dB"};
    if (std::abs(value) < 0.05F)
        value = 0.0F;
    wchar_t short4[16]{}, short8[16]{}, full[32]{};
    swprintf_s(short4, L"%+.0fdB", value);
    swprintf_s(short8, L"%+.1fdB", value);
    swprintf_s(full, L"%+.1f dB", value);
    if (wcslen(short4) > 4)
        swprintf_s(short4, L"%+.0f", value);
    if (value == 0.0F)
    {
        wcscpy_s(short4, L"0dB");
        wcscpy_s(short8, L"0.0dB");
        wcscpy_s(full, L"0.0 dB");
    }
    return {short4, short8, full};
}
void LoadValueText(EuPrimitiveControl &primitive, NEuCon::uint16 index, const ValueText &text,
                   const char *operation)
{
    Check(primitive.LoadValueAt(index, text.short4, text.short8, text.full), operation);
}
void LoadDbValueText(EuPrimitiveControl &primitive, const std::vector<float> &values, float minimum,
                     const char *operation)
{
    for (size_t i = 0; i < values.size(); ++i)
        LoadValueText(primitive, static_cast<NEuCon::uint16>(i), DbValueText(values[i], minimum), operation);
}
void CheckTextResult(EuPrimitiveControl &p, tERR result, NEuCon::uint16 index, const std::wstring &text,
                     const char *operation)
{
    if (result != kERR_AlreadySet)
    {
        Check(result, operation);
        return;
    }
    // A blank indexed-string entry can already equal its requested initial
    // value. Accept this status only for text, with all display widths verified.
    NEuCon::uint32 primitiveId = 0;
    const auto idResult = p.GetId(primitiveId);
    NEuCon::uint16 tableSize = 0;
    Check(p.GetValueTableSize(tableSize), "Text no-op table size");
    if (index >= tableSize)
        throw std::runtime_error(std::string(operation) + " AlreadySet index outside text table");
    for (const auto width : {kSTRLEN_4, kSTRLEN_8, kSTRLEN_Long})
    {
        tEuString actual;
        const auto readResult = p.GetValueAt(index, actual, width);
        const auto expected = text.substr(0, width == kSTRLEN_4 ? 4 : width == kSTRLEN_8 ? 8 : text.size());
        const bool matches = readResult == kERR_OK && actual == expected;
        Log(std::string(operation) + " result=" + std::to_string(result) +
            " primitive=" + std::to_string(primitiveId) + " id-result=" + std::to_string(idResult) +
            " index=" + std::to_string(index) + " width=" + std::to_string(width) +
            " requested-length=" + std::to_string(text.size()) +
            " readback-result=" + std::to_string(readResult) + " matched=" + std::to_string(matches));
        Check(readResult, "Text no-op readback");
        if (!matches)
            throw std::runtime_error(std::string(operation) + " AlreadySet text readback mismatch");
    }
}
void UpdateText(EuPrimitiveControl &p, const std::wstring &text, const char *operation,
                NEuCon::uint16 index = 0)
{
    CheckTextResult(p, p.ChangeText(text, index), index, text, operation);
}
void Label(EuPrimitiveControl &p, const std::wstring &text)
{
    Initialize(p, kTYP_IndexedString, 1);
    CheckTextResult(p, p.LoadValueAt(0, text), 0, text, "Label table");
    Check(p.SetCurrentIndex(0), "Label index");
}
void RawSwitch(EuPrimitiveControl &p)
{
    Switch(p);
    Check(static_cast<EuPrimitiveSwitch &>(p).SetSwitchMode(kSWITCH_Raw), "Raw press/release mode");
}
void MarkConfigPage(EuControlKnobCellArray &array, NEuCon::uint32 member)
{
    Check(array.NewConfigPage(member), "Config page marker");
    std::vector<NEuCon::uint32> actual;
    Check(array.GetConfigPages(actual), "Config page readback");
    if (std::count(actual.begin(), actual.end(), member) != 1)
        throw std::runtime_error("Config page member readback mismatch");
    NEuCon::uint32 id = 0;
    Check(array.GetId(id), "Config array ID");
    Log("config-page control=" + std::to_string(id) + " member=" + std::to_string(member) +
        " readback=1");
}
void ToggleFeedback(EuControl &control, NEuCon::uint32 button, NEuCon::uint32 led, const Parameter &p)
{
    Check(Primitive(control, button).SetCurrentIndex(p.value.Bool() ? 1 : 0), "Switch feedback");
    Check(Primitive(control, led)
              .SetCurrentIndex(static_cast<NEuCon::uint16>(p.value.Bool() ? kLEDStatus_On : kLEDStatus_Off)),
          "LED feedback");
}
struct Event
{
    enum class Kind
    {
        Visibility,
        Surface,
        Primitive,
        MonitorPrimitive,
        ApplicationCommand
    } kind = Kind::Primitive;
    int tag = 0, type = 0;
    NEuCon::uint32 control = 0, member = 0, primitive = 0, flags = 0;
    NEuCon::uint16 index = 0;
    bool visible = false;
    tVisibilityHandle handle = kEuInvalidVisibilityHandle;
    tEuMeterFormat format = kEuInvalidMeterFormat;
    DWORD thread = 0;
    uint64_t epoch = 0, configEpoch = 0, configModel = 0;
    double value = 0;
    bool decoded = false;
    tERR decodeResult = kERR_OK;
    std::chrono::steady_clock::time_point at{};
};
struct Inbox
{
    std::mutex mutex;
    std::deque<Event> events;
    std::atomic<bool> overflow = false;
    std::atomic<uint64_t> epoch = 0;
    std::function<void()> wake;
    bool wakePending = false;
    void Push(Event event) noexcept
    {
        try
        {
            bool notify = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (events.size() < 8192)
                {
                    events.push_back(event);
                    if (!wakePending)
                        notify = wakePending = true;
                }
                else
                    overflow = true;
            }
            if (notify && wake)
                wake();
        }
        catch (...)
        {
            overflow = true;
        }
    }
    std::deque<Event> Take()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::deque<Event> result;
        result.swap(events);
        wakePending = false;
        return result;
    }
};

class ApplicationCommands final : public EuProcessor
{
  public:
    explicit ApplicationCommands(Inbox &inbox)
        : inbox_(inbox), applications_(this), windows_(this), uad_(this), mackie_(this)
    {
        Check(SetAttribute(kATRIBID_ProcessorType, kProcType_Command), "Command processor type");
        Check(SetAttribute(kATRIBID_ContainsSoftKeys, 1), "Command processor soft keys");
        Check(SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(L"Key Commands")),
              "Command processor name");
        Check(SetAttribute(kATRIBID_DoNotSort, 1), "Command processor order");
        Check(SetPersistenceID(L"Lindelea.UadConsoleBridge.Commands.v1"),
              "Command processor persistence");
        Check(applications_.SetId(1), "Application command container ID");
        Check(applications_.SetAttribute(kATRIBID_SimpleUserVisibleName,
                                         tEuString(L"EUCON Applications")),
              "Application command container name");
        Check(applications_.SetAttribute(kATRIBID_DoNotSort, 1),
              "Application command container order");
        Check(applications_.SetPersistenceID(L"Lindelea.UadConsoleBridge.Commands.Applications.v1"),
              "Application command container persistence");
        Check(AddControl(applications_), "Add application command container");
        Add(windows_, windowsId_, L"Windows EUCON", L"WindowsEucon", false);
        Add(uad_, uadId_, L"UAD EUCON", L"UadEucon", true);
        Add(mackie_, mackieId_, L"Mackie Control", L"MackieControl", false);
    }
    ~ApplicationCommands() override
    {
        applications_.Remove(mackieId_);
        applications_.Remove(uadId_);
        applications_.Remove(windowsId_);
        RemoveControl(applications_);
    }
    void OnPrimitiveCallback(tEVT eventType, NEuCon::uint32, NEuCon::uint32 controlId,
                             NEuCon::uint32 memberId, NEuCon::uint32, EuPrimitiveControl *,
                             NEuCon::uint16, void * = nullptr) override
    {
        if (eventType != kEVT_PRIM_StateChange || controlId != 1) return;
        Event event;
        event.kind = Event::Kind::ApplicationCommand;
        event.thread = GetCurrentThreadId();
        if (memberId == windowsId_) event.type = static_cast<int>(bridge::Application::WindowsEucon);
        else if (memberId == uadId_) event.type = static_cast<int>(bridge::Application::UadEucon);
        else if (memberId == mackieId_) event.type = static_cast<int>(bridge::Application::MackieControl);
        else return;
        inbox_.Push(event);
    }

  private:
    void Add(EuControlSwitch &control, NEuCon::uint32 &member, const wchar_t *name,
             const wchar_t *token, bool selfApplication)
    {
        Check(control.SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(name)),
              "Application command name");
        Check(control.SetPersistenceID(std::wstring(L"Lindelea.UadConsoleBridge.Commands.") + token +
                                       L".v1"),
              "Application command persistence");
        OneShotSwitch(Primitive(control, EuControlSwitch::kID_Switch));
        Check(control.SetLedOverride(selfApplication), "Application command LED");
        if (selfApplication)
        {
            auto &led = Primitive(control, EuControlSwitch::kID_Led);
            Check(led.SetCurrentIndex(kLEDStatus_On), "Current application LED state");
            Check(led.Refresh(), "Current application LED refresh");
        }
        Check(applications_.PushBack(&control, member), "Add application command");
    }
    Inbox &inbox_;
    EuControlSwitchArray applications_;
    EuControlSwitch windows_, uad_, mackie_;
    NEuCon::uint32 windowsId_ = 0, uadId_ = 0, mackieId_ = 0;
};
class Node final : public EuNode
{
  public:
    explicit Node(Inbox &inbox) : inbox_(inbox)
    {
    }
    void OnCallback(tEVT type, void *hidden, void *shown, void *, int &status) override
    {
        status = kERR_OK;
        if (type == kEVT_NODE_VisibilityChangedV2)
        {
            QueueVisibility<NEuCon::tVisChangeDataVectorV2>(hidden, false);
            QueueVisibility<NEuCon::tVisChangeDataVectorV2>(shown, true);
        }
        else if (type == kEVT_NODE_VisibilityChanged)
        {
            QueueVisibility<NEuCon::tVisChangeDataVector>(hidden, false);
            QueueVisibility<NEuCon::tVisChangeDataVector>(shown, true);
        }
        else if (type == kEVT_NODE_SurfaceNodeAdded || type == kEVT_NODE_SurfaceNodeRemoved ||
                 type == kEVT_NODE_NetworkError)
        {
            Event event;
            event.kind = Event::Kind::Surface;
            event.type = type;
            event.thread = GetCurrentThreadId();
            inbox_.Push(event);
        }
    }

  private:
    template <class V> void QueueVisibility(void *data, bool visible)
    {
        if (!data)
            return;
        for (const auto &item : *static_cast<V *>(data))
        {
            if (item.mVisChangeObjType != NEuCon::kVisChangeObjType_EuControlMultiMeter)
                continue;
            Event event;
            event.kind = Event::Kind::Visibility;
            event.tag = item.mUserData;
            event.visible = visible;
            event.handle = item.mVisibilityHandle;
            event.format = item.mMeterFormat;
            event.thread = GetCurrentThreadId();
            inbox_.Push(event);
        }
    }
    Inbox &inbox_;
};
// A control room is never a channel strip. The runtime owns its assignment.
class ControlRoom final : public EuProcessor
{
  public:
    ControlRoom(const Monitor &m, int tag, Inbox &inbox, MonitorController &controller)
        : target_(m), tag_(tag), inbox_(inbox), controller_(controller), level_(this), mute_(this),
          dim_(this), formats_(this), mono_(this), dimAmount_(this), talk_(this), sources_(this)
    {
        controls_.reserve(4);
        try
        {
            Check(SetPersistenceID(Wide(m.key + ".control-room")), "Monitor persistence");
            Check(SetAttribute2(kATRIBID_ProcessorType, kProcType_Monitor), "Monitor processor type");
            Check(SetAttribute2(kATRIBID_LayoutRule0, kRUL_EuLayoutMonitor), "Monitor layout");
            Check(SetAttribute2(kATRIBID_SimpleUserVisibleName, L"Control Room"), "Monitor name");
            Check(SetAttribute2(kATRIBID_ContainsSoftKeys, 1), "Monitor soft keys");
            Add(level_, Level, EuLayoutMonitor::kNAM_ControlRoom, L"Main");
            auto &knob = Primitive(level_, EuControlKnob::kID_Knob);
            const auto low = static_cast<float>(*m.level->minimum),
                       high = static_cast<float>(*m.level->maximum);
            const auto steps = static_cast<size_t>(std::ceil((high - low) * 10));
            for (size_t i = 0; i <= steps; ++i)
                table_.push_back(low + (high - low) * static_cast<float>(i) / static_cast<float>(steps));
            Initialize(knob, kTYP_Float, static_cast<NEuCon::uint16>(table_.size()));
            Check(knob.LoadValueTable(table_, 1), "Monitor attenuation table");
            LoadDbValueText(knob, table_, low, "Monitor attenuation text");
            knob.MakeConfirmationCallback(true);
            auto *levelRing = dynamic_cast<EuPrimitiveKnob *>(&knob);
            if (!levelRing)
                throw std::runtime_error("Missing monitor level knob");
            Check(levelRing->SetPositionRingMode(kRingThermometerLeft), "Monitor level ring");
            RawSwitch(Primitive(level_, EuControlKnob::kID_KnobTouchSense));
            Label(Primitive(level_, EuControlKnob::kID_KnobLabelDisplay), L"Mix");
            hasMute_ = WritableButton(m, MonitorField::Mute);
            hasDim_ = WritableButton(m, MonitorField::Dim);
            hasMono_ = WritableButton(m, MonitorField::Mono);
            if (hasMute_)
            {
                Add(mute_, Mute, EuLayoutMonitor::kNAM_Mute, L"Mute");
                InitButton(mute_);
            }
            if (hasDim_)
            {
                Add(dim_, Dim, EuLayoutMonitor::kNAM_Dim, L"Dim");
                InitButton(dim_);
            }
            if (hasMono_)
            {
                Add(formats_, Formats, EuLayoutMonitor::kNAM_FolddownFormat, L"Folddown Format");
                Check(formats_.SetAttribute2(kATRIBID_ContainsSoftKeys, 1), "Fold-down soft keys");
                Check(mono_.SetPersistenceID(L"Mono"), "Mono persistence");
                Check(mono_.SetAttribute2(kATRIBID_SimpleUserVisibleName, L"Mono"), "Mono label");
                InitButton(mono_);
                Check(formats_.PushBack(&mono_, monoMember_), "Add mono format");
                monoAdded_ = true;
            }
            hasDimAmount_ = MonitorFieldAvailable(m, MonitorField::DimAmount);
            if (hasDimAmount_)
            {
                Add(dimAmount_, DimAmount, EuLayoutMonitor::kNAM_DimLevel, L"Dim Amount");
                auto &depth = Primitive(dimAmount_, EuControlKnob::kID_Knob);
                Initialize(depth, kTYP_Float, static_cast<NEuCon::uint16>(MonitorDimTable().size()));
                Check(depth.LoadValueTable(MonitorDimTable(), 0), "Dim depth table");
                LoadDbValueText(depth, MonitorDimTable(), MonitorDimTable().front(), "Dim depth text");
                depth.MakeConfirmationCallback(true);
                auto *dimRing = dynamic_cast<EuPrimitiveKnob *>(&depth);
                if (!dimRing)
                    throw std::runtime_error("Missing monitor DIM knob");
                Check(dimRing->SetPositionRingMode(kRingThermometerLeft), "Monitor DIM ring");
                RawSwitch(Primitive(dimAmount_, EuControlKnob::kID_KnobTouchSense));
                Label(Primitive(dimAmount_, EuControlKnob::kID_KnobLabelDisplay), L"Dim");
            }
            hasTalk_ = MonitorFieldAvailable(m, MonitorField::Talk);
            if (hasTalk_)
            {
                Add(talk_, Talk, EuLayoutMonitor::kNAM_Talk, L"Talkback");
                InitButton(talk_);
                Check(talk_.SetSwitchMode(kSWITCH_MomentaryLatch), "Talkback momentary/latch");
            }
            if (MonitorFieldAvailable(m, MonitorField::Source))
            {
                sourceValues_ = MonitorSources(m);
                Add(sources_, Sources, EuLayoutMonitor::kNAM_ControlRoomSource, L"Control Room Sources");
                Check(sources_.SetAttribute2(kATRIBID_ContainsSoftKeys, 1), "Monitor source soft keys");
                sourceButtons_.reserve(sourceValues_.size());
                sourceMembers_.reserve(sourceValues_.size());
                for (const auto &value : sourceValues_)
                {
                    auto button = std::make_unique<EuControlSwitch>(this);
                    Check(button->SetPersistenceID(Wide(value)), "Source persistence");
                    Check(button->SetAttribute2(kATRIBID_SimpleUserVisibleName, SourceLabel(value)),
                          "Source name");
                    InitButton(*button);
                    NEuCon::uint32 member = 0;
                    Check(sources_.PushBack(button.get(), member), "Add monitor source");
                    sourceMembers_.push_back(member);
                    sourceButtons_.push_back(std::move(button));
                }
            }
            Apply(m);
        }
        catch (...)
        {
            Detach();
            throw;
        }
    }
    ~ControlRoom() override
    {
        Detach();
    }
    bool Matches(const Monitor &m) const
    {
        return SameMonitorTarget(target_, m);
    }
    int Tag() const
    {
        return tag_;
    }
    void OnConfirmValueCallback(NEuCon::uint32, NEuCon::uint32 control, NEuCon::uint32 member,
                                NEuCon::uint32 primitive, EuPrimitiveControl *,
                                NEuCon::uint16 &index) override
    {
        const int slot = Slot(control, member, primitive);
        if (slot < 0)
            return;
        // Only constrain the proposed index here. No SDK mutation, lock or I/O.
        if (!controller_.Epoch())
            index = feedback_[slot].load();
        else if (slot == 0)
            index = std::min(index, ceilingIndex_.load());
        else if (slot >= 6)
            index = 1; // A monitor source is selected, never toggled to no source.
        else if (slot == 5 && talkToMonitor_.load() && index)
            index = feedback_[slot].load();
    }
    void OnPrimitiveCallback(tEVT type, NEuCon::uint32 flags, NEuCon::uint32 control, NEuCon::uint32 member,
                             NEuCon::uint32 primitive, EuPrimitiveControl *affected, NEuCon::uint16 index,
                             void *) override
    {
        if (type != kEVT_PRIM_StateChange || (flags & kPRIMITIVE_FORCE_UPDATE))
            return;
        if (control == Level && primitive == EuControlKnob::kID_KnobTouchSense)
        {
            touched_ = index != 0;
            return;
        }
        if (control == DimAmount && primitive == EuControlKnob::kID_KnobTouchSense)
        {
            dimTouched_ = index != 0;
            return;
        }
        const int slot = Slot(control, member, primitive);
        if (slot < 0 || !affected)
            return;
        Event event;
        event.kind = Event::Kind::MonitorPrimitive;
        event.tag = tag_;
        event.type = type;
        event.flags = flags;
        event.control = control;
        event.member = member;
        event.primitive = primitive;
        event.index = index;
        event.thread = GetCurrentThreadId();
        event.epoch = controller_.Epoch();
        event.at = std::chrono::steady_clock::now();
        if (slot == 0 || slot == 4)
        {
            float value = 0;
            event.decodeResult = affected->GetValueAt(index, value);
            event.value = value;
            event.decoded = event.decodeResult == kERR_OK && std::isfinite(value);
        }
        else
        {
            NEuCon::int32 value = 0;
            event.decodeResult = affected->GetValueAt(index, value);
            event.value = value;
            event.decoded = event.decodeResult == kERR_OK && (value == 0 || value == 1);
        }
        inbox_.Push(event);
    }
    bool Dispatch(const Event &event)
    {
        if (event.tag != tag_ || !event.decoded || !event.epoch || event.epoch != controller_.Epoch() ||
            std::chrono::steady_clock::now() - event.at >= std::chrono::seconds(5))
            return false;
        const int slot = Slot(event.control, event.member, event.primitive);
        if (slot < 0)
            return false;
        if (slot >= 6)
        {
            if (!event.value || static_cast<size_t>(slot - 6) >= sourceValues_.size())
                return false;
            Json source;
            source.kind = Json::Kind::String;
            source.scalar = sourceValues_[slot - 6];
            return controller_.Submit(target_.key, MonitorField::Source, source, event.epoch);
        }
        if (slot == 4)
            return controller_.Submit(target_.key, MonitorField::DimAmount, ControlNumber(-event.value),
                                      event.epoch);
        if (slot == 5)
            return controller_.Submit(target_.key, MonitorField::Talk,
                                      Json::Parse(event.value ? "true" : "false"), event.epoch);
        const MonitorField fields[] = {MonitorField::Level, MonitorField::Mute, MonitorField::Dim,
                                       MonitorField::Mono};
        return controller_.Submit(target_.key, fields[slot],
                                  slot == 0 ? ControlNumber(event.value)
                                            : Json::Parse(event.value ? "true" : "false"),
                                  event.epoch);
    }
    void Apply(const Monitor &raw)
    {
        const auto status = controller_.Status(); // release application lock before SDK calls
        const auto m = controller_.Feedback(raw);
        const double ceiling = status.epoch ? status.ceiling : m.level->value.Number();
        const auto end = std::upper_bound(table_.begin(), table_.end(), ceiling);
        ceilingIndex_ = static_cast<NEuCon::uint16>(end == table_.begin() ? 0 : end - table_.begin() - 1);
        auto &knob = Primitive(level_, EuControlKnob::kID_Knob);
        NEuCon::uint16 current = 0;
        Check(knob.GetIndexForValue(static_cast<float>(m.level->value.Number()), current),
              "Monitor value index");
        feedback_[0] = current;
        if (!touched_ || !status.epoch)
            Check(knob.SetCurrentIndex(current), "Monitor level feedback");
        if (lastSource_ != m.source)
        {
            UpdateText(Primitive(level_, EuControlKnob::kID_KnobLabelDisplay), SourceLabel(m.source),
                       "Monitor source label");
            lastSource_ = m.source;
        }
        size_t slot = 1;
        for (auto item : {std::pair<EuControlSwitch *, const std::optional<Parameter> *>{&mute_, &m.mute},
                          {&dim_, &m.dim},
                          {&mono_, &m.mono}})
        {
            const bool published = slot == 1 ? hasMute_ : (slot == 2 ? hasDim_ : hasMono_);
            if (published && *item.second)
            {
                feedback_[slot] = (*item.second)->value.Bool() ? 1 : 0;
                ToggleFeedback(*item.first, EuControlSwitch::kID_Switch, EuControlSwitch::kID_Led,
                               **item.second);
            }
            ++slot;
        }
        if (hasDimAmount_)
        {
            auto &depth = Primitive(dimAmount_, EuControlKnob::kID_Knob);
            Check(depth.GetIndexForValue(static_cast<float>(-m.dimAttenuation->value.Number()), current),
                  "Dim depth index");
            feedback_[4] = current;
            if (!dimTouched_ || !status.epoch)
                Check(depth.SetCurrentIndex(current), "Dim depth feedback");
        }
        if (hasTalk_)
        {
            talkToMonitor_ = m.talkbackToMonitor->value.Bool();
            feedback_[5] = m.talk->value.Bool() ? 1 : 0;
            ToggleFeedback(talk_, EuControlSwitch::kID_Switch, EuControlSwitch::kID_Led, *m.talk);
        }
        for (size_t i = 0; i < sourceButtons_.size(); ++i)
        {
            Parameter state;
            state.value = Json::Parse(m.source == sourceValues_[i] ? "true" : "false");
            feedback_[6 + i] = state.value.Bool() ? 1 : 0;
            ToggleFeedback(*sourceButtons_[i], EuControlSwitch::kID_Switch, EuControlSwitch::kID_Led, state);
        }
    }

  private:
    enum : NEuCon::uint32
    {
        Level = 1,
        Mute,
        Dim,
        Formats,
        DimAmount,
        Talk,
        Sources
    };
    int Slot(NEuCon::uint32 control, NEuCon::uint32 member, NEuCon::uint32 primitive) const
    {
        if (control == Level && primitive == EuControlKnob::kID_Knob)
            return 0;
        if (control == DimAmount && hasDimAmount_ && primitive == EuControlKnob::kID_Knob)
            return 4;
        if (primitive != EuControlSwitch::kID_Switch)
            return -1;
        if (control == Mute && hasMute_)
            return 1;
        if (control == Dim && hasDim_)
            return 2;
        if (control == Formats && monoAdded_ && member == monoMember_)
            return 3;
        if (control == Talk && hasTalk_)
            return 5;
        if (control == Sources)
            for (size_t i = 0; i < sourceMembers_.size(); ++i)
                if (member == sourceMembers_[i])
                    return static_cast<int>(6 + i);
        return -1;
    }
    static std::wstring SourceLabel(const std::string &source)
    {
        return source == "mon"
                   ? L"Mix"
                   : (source.size() == 4 && source.rfind("cue", 0) == 0 ? L"Cue " + Wide(source.substr(3))
                                                                        : L"Unknown");
    }
    void Add(EuControl &control, NEuCon::uint32 id, int layout, const wchar_t *name)
    {
        Check(control.SetId(id), "Monitor control ID");
        Check(control.SetPersistenceID(name), "Monitor control persistence");
        Check(control.SetAttribute2(kATRIBID_LayoutName0, layout), "Monitor control layout");
        Check(control.SetAttribute2(kATRIBID_SimpleUserVisibleName, name), "Monitor control name");
        Check(AddControl(control), "Add monitor control");
        controls_.push_back(&control);
    }
    static bool WritableButton(const Monitor &m, MonitorField field)
    {
        return MonitorFieldAvailable(m, field);
    }
    void InitButton(EuControlSwitch &button)
    {
        auto &p = Primitive(button, EuControlSwitch::kID_Switch);
        Switch(p);
        p.MakeConfirmationCallback(true);
        Initialize(Primitive(button, EuControlSwitch::kID_Led), kTYP_Int, 4);
        Check(Primitive(button, EuControlSwitch::kID_Led).LoadValueTableInterpolated(0, 3),
              "Monitor LED table");
        Check(button.SetLedOverride(true), "Monitor LED ownership");
    }
    void Detach() noexcept
    {
        SetIsBeingDestroyed();
        if (monoAdded_)
            CleanupResult(formats_.Remove(monoMember_), "Remove mono format");
        for (auto member : sourceMembers_)
            CleanupResult(sources_.Remove(member), "Remove monitor source");
        for (auto *c : controls_)
            CleanupResult(RemoveControl(*c), "Remove monitor control");
    }
    const Monitor target_;
    const int tag_;
    Inbox &inbox_;
    MonitorController &controller_;
    EuControlKnob level_;
    EuControlSwitch mute_, dim_;
    EuControlSwitchArray formats_;
    EuControlSwitch mono_;
    EuControlKnob dimAmount_;
    EuControlSwitch talk_;
    EuControlSwitchArray sources_;
    std::vector<std::string> sourceValues_;
    std::string lastSource_;
    std::vector<std::unique_ptr<EuControlSwitch>> sourceButtons_;
    std::vector<NEuCon::uint32> sourceMembers_;
    NEuCon::uint32 monoMember_ = 0;
    bool monoAdded_ = false;
    bool hasMute_ = false, hasDim_ = false, hasMono_ = false;
    bool hasDimAmount_ = false, hasTalk_ = false;
    std::vector<EuControl *> controls_;
    std::vector<float> table_;
    std::atomic<bool> touched_ = false;
    std::atomic<bool> dimTouched_ = false, talkToMonitor_ = false;
    std::atomic<NEuCon::uint16> feedback_[11]{};
    std::atomic<NEuCon::uint16> ceilingIndex_ = 0;
};
#include "ConfigKnob.h"
#include "ChannelKnobSets.h"
#include "UpperKnobSets.h"
class Strip final : public EuProcessor
{
  public:
    Strip(const Channel &channel, int tag, int order, Inbox &inbox, EuNode &node, bool config = false)
        : tag_(tag), inbox_(inbox), fader_(this), name_(this), number_(this), format_(this), solo_(this),
          rec_(this), pan_(this), meter_(this), outputName_(this), inputName_(this), knobs_(*this, node, config),
          monitorKnobs_(*this, config), upper_(*this, tag)
    {
        controls_.reserve(8);
        panIds_.reserve(2);
        panCells_.reserve(2);
        try
        {
            if (channel.monitor)
                throw std::invalid_argument("A monitor cannot be registered as a channel strip");
            Check(SetPersistenceID(Wide(channel.key)), "Channel persistence");
            Check(SetAttribute2(kATRIBID_ProcessorType, kProcType_ChannelStrip), "Channel type");
            Check(SetAttribute2(kATRIBID_LayoutRule0, kRUL_EuLayoutChannel), "Channel layout");
            Check(SetAttribute2(kATRIBID_TrackType, channel.auxiliary ? kTRACK_Aux : kTRACK_Input),
                  "Track type");
            Check(SetAttribute2(kATRIBID_ChannelType, channel.auxiliary ? L"Aux" : L"Input"), "Track label");
            Check(SetAttribute2(kATRIBID_ChannelColor, static_cast<NEuCon::int32>(ChannelColor(channel))),
                  "Channel color");
            Text(name_, Name, EuLayoutChannel::kNAM_Name, Wide(channel.name));
            Text(number_, Number, EuLayoutChannel::kNAM_ChannelNumber, std::to_wstring(order));
            Text(format_, Format, EuLayoutChannel::kNAM_ChannelFormatDisplay,
                 channel.stereo ? L"Stereo" : L"Mono");
            Text(outputName_, 30, EuLayoutChannel::kNAM_OutputName, Wide(channel.destination));
            Text(inputName_, 31, EuLayoutChannel::kNAM_InputName, Wide(channel.ioType));
            if (channel.level || channel.mute)
                Add(fader_, Fader, EuLayoutChannel::kNAM_Fader, L"Fader");
            if (channel.level)
            {
                RawSwitch(Primitive(fader_, EuControlFader::kID_SliderTouchSense));
                auto &slider = Primitive(fader_, EuControlFader::kID_Slider);
                // A sorted dB table; this does not assume that UA's tapered scalar
                // has the Windows volume curve. Callbacks decode this exact table.
                const float low = static_cast<float>(channel.level->minimum.value_or(-144));
                const float high = static_cast<float>(channel.level->maximum.value_or(12));
                const auto table = FaderDbTable(low, high);
                Initialize(slider, kTYP_Float, static_cast<NEuCon::uint16>(table.size()));
                Check(slider.LoadValueTable(table, 1), "Fader dB table");
                hasLevel_ = true;
                levelMinimum_ = channel.level->minimum;
                levelMaximum_ = channel.level->maximum;
            }
            if (channel.mute)
            {
                Switch(Primitive(fader_, EuControlFader::kID_Mute));
                Initialize(Primitive(fader_, EuControlFader::kID_MuteLed), kTYP_Int, 4);
                Check(Primitive(fader_, EuControlFader::kID_MuteLed).LoadValueTableInterpolated(0, 3),
                      "Mute LED table");
                hasMute_ = true;
            }
            if (channel.solo)
            {
                Add(solo_, Solo, EuLayoutChannel::kNAM_Solo, L"Solo");
                Switch(Primitive(solo_, EuControlSwitch::kID_Switch));
                Initialize(Primitive(solo_, EuControlSwitch::kID_Led), kTYP_Int, 4);
                Check(Primitive(solo_, EuControlSwitch::kID_Led).LoadValueTableInterpolated(0, 3),
                      "Solo LED table");
                Check(solo_.SetLedOverride(true), "Solo LED ownership");
                hasSolo_ = true;
            }
            if (Available(channel, ChannelField::RecordPreEffects))
            {
                // Explicit user-selected Console mapping, not DAW record arming.
                Add(rec_, Rec, EuLayoutChannel::kNAM_RecordArm, L"UAD.RecordEffects");
                Switch(Primitive(rec_, EuControlSwitch::kID_Switch));
                Initialize(Primitive(rec_, EuControlSwitch::kID_Led), kTYP_Int, 4);
                Check(Primitive(rec_, EuControlSwitch::kID_Led).LoadValueTableInterpolated(0, 3),
                      "UAD REC LED");
                Check(rec_.SetLedOverride(true), "UAD REC LED ownership");
                hasRec_ = true;
            }
            upper_.InitializeDirectory();
            knobs_.beforeRemove = [&](EuControlKnobCellArray *page) { upper_.UnlinkPage(page); };
            RebuildPan(channel);
            knobs_.Rebuild(channel);
            SyncUpper();
            Add(meter_, MeterControl, EuLayoutChannel::kNAM_ChannelLevelMeter, L"Meter");
            Check(meter_.SetUserData(tag_, nullptr), "Meter identity");
            for (unsigned leg = 0; leg < 2; ++leg)
            {
                for (unsigned id :
                     {EuControlMultiMeter::kID_Meter0 + leg, EuControlMultiMeter::kID_PeakHold0 + leg})
                {
                    auto &p = Primitive(meter_, id);
                    Initialize(p, kTYP_Float, 145);
                    Check(p.LoadValueTableInterpolated(-144.0F, 0.0F, 1), "Meter table");
                }
                auto &led = Primitive(meter_, EuControlMultiMeter::kID_MeterClipLed0 + leg);
                Initialize(led, kTYP_Int, 4);
                Check(led.LoadValueTableInterpolated(0, 3), "Clip table");
            }
            Apply(channel, order, false);
        }
        catch (...)
        {
            DetachControls();
            throw;
        }
    }
    ~Strip() override
    {
        DetachControls();
    }
    void DetachControls() noexcept
    {
        SetIsBeingDestroyed();
        upper_.Clear();
        monitorKnobs_.Clear();
        knobs_.Clear();
        for (auto it = panIds_.rbegin(); it != panIds_.rend(); ++it)
            CleanupResult(pan_.Remove(*it), "Remove pan cell");
        for (auto *control : controls_)
            CleanupResult(RemoveControl(*control), "Remove control");
    }
    void OnPrimitiveCallback(tEVT type, NEuCon::uint32 flags, NEuCon::uint32 control, NEuCon::uint32 member,
                             NEuCon::uint32 primitive, EuPrimitiveControl *affected, NEuCon::uint16 index,
                             void *) override
    {
        if (type != kEVT_PRIM_StateChange || (flags & kPRIMITIVE_FORCE_UPDATE))
            return;
        if (control == Fader && primitive == EuControlFader::kID_SliderTouchSense)
            touched_ = index != 0;
        if (control == Pan && primitive == EuControlKnobCell::kID_KnobTouchSense)
        {
            if (member == leftMember_)
                leftTouched_ = index != 0;
            if (member == rightMember_)
                rightTouched_ = index != 0;
        }
        if (control >= ChannelKnobSets::FirstId && primitive == EuControlKnobCell::kID_KnobTouchSense)
        {
            try
            {
                knobs_.Touch(control, member, index != 0);
            }
            catch (...)
            {
                inbox_.overflow = true;
                return;
            }
        }
        if (control == MonitorKnobSet::Id && primitive == EuControlKnobCell::kID_KnobTouchSense)
        {
            try
            {
                monitorKnobs_.Touch(member, index != 0);
            }
            catch (...)
            {
                inbox_.overflow = true;
                return;
            }
        }
        Event event;
        event.tag = tag_;
        event.type = type;
        event.flags = flags;
        event.control = control;
        event.member = member;
        event.primitive = primitive;
        event.index = index;
        event.thread = GetCurrentThreadId();
        event.epoch = control == MonitorKnobSet::Id ? monitorEpoch_.load() : channelEpoch_.load();
        event.configEpoch = configEpoch_.load();
        if (control == MonitorKnobSet::Id) event.configModel = monitorKnobs_.Revision();
        event.at = std::chrono::steady_clock::now();
        if (affected &&
            ((control == Fader && primitive == EuControlFader::kID_Slider) ||
             ((control == Pan || control == MonitorKnobSet::Id || control >= ChannelKnobSets::FirstId) &&
              primitive == EuControlKnobCell::kID_Knob)))
        {
            float value = 0;
            event.decodeResult = affected->GetValueAt(index, value);
            event.value = value;
            event.decoded = event.decodeResult == kERR_OK && std::isfinite(value);
        }
        else if (affected &&
                 ((control == Fader && primitive == EuControlFader::kID_Mute) ||
                  ((control == Solo || control == Rec) && primitive == EuControlSwitch::kID_Switch) ||
                  ((control == MonitorKnobSet::Id || control >= ChannelKnobSets::FirstId) &&
                   primitive == EuControlKnobCell::kID_LowerSwitch)))
        {
            NEuCon::int32 value = 0;
            event.decodeResult = affected->GetValueAt(index, value);
            event.value = value;
            event.decoded = event.decodeResult == kERR_OK && (value == 0 || value == 1);
        }
        else if (affected && control == Pan && primitive == EuControlKnobCell::kID_KnobTopSwitch)
        {
            // OneShot always reads as zero. The callback itself is the action;
            // member identifies the active mono/left/right pan peer.
            event.decodeResult = kERR_OK;
            event.value = 0;
            event.decoded = true;
        }
        inbox_.Push(event); // No transport, logging or SDK pointer escapes the callback.
    }
    bool Touched() const
    {
        return touched_ || leftTouched_ || rightTouched_ || knobs_.Touched() || monitorKnobs_.Touched();
    }
    bool Dispatch(const Event &event, const Channel &channel, ChannelController &controller,
                  const std::optional<Monitor> &monitor, MonitorController &monitorController,
                  ConfigController *configuration = nullptr)
    {
        if (event.control == MonitorKnobSet::Id)
            return monitor && monitorKnobs_.Dispatch(event, *monitor, monitorController, configuration);
        if (knobs_.IsConfig(event.control, event.member))
            return configuration && knobs_.DispatchConfig(event, channel, *configuration);
        if (knobs_.IsReadOnlyConfig(event.control, event.member))
            return false; // A preview gesture cannot write or revoke channel authorization.
        if (event.epoch && event.epoch == controller.Epoch(channel.key) &&
            std::chrono::steady_clock::now() - event.at >= std::chrono::seconds(5))
        {
            Log("Expired channel gesture discarded; permission preserved");
            return false;
        }
        if (!event.decoded || !event.epoch || event.epoch != controller.Epoch(channel.key))
            return false;
        if (event.control >= ChannelKnobSets::FirstId)
            return knobs_.Dispatch(event, channel, controller);
        if (event.control == Rec && event.primitive == EuControlSwitch::kID_Switch)
            return controller.Submit(channel.key, ChannelField::RecordPreEffects,
                                     Json::Parse(event.value != 0 ? "false" : "true"), event.epoch);
        std::optional<ChannelField> field;
        if (event.control == Fader && event.primitive == EuControlFader::kID_Slider)
            field = ChannelField::Level;
        else if (event.control == Fader && event.primitive == EuControlFader::kID_Mute)
            field = ChannelField::Mute;
        else if (event.control == Solo && event.primitive == EuControlSwitch::kID_Switch)
            field = ChannelField::Solo;
        else if (event.control == Pan &&
                 (event.primitive == EuControlKnobCell::kID_Knob ||
                  event.primitive == EuControlKnobCell::kID_KnobTopSwitch))
        {
            if (event.member == leftMember_)
                field = ChannelField::PanLeft;
            else if (event.member == rightMember_)
                field = ChannelField::PanRight;
        }
        if (!field)
            return false; // Touch and the runtime's SEL peer switch never write.
        const bool boolean = *field == ChannelField::Mute || *field == ChannelField::Solo;
        const bool pan = *field == ChannelField::PanLeft || *field == ChannelField::PanRight;
        const bool panCenter = pan && event.primitive == EuControlKnobCell::kID_KnobTopSwitch;
        return controller.Submit(channel.key, *field,
                                 boolean ? Json::Parse(event.value != 0 ? "true" : "false")
                                         : ControlNumber(pan ? (panCenter ? 0.0 : event.value / 100.0)
                                                             : event.value),
                                 event.epoch);
    }
    int Tag() const
    {
        return tag_;
    }
    void SetEpoch(uint64_t epoch)
    {
        channelEpoch_ = epoch;
    }
    void SyncUpper()
    {
        upper_.Link(UpperFunction::Inserts, knobs_.Page(ChannelFunction::Inserts));
        upper_.Link(UpperFunction::Input, knobs_.Page(ChannelFunction::Input));
        upper_.Link(UpperFunction::Aux, knobs_.Page(ChannelFunction::Aux));
        upper_.Link(UpperFunction::Pan, panMask_ > 0 ? &pan_ : nullptr);
        upper_.Link(UpperFunction::Mix, knobs_.Page(ChannelFunction::Mix));
        upper_.Link(UpperFunction::Unison, knobs_.Page(ChannelFunction::Unison));
        upper_.Link(UpperFunction::Console, knobs_.Page(ChannelFunction::Console));
        upper_.Link(UpperFunction::ControlRoom, monitorKnobs_.Page());
    }
    void ApplyMonitor(const std::optional<Monitor> &monitor, uint64_t epoch)
    {
        monitorEpoch_ = monitor ? epoch : 0;
        if (!monitorKnobs_.Matches(monitor) && !Touched())
        {
            Check(Freeze(), "Monitor extension owner freeze");
            try
            {
                upper_.UnlinkPage(monitorKnobs_.Page());
                monitorKnobs_.Rebuild(monitor);
                SyncUpper();
            }
            catch (...)
            {
                CleanupResult(Thaw(), "Monitor extension failed thaw");
                throw;
            }
            Check(Thaw(), "Monitor extension owner thaw");
        }
        if (monitor)
            monitorKnobs_.Apply(*monitor);
    }
    bool UpperLinksForTest() const
    {
        return upper_.BindingsForTest() &&
               upper_.ChildForTest(UpperFunction::Inserts) == knobs_.Page(ChannelFunction::Inserts) &&
               upper_.ChildForTest(UpperFunction::Input) == knobs_.Page(ChannelFunction::Input) &&
               upper_.ChildForTest(UpperFunction::Aux) == knobs_.Page(ChannelFunction::Aux) &&
               upper_.ChildForTest(UpperFunction::Pan) == (panMask_ > 0 ? &pan_ : nullptr) &&
               upper_.ChildForTest(UpperFunction::Mix) == knobs_.Page(ChannelFunction::Mix) &&
               upper_.ChildForTest(UpperFunction::Unison) == knobs_.Page(ChannelFunction::Unison) &&
               upper_.ChildForTest(UpperFunction::Console) == knobs_.Page(ChannelFunction::Console) &&
               !upper_.ChildForTest(UpperFunction::Dynamics) && !upper_.ChildForTest(UpperFunction::Eq) &&
               !upper_.ChildForTest(UpperFunction::Group);
    }
    bool WrongUpperLinkRejectedForTest()
    {
        auto *input = knobs_.Page(ChannelFunction::Input);
        if (!input)
            return false;
        try
        {
            upper_.Link(UpperFunction::Eq, input);
        }
        catch (const std::invalid_argument &)
        {
            return UpperLinksForTest();
        }
        return false;
    }
    bool UpperMonitorForTest() const
    {
        return upper_.ChildForTest(UpperFunction::ControlRoom) != nullptr;
    }
    void ApplyGlobalConfig(const Snapshot &state, uint64_t epoch = 0)
    {
        configEpoch_ = state.connected ? epoch : 0;
        knobs_.ApplyGlobalConfig(state.globalConfig, state.connected);
    }
    bool ConfigMarkersForTest()
    {
        return knobs_.ConfigMarkersForTest() && monitorKnobs_.ConfigMarkersForTest();
    }
    bool GlobalConfigReadbackForTest(const std::string &key, const std::wstring &expected)
    {
        return knobs_.GlobalConfigReadbackForTest(key, expected);
    }
    bool ConfigValueForTest(const std::string &key, const std::wstring &expected) const
    { return knobs_.ConfigValueForTest(key, expected); }
    bool RingModesForTest() const
    { return knobs_.RingModesForTest() && monitorKnobs_.RingModesForTest(); }
    bool PanResetSwitchesForTest() const
    {
        for (const auto &cell : panCells_)
        {
            auto &primitive = Primitive(*cell, EuControlKnobCell::kID_KnobTopSwitch);
            auto *button = dynamic_cast<EuPrimitiveSwitch *>(&primitive);
            tSWITCH mode = kSWITCH_NumSwitchmodes;
            NEuCon::uint16 size = 0;
            if (!button || button->GetSwitchMode(mode) != kERR_OK || mode != kSWITCH_OneShot ||
                primitive.GetValueTableSize(size) != kERR_OK || size != 1)
                return false;
        }
        return panCells_.size() ==
               static_cast<size_t>((panMask_ & 1 ? 1 : 0) + (panMask_ & 2 ? 1 : 0));
    }
    NEuCon::uint16 RecIndexForTest()
    {
        NEuCon::uint16 index = 0;
        Check(Primitive(rec_, EuControlSwitch::kID_Switch).GetCurrentIndex(index), "UAD REC test readback");
        return index;
    }
    NEuCon::uint32 PageIdForTest(ChannelFunction function) const
    {
        return knobs_.PageIdForTest(function);
    }
    bool Visible() const
    {
        return visible_;
    }
    bool MatchesCapabilities(const Channel &channel) const
    {
        return hasLevel_ == channel.level.has_value() && hasMute_ == channel.mute.has_value() &&
               hasRec_ == Available(channel, ChannelField::RecordPreEffects) &&
               hasSolo_ == channel.solo.has_value() &&
               (!channel.level ||
                (channel.level->minimum == levelMinimum_ && channel.level->maximum == levelMaximum_));
    }
    void Visibility(const Event &event)
    {
        visible_ = event.visible;
        handle_ = event.visible ? event.handle : kEuInvalidVisibilityHandle;
        meterFormat_ = event.format;
    }
    void Apply(const Channel &channel, int order, bool registered = true)
    {
        if (order < 0)
            order = order_;
        if (!Touched() && !knobs_.Matches(channel))
        {
            Check(Freeze(), "Channel feature update freeze");
            try
            {
                knobs_.Rebuild(channel);
                SyncUpper();
            }
            catch (...)
            {
                CleanupResult(Thaw(), "Channel feature failure thaw");
                throw;
            }
            Check(Thaw(), "Channel feature update thaw");
        }
        knobs_.Apply(channel);
        const auto color = ChannelColor(channel);
        if (color != lastColor_)
        {
            Check(SetAttribute2(kATRIBID_ChannelColor, static_cast<NEuCon::int32>(color), true),
                  "Channel color feedback");
            lastColor_ = color;
        }
        if (hasRec_ && channel.recordPreEffects)
        {
            auto printed = *channel.recordPreEffects;
            printed.value = Json::Parse(channel.recordPreEffects->value.Bool() ? "false" : "true");
            ToggleFeedback(rec_, EuControlSwitch::kID_Switch, EuControlSwitch::kID_Led, printed);
        }
        if (channel.destination != lastOutput_)
        {
            UpdateText(Primitive(outputName_, 0), Wide(channel.destination), "Output name feedback");
            lastOutput_ = channel.destination;
        }
        if (channel.ioType != lastInput_)
        {
            UpdateText(Primitive(inputName_, 0), Wide(channel.ioType), "Input name feedback");
            lastInput_ = channel.ioType;
        }
        if (order != order_)
            Check(SetAttribute2(kATRIBID_ChannelOrder, order, true), "Channel order");
        if (channel.name != lastName_)
        {
            UpdateText(Primitive(name_, 0), Wide(channel.name), "Channel name");
            Check(SetUserVisibleName(Wide(channel.name)), "Channel visible name");
            lastName_ = channel.name;
        }
        if (order != order_)
        {
            UpdateText(Primitive(number_, 0), std::to_wstring(order), "Channel number");
            order_ = order;
        }
        if (hasLevel_ && channel.level && !touched_)
            Check(Primitive(fader_, EuControlFader::kID_Slider)
                      .SetCurrentValue(static_cast<float>(channel.level->value.Number())),
                  "Fader feedback");
        if (hasMute_ && channel.mute)
            ToggleFeedback(fader_, EuControlFader::kID_Mute, EuControlFader::kID_MuteLed, *channel.mute);
        if (hasSolo_ && channel.solo)
            ToggleFeedback(solo_, EuControlSwitch::kID_Switch, EuControlSwitch::kID_Led, *channel.solo);
        if (!Touched() &&
            (panMask_ != (channel.pan ? 1 : 0) + (channel.panRight ? 2 : 0) || panStereo_ != channel.stereo))
        {
            Check(Freeze(), "Processor freeze");
            try
            {
                RebuildPan(channel);
                SyncUpper();
            }
            catch (...)
            {
                CleanupResult(Thaw(), "Processor thaw");
                throw;
            }
            Check(Thaw(), "Processor thaw");
        }
        if (panMask_ == (channel.pan ? 1 : 0) + (channel.panRight ? 2 : 0) && panStereo_ == channel.stereo)
        {
            size_t cell = 0;
            for (const auto *parameter : {&channel.pan, &channel.panRight})
                if (*parameter)
                {
                    const bool touch = parameter == &channel.pan ? leftTouched_.load() : rightTouched_.load();
                    if (!touch)
                        Check(Primitive(*panCells_[cell], EuControlKnobCell::kID_Knob)
                                  .SetCurrentValue(static_cast<float>((*parameter)->value.Number() * 100)),
                              "Pan feedback");
                    ++cell;
                }
        }
        if (!formatReady_ || stereo_ != channel.stereo || outputFormat_ != channel.outputFormat)
        {
            Check(SetAttribute2(kATRIBID_ChannelFormat, channel.stereo ? L"Stereo" : L"Mono", true),
                  "Channel format");
            const auto output =
                channel.outputFormat == AudioFormat::Stereo
                    ? kFORMAT_Stereo
                    : (channel.outputFormat == AudioFormat::Mono ? kFORMAT_Mono : kFORMAT_Unknown);
            Check(SetAttribute2(kATRIBID_TrackFormat, output, true), "Output track format");
            UpdateText(Primitive(format_, 0), channel.stereo ? L"Stereo" : L"Mono", "Format display");
            outputFormat_ = channel.outputFormat;
            formatReady_ = true;
        }
        if (registered && (!meterReady_ || stereo_ != channel.stereo))
        {
            stereo_ = channel.stereo;
            visible_ = false;
            handle_ = kEuInvalidVisibilityHandle;
            Check(meter_.SetAttribute2(kATRIBID_MeterType, kMeterType__SamplePeak, true), "Meter scale");
            Check(SetAttribute2(kATRIBID_MeterType, kMeterType__SamplePeak, true), "Channel meter scale");
            const std::vector<NEuCon::uint32> roles = stereo_
                                                          ? std::vector<NEuCon::uint32>{kMTR_Left, kMTR_Right}
                                                          : std::vector<NEuCon::uint32>{kMTR_Mono};
            Check(meter_.SetFormat(
                      EuMakeMeterFormat(roles.size(), kEuMFMT_PL_Level | kEuMFMT_PL_Peak | kEuMFMT_PL_Clip),
                      roles),
                  "Meter format");
            meterReady_ = true;
            Log("meter-format tag=" + std::to_string(tag_) + " legs=" + std::to_string(roles.size()) +
                " output-format=" + std::to_string(static_cast<int>(outputFormat_)) +
                " monitor=" + std::to_string(channel.monitor) + " units=dBFS type=SamplePeak");
        }
        values_ = channel.meters;
    }
    void Unavailable()
    {
        channelEpoch_ = 0;
        monitorEpoch_ = 0;
        if (lastName_ != "Offline")
        {
            UpdateText(Primitive(name_, 0), L"Offline", "Offline label");
            lastName_ = "Offline";
        }
        values_.clear();
    }
    void ApplyMeters(const std::vector<Meter> &meters) { values_ = meters; }
    void WriteMeters(EuBatchedMeterWriter &writer)
    {
        if (!visible_ || handle_ == kEuInvalidVisibilityHandle)
            return;
        const auto now = GetTickCount64();
        const bool trace = meterTraces_ < 12 && now >= nextMeterTrace_;
        if (trace)
        {
            ++meterTraces_;
            nextMeterTrace_ = now + 5000;
        }
        for (NEuCon::uint32 i = 0; i < EuMeterFormatNumLegs(meterFormat_); ++i)
        {
            const Meter empty;
            const auto &value = i < values_.size() ? values_[i] : empty;
            const auto result = writer.SetPerLegValuesV2(
                meter_, handle_, meterFormat_, i, static_cast<float>(value.levelDb.value_or(-144)),
                static_cast<float>(value.peakDb.value_or(-144)), 0, value.clip.value_or(false));
            if (trace)
                Log("meter-sample tag=" + std::to_string(tag_) + " leg=" + std::to_string(i) +
                    " level-dBFS=" + (value.levelDb ? std::to_string(*value.levelDb) : "unavailable") +
                    " peak-dBFS=" + (value.peakDb ? std::to_string(*value.peakDb) : "unavailable") +
                    " handle=" + std::to_string(handle_) + " format=" + std::to_string(meterFormat_) +
                    " result=" + std::to_string(result));
            if (result != kEUBMR_OK)
            {
                Log("meter-write tag=" + std::to_string(tag_) + " error=" + std::to_string(result));
                visible_ = false;
                handle_ = kEuInvalidVisibilityHandle;
                break;
            }
        }
    }

  private:
    enum : NEuCon::uint32
    {
        Fader = 1,
        Name,
        Number,
        Format,
        Solo,
        Pan,
        MeterControl,
        Rec
    };
    void Add(EuControl &control, NEuCon::uint32 id, int layout, const wchar_t *pid)
    {
        Check(control.SetId(id), "Control ID");
        Check(control.SetPersistenceID(pid), "Control persistence");
        Check(control.SetAttribute2(kATRIBID_LayoutName0, layout), "Control layout");
        Check(AddControl(control), "Add control");
        controls_.push_back(&control);
    }
    void Text(EuControlTextDisplay &display, NEuCon::uint32 id, int layout, const std::wstring &text)
    {
        Add(display, id, layout, (L"Text" + std::to_wstring(id)).c_str());
        Label(Primitive(display, 0), text);
    }
    void RebuildPan(const Channel &channel)
    {
        if (!panAdded_)
        {
            if (!channel.pan && !channel.panRight)
            {
                panMask_ = 0;
                panStereo_ = channel.stereo;
                return;
            }
            Add(pan_, Pan, EuLayoutChannel::kNAM_Pan, L"Pan");
            Check(pan_.SetAttribute2(kATRIBID_FuncPersID, L"Avid.Chan.Pan"), "Pan function");
            panAdded_ = true;
        }
        Check(pan_.Freeze(), "Pan freeze");
        try
        {
            for (auto it = panIds_.rbegin(); it != panIds_.rend(); ++it)
                Check(pan_.Remove(*it), "Remove pan cell");
            panIds_.clear();
            panCells_.clear();
            leftMember_ = rightMember_ = 0;
            Check(pan_.SetPeerSubstituteSwitch(EuControlKnobCell::kID_UpperSwitch), "Pan peer selector");
            for (int index = 0; index < 2; ++index)
            {
                const auto &parameter = index == 0 ? channel.pan : channel.panRight;
                if (!parameter)
                    continue;
                auto knob = std::make_unique<EuControlKnobCell>(this);
                Check(knob->SetPersistenceID(index == 0 ? L"PanLeft" : L"PanRight"), "Pan cell persistence");
                auto &p = Primitive(*knob, EuControlKnobCell::kID_Knob);
                Initialize(p, kTYP_Float, 201);
                Check(p.LoadValueTableInterpolated(-100.0F, 100.0F, 0), "Pan table");
                Check(p.SetAttribute2(kATRIBID_LayoutName1,
                                      index == 0 ? EuLayoutPan::kNAM_LeftPan : EuLayoutPan::kNAM_RightPan),
                      "Native pan semantic");
                RawSwitch(Primitive(*knob, EuControlKnobCell::kID_KnobTouchSense));
                OneShotSwitch(Primitive(*knob, EuControlKnobCell::kID_KnobTopSwitch));
                auto *rotary = dynamic_cast<EuPrimitiveKnob *>(&p);
                if (!rotary)
                    throw std::runtime_error("Missing rotary primitive");
                Check(rotary->SetPositionRingMode(kRingCenterAnchored), "Pan ring");
                for (int i = 0; i <= 200; ++i)
                {
                    const auto label =
                        i == 100 ? L"Center" : std::to_wstring(std::abs(i - 100)) + (i < 100 ? L" L" : L" R");
                    Check(p.LoadValueAt(static_cast<NEuCon::uint16>(i), label), "Pan value text");
                }
                Label(Primitive(*knob, EuControlKnobCell::kID_KnobLabelDisplay),
                      channel.stereo ? (index == 0 ? L"Pan L" : L"Pan R") : L"Pan");
                if (channel.pan && channel.panRight)
                    Switch(Primitive(*knob, EuControlKnobCell::kID_UpperSwitch));
                NEuCon::uint32 member = 0;
                if (index == 1 && channel.pan)
                    Check(pan_.AddPeerKnobCell(panIds_.front(), knob.get(), member), "Add right pan peer");
                else
                    Check(pan_.PushBack(knob.get(), member), "Add pan cell");
                panIds_.push_back(member);
                if (index == 0)
                    leftMember_ = member;
                else
                    rightMember_ = member;
                panCells_.push_back(std::move(knob));
            }
            panMask_ = (channel.pan ? 1 : 0) + (channel.panRight ? 2 : 0);
            panStereo_ = channel.stereo;
        }
        catch (...)
        {
            CleanupResult(pan_.Thaw(), "Pan thaw");
            throw;
        }
        Check(pan_.Thaw(), "Pan thaw");
    }
    int tag_, order_ = -1, panMask_ = -1;
    uint32_t lastColor_ = 0;
    Inbox &inbox_;
    std::atomic<uint64_t> channelEpoch_ = 0;
    std::atomic<bool> touched_ = false;
    std::atomic<bool> leftTouched_ = false, rightTouched_ = false;
    std::atomic<NEuCon::uint32> leftMember_ = 0, rightMember_ = 0;
    EuControlFader fader_;
    EuControlTextDisplay name_, number_, format_;
    EuControlSwitch solo_, rec_;
    EuControlKnobCellArray pan_;
    EuControlMultiMeter meter_;
    EuControlTextDisplay outputName_, inputName_;
    ChannelKnobSets knobs_;
    MonitorKnobSet monitorKnobs_;
    UpperDirectory upper_;
    std::atomic<uint64_t> monitorEpoch_{0};
    std::atomic<uint64_t> configEpoch_{0};
    std::string lastOutput_, lastInput_;
    std::vector<std::unique_ptr<EuControlKnobCell>> panCells_;
    std::vector<NEuCon::uint32> panIds_;
    std::vector<EuControl *> controls_;
    bool hasLevel_ = false, hasMute_ = false, hasSolo_ = false, hasRec_ = false, panAdded_ = false;
    bool stereo_ = false, meterReady_ = false, visible_ = false, formatReady_ = false;
    AudioFormat outputFormat_ = AudioFormat::Unknown;
    ULONGLONG nextMeterTrace_ = 0;
    unsigned meterTraces_ = 0;
    bool panStereo_ = false;
    std::optional<double> levelMinimum_, levelMaximum_;
    std::string lastName_;
    tVisibilityHandle handle_ = kEuInvalidVisibilityHandle;
    tEuMeterFormat meterFormat_ = kEuInvalidMeterFormat;
    std::vector<Meter> values_;
};
} // namespace
struct ApolloEucon::Impl
{
    Inbox inbox;
    std::unique_ptr<Node> node;
    std::map<std::string, std::unique_ptr<Strip>> strips;
    std::unique_ptr<ControlRoom> controlRoom;
    std::unique_ptr<ApplicationCommands> applicationCommands;
    bool manager = false, registered = false;
    int nextTag = 1;
    uint64_t eventCount = 0;
    FeedbackUpdatePolicy refreshPolicy;
    ChannelController &controller;
    MonitorController &monitorController;
    const bool experimentalConfig;
    ConfigController *configuration;
    explicit Impl(const Snapshot &initial, ChannelController &control, MonitorController &monitor, bool config,
                  ConfigController *settings, std::function<void()> wakeOwner)
        : controller(control), monitorController(monitor), experimentalConfig(config), configuration(settings)
    {
        try
        {
            inbox.wake = std::move(wakeOwner);
            Check(EuConManager::Initialize(), "EUCON initialize");
            manager = true;
            Log(std::string("experimental-config=") + (config ? "1 separately-confirmed-writes=1" : "0"));
            node = std::make_unique<Node>(inbox);
            Check(node->Freeze(), "Initial node freeze");
            Check(node->SetPersistenceID(L"Lindelea.ApolloBridge.EUCON.v1"), "Node persistence");
            Check(node->SetSimpleFriendlyName(L"UAD Console Bridge for EUCON"), "Node friendly name");
            Check(node->SetUserVisibleName(L"UAD Console Bridge for EUCON"), "Node visible name");
            Check(node->SetAttribute2(kATRIBID_ProcessorMeterAPIVersion, kMeterAPIVersion_3_1), "Meter API");
            applicationCommands = std::make_unique<ApplicationCommands>(inbox);
            Check(node->RegisterProcessor(*applicationCommands), "Register command processor");
            int order = 0;
            for (const auto &c : SurfaceChannels(initial))
                Add(c, ++order);
            UpdateMonitor(initial);
            Check(node->Thaw(), "Initial node thaw");
            Check(EuCon::GetInstance().RegisterNode(node.get()), "Register node");
            registered = true;
            Log("node registered read-only tracks=" + std::to_string(strips.size()));
        }
        catch (...)
        {
            Shutdown();
            throw;
        }
    }
    ~Impl()
    {
        Shutdown();
    }
    void Shutdown()
    {
        controller.Disarm();
        monitorController.Disarm();
        if (node)
        {
            CleanupResult(node->Freeze(), "Shutdown freeze");
            for (auto &entry : strips)
                CleanupResult(node->UnregisterProcessor(*entry.second), "Unregister processor");
            strips.clear();
            if (controlRoom)
            {
                CleanupResult(node->UnregisterProcessor(*controlRoom), "Unregister monitor");
                controlRoom.reset();
            }
            if (applicationCommands)
            {
                CleanupResult(node->UnregisterProcessor(*applicationCommands),
                              "Unregister command processor");
                applicationCommands.reset();
            }
            if (registered)
                CleanupResult(EuCon::GetInstance().UnregisterNode(*node), "Unregister node");
            node.reset();
            registered = false;
        }
        if (manager)
        {
            CleanupResult(EuConManager::Destroy(), "EUCON destroy");
            manager = false;
        }
    }
    void Add(const Channel &channel, int order)
    {
        auto strip = std::make_unique<Strip>(channel, nextTag++, order, inbox, *node, experimentalConfig);
        const auto inserted = strips.emplace(channel.key, std::move(strip));
        if (!inserted.second)
            throw std::runtime_error("Duplicate track registration");
        auto *ptr = inserted.first->second.get();
        try
        {
            Check(node->RegisterProcessor(*ptr), "Register processor");
        }
        catch (...)
        {
            strips.erase(inserted.first);
            throw;
        }
        ptr->Apply(channel, order);
        Log("track registered tag=" + std::to_string(ptr->Tag()) + " order=" + std::to_string(order));
    }
    void Apply(const Snapshot &state)
    {
        controller.Validate();
        monitorController.Validate();
        inbox.epoch = controller.Epoch();
        const auto events = inbox.Take();
        if (inbox.overflow)
            throw std::runtime_error("EUCON callback inbox overflow");
        const auto channelStatus = controller.Status();
        const auto monitorStatus = monitorController.Status();
        const auto configStatus = configuration ? configuration->Status() : ConfigStatus{};
        const FeedbackUpdatePolicy::Stamp stamp{
            state.generation, state.metadataRevision, state.controlRevision, state.connected ? 1U : 0U,
            channelStatus.epoch, channelStatus.confirmed, monitorStatus.epoch, monitorStatus.confirmed,
            configStatus.epoch, configStatus.confirmed, configStatus.busy ? 1U : 0U};
        const bool full = refreshPolicy.FullUpdate(stamp, !events.empty(),
            channelStatus.pending || monitorStatus.pending || configStatus.busy,
            std::chrono::steady_clock::now());
        if (state.connected && state.controlRevision && !full)
        {
            // Meter-only observation cannot change layout, controls or epochs.
            // Use the existing saved visibility handles and writer lifetime.
            for (const auto &c : state.channels)
                if (const auto it = strips.find(c.key); it != strips.end())
                    it->second->ApplyMeters(c.meters);
            EuBatchedMeterWriter writer(*node);
            for (auto &strip : strips) strip.second->WriteMeters(writer);
            return;
        }
        const auto channels = SurfaceChannels(state);
        const std::optional<Monitor> monitor =
            state.connected && state.monitors.size() == 1 && MonitorEligible(state.monitors.front())
                ? std::optional<Monitor>(state.monitors.front())
                : std::nullopt;
        if (inbox.overflow)
            throw std::runtime_error("EUCON callback inbox overflow");
        for (const auto &event : events)
        {
            bool queued = false;
            if (event.kind == Event::Kind::ApplicationCommand)
                queued = bridge::RequestSummon(static_cast<bridge::Application>(event.type));
            if (event.kind == Event::Kind::MonitorPrimitive && controlRoom)
                queued = controlRoom->Dispatch(event);
            if (event.kind == Event::Kind::Primitive)
                for (const auto &c : channels)
                {
                    const auto it = strips.find(c.key);
                    if (it != strips.end() && it->second->Tag() == event.tag)
                    {
                        queued = it->second->Dispatch(event, c, controller, monitor, monitorController, configuration);
                        break;
                    }
                }
            ++eventCount;
            if (event.kind == Event::Kind::Surface && event.type == kEVT_NODE_NetworkError)
            {
                Log("network error callback-thread=" + std::to_string(event.thread));
                throw std::runtime_error("EUCON network error; see diagnostic log");
            }
            if (event.kind == Event::Kind::Visibility)
            {
                for (auto &strip : strips)
                    if (strip.second->Tag() == event.tag)
                    {
                        strip.second->Visibility(event);
                        break;
                    }
                Log("visibility tag=" + std::to_string(event.tag) +
                    " shown=" + std::to_string(event.visible) + " handle=" + std::to_string(event.handle) +
                    " format=" + std::to_string(event.format) +
                    " callback-thread=" + std::to_string(event.thread));
            }
            else if (eventCount < 2000)
                Log("surface event=" + std::to_string(event.type) + " tag=" + std::to_string(event.tag) +
                    " control=" + std::to_string(event.control) + " member=" + std::to_string(event.member) +
                    " primitive=" + std::to_string(event.primitive) +
                    " index=" + std::to_string(event.index) + " flags=" + std::to_string(event.flags) +
                    " callback-thread=" + std::to_string(event.thread) +
                    " decode=" + std::to_string(event.decodeResult) +
                    " epoch=" + std::to_string(event.epoch) + (queued ? " write=queued" : " write=none"));
        }
        const bool monitorChanged = NeedsMonitorUpdate(state);
        if (monitorChanged)
        {
            Check(node->Freeze(), "Monitor topology freeze");
            try
            {
                UpdateMonitor(state);
            }
            catch (...)
            {
                CleanupResult(node->Thaw(), "Monitor topology thaw");
                throw;
            }
            Check(node->Thaw(), "Monitor topology thaw");
        }
        if (controlRoom && state.monitors.size() == 1)
            controlRoom->Apply(state.monitors.front());
        const bool touched =
            std::any_of(strips.begin(), strips.end(), [](const auto &s) { return s.second->Touched(); });
        std::set<std::string> desired;
        for (const auto &c : channels)
            desired.insert(c.key);
        bool topology = desired.size() != strips.size();
        for (const auto &key : desired)
            if (!strips.count(key))
                topology = true;
        for (const auto &c : channels)
        {
            const auto it = strips.find(c.key);
            if (it != strips.end() && !it->second->MatchesCapabilities(c))
                topology = true;
        }
        if (topology && !touched)
        {
            Check(node->Freeze(), "Topology freeze");
            try
            {
                for (auto it = strips.begin(); it != strips.end();)
                    if (!desired.count(it->first))
                    {
                        Check(node->UnregisterProcessor(*it->second), "Remove track");
                        it = strips.erase(it);
                    }
                    else
                        ++it;
                int order = 0;
                for (const auto &c : channels)
                {
                    ++order;
                    const auto it = strips.find(c.key);
                    if (it != strips.end() && !it->second->MatchesCapabilities(c))
                    {
                        Check(node->UnregisterProcessor(*it->second), "Replace changed capabilities");
                        strips.erase(it);
                    }
                    if (!strips.count(c.key))
                        Add(c, order);
                }
            }
            catch (...)
            {
                CleanupResult(node->Thaw(), "Topology thaw");
                throw;
            }
            Check(node->Thaw(), "Topology thaw");
        }
        int order = 0;
        for (const auto &c : channels)
        {
            ++order;
            const auto it = strips.find(c.key);
            if (it != strips.end())
            {
                it->second->SetEpoch(controller.Epoch(c.key));
                it->second->Apply(controller.Feedback(c), topology && touched ? -1 : order);
                it->second->ApplyGlobalConfig(state, configuration ? configuration->Epoch() : 0);
                it->second->ApplyMonitor(
                    monitor ? std::optional<Monitor>(monitorController.Feedback(*monitor)) : std::nullopt,
                    monitorController.Epoch());
            }
        }
        for (auto &s : strips)
            if (!desired.count(s.first))
            {
                s.second->ApplyGlobalConfig(Snapshot{});
                s.second->Unavailable();
            }
        // No application/inbox mutex may be held while the writer owns its lock.
        EuBatchedMeterWriter writer(*node);
        for (auto &strip : strips)
            strip.second->WriteMeters(writer);
    }
    bool NeedsMonitorUpdate(const Snapshot &s) const
    {
        const bool eligible = s.connected && s.monitors.size() == 1 && MonitorEligible(s.monitors.front());
        return eligible ? (!controlRoom || !controlRoom->Matches(s.monitors.front())) : bool(controlRoom);
    }
    void UpdateMonitor(const Snapshot &s)
    {
        if (!NeedsMonitorUpdate(s))
            return;
        monitorController.Disarm();
        if (controlRoom)
        {
            Check(node->UnregisterProcessor(*controlRoom), "Remove monitor processor");
            controlRoom.reset();
        }
        if (s.connected && s.monitors.size() == 1 && MonitorEligible(s.monitors.front()))
        {
            auto next =
                std::make_unique<ControlRoom>(s.monitors.front(), nextTag++, inbox, monitorController);
            Check(node->RegisterProcessor(*next), "Register monitor processor");
            controlRoom = std::move(next);
            Log("monitor registered tag=" + std::to_string(controlRoom->Tag()) + " locked=1 channel-fader=0");
        }
    }
};
ApolloEucon::ApolloEucon(const Snapshot &initial, ChannelController &controller, MonitorController &monitor,
                         bool experimentalConfig, ConfigController *configuration,
                         std::function<void()> wakeOwner)
    : impl_(std::make_unique<Impl>(initial, controller, monitor, experimentalConfig, configuration,
                                  std::move(wakeOwner)))
{
}
ApolloEucon::~ApolloEucon() = default;
void ApolloEucon::Apply(const Snapshot &state)
{
    impl_->Apply(state);
}
uint64_t ApolloEucon::SurfaceEvents() const
{
    return impl_->eventCount;
}
size_t ApolloEucon::VisibleMeters() const
{
    return std::count_if(impl_->strips.begin(), impl_->strips.end(),
                         [](const auto &p) { return p.second->Visible(); });
}
std::string RunEuconTextTests()
{
    Check(EuConManager::Initialize(), "Text test SDK initialize");
    size_t checks = 0;
    try
    {
        // Never register this processor or a node. The same Label helper is
        // used by production input/output names, including absent names.
        EuProcessor processor;
        EuControlTextDisplay display(&processor);
        auto &primitive = Primitive(display, EuControlTextDisplay::kID_TextDisplay);
        Label(primitive, L"");
        const auto verify = [&](EuPrimitiveControl &p, NEuCon::uint16 index, const std::wstring &text) {
            for (const auto width : {kSTRLEN_4, kSTRLEN_8, kSTRLEN_Long})
            {
                tEuString actual;
                Check(p.GetValueAt(index, actual, width), "Text test readback");
                if (actual != text.substr(0, width == kSTRLEN_4 ? 4 : width == kSTRLEN_8 ? 8 : text.size()))
                    throw std::runtime_error("Text test display-width mismatch");
                ++checks;
            }
        };
        verify(primitive, 0, L"");
        const std::vector<std::wstring> labels{L"",
                                               L"Monitor",
                                               L"Monitor",
                                               L"",
                                               L"AUX 1",
                                               L"\u8f93\u5165\u8def\u7531\u7acb\u4f53\u58f0",
                                               L"Long output destination 1-2",
                                               L""};
        for (const auto &label : labels)
        {
            UpdateText(primitive, label, "Text test update");
            verify(primitive, 0, label);
            CheckTextResult(primitive, primitive.LoadValueAt(0, label), 0, label, "Text test repeated load");
            verify(primitive, 0, label);
            EuControlTextDisplay initial(&processor);
            auto &p = Primitive(initial, EuControlTextDisplay::kID_TextDisplay);
            Label(p, label);
            verify(p, 0, label);
        }
        const auto rejects = [&](const auto &operation) {
            bool rejected = false;
            try
            {
                operation();
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            if (!rejected)
                throw std::runtime_error("Text test accepted an invalid result");
            ++checks;
        };
        rejects(
            [&] { CheckTextResult(primitive, kERR_AlreadySet, 0, L"Not the same", "Expected mismatch"); });
        rejects([&] { CheckTextResult(primitive, kERR_AlreadySet, 9, L"", "Expected invalid entry"); });
        rejects([&] { CheckTextResult(primitive, kERR_OutOfRange, 0, L"", "Expected SDK failure"); });
        rejects([&] { Check(kERR_AlreadySet, "Expected strict non-text failure"); });
        // Exercise explicit short/long variants, repeated updates, clearing and
        // reattachment. A single full string must not silently replace CR.
        const auto crText = UpperDirectoryLabels(UpperFunction::ControlRoom);
        {
            EuControlTextDisplay explicitTable(&processor);
            auto &p = Primitive(explicitTable, EuControlTextDisplay::kID_TextDisplay);
            Label(p, L"");
            const auto result = p.LoadValueAt(0, crText[0], crText[1], crText[2]);
            Check(result, "Explicit label table regression");
            if (!UpperLabelMatches(p, crText))
                throw std::runtime_error("Explicit label table did not preserve CR variants");
            ++checks;
        }
        for (bool available : {true, true, false, false, true})
        {
            const auto text = UpperDirectoryLabels(UpperFunction::ControlRoom, available);
            UpdateUpperLabel(primitive, text);
            if (!UpperLabelMatches(primitive, text))
                throw std::runtime_error("Control room text variants changed during refresh");
            ++checks;
        }
        UpdateText(primitive, L"OTHER", "Test intermediate label");
        UpdateText(primitive, L"CONTROL ROOM", "Test automatic truncation");
        if (UpperLabelMatches(primitive, crText))
            throw std::runtime_error("Truncated CONTROL ROOM must not equal explicit CR variants");
        UpdateUpperLabel(primitive, crText);
        ++checks;
        // Plug-in engineering text uses the same runtime path on numeric tables.
        EuControlKnob knobControl(&processor);
        auto &knob = Primitive(knobControl, EuControlKnob::kID_Knob);
        Initialize(knob, kTYP_Float, 2);
        Check(knob.LoadValueTableInterpolated(0.0F, 1.0F, 2), "Text test numeric table");
        for (const auto &label : {L"25%", L"25%", L"3.2 ms", L"", L""})
        {
            UpdateText(knob, label, "Text test numeric display", 1);
            verify(knob, 1, label);
        }
        // Knob temporary text comes from the primitive value table. Verify the
        // exact 4/8/full strings supplied to S3-class displays, including the
        // professional dB unit and the fader-floor representation.
        EuControlKnob valueControl(&processor);
        auto &valueKnob = Primitive(valueControl, EuControlKnob::kID_Knob);
        const std::vector<float> dbValues{-144.0F, -12.0F, 0.0F, 4.0F};
        Initialize(valueKnob, kTYP_Float, static_cast<NEuCon::uint16>(dbValues.size()));
        Check(valueKnob.LoadValueTable(dbValues, 1), "Value text numeric table");
        LoadDbValueText(valueKnob, dbValues, dbValues.front(), "Value text dB table");
        const auto verifyVariants = [&](EuPrimitiveControl &p, NEuCon::uint16 index,
                                        const ValueText &expected) {
            const std::array<std::pair<tSTRLEN, const std::wstring *>, 3> widths{{
                {kSTRLEN_4, &expected.short4}, {kSTRLEN_8, &expected.short8}, {kSTRLEN_Long, &expected.full}}};
            for (const auto &[width, text] : widths)
            {
                tEuString actual;
                Check(p.GetValueAt(index, actual, width), "Value text readback");
                if (actual != *text)
                    throw std::runtime_error(
                        "Value text width mismatch at index " + std::to_string(index) +
                        " width " + std::to_string(static_cast<int>(width)) + " (expected " +
                        std::to_string(text->size()) + " characters, received " +
                        std::to_string(actual.size()) + ")");
                ++checks;
            }
        };
        for (size_t i = 0; i < dbValues.size(); ++i)
            verifyVariants(valueKnob, static_cast<NEuCon::uint16>(i),
                           DbValueText(dbValues[i], dbValues.front()));
        EuControlKnobCell switchCell(&processor);
        auto &switchPrimitive = Primitive(switchCell, EuControlKnobCell::kID_LowerSwitch);
        Switch(switchPrimitive);
        verifyVariants(switchPrimitive, 0, {L"OFF", L"OFF", L"Off"});
        verifyVariants(switchPrimitive, 1, {L"ON", L"ON", L"On"});
        // Exercise the real strip/knob-set construction and incremental updates
        // without registering any application or accessing an audio engine.
        Inbox testInbox;
        Node testNode(testInbox);
        {
            ApplicationCommands commands(testInbox);
            Check(testNode.Freeze(), "Command model freeze");
            Check(testNode.RegisterProcessor(commands), "Command model register");
            Check(testNode.UnregisterProcessor(commands), "Command model unregister");
            Check(testNode.Thaw(), "Command model thaw");
            checks += 4;
        }
        auto fixture = ConsoleFixture();
        AddMonitorFixture(fixture);
        const auto model = BuildSnapshot(fixture);
        int tag = 200;
        for (const bool config : {false, true})
        for (auto channel : model.channels)
        {
            ++tag;
            Strip strip(channel, tag, tag, testInbox, testNode, config);
            Check(testNode.Freeze(), "Model test membership freeze");
            const auto registration = testNode.RegisterProcessor(strip);
            if (registration != kERR_OK)
            {
                CleanupResult(testNode.Thaw(), "Model test failed registration thaw");
                Check(registration, "Model test processor registration");
            }
            const auto detach = [&](Strip *p) {
                CleanupResult(testNode.Freeze(), "Model test cleanup freeze");
                CleanupResult(testNode.UnregisterProcessor(*p), "Model test cleanup unregister");
                CleanupResult(testNode.Thaw(), "Model test cleanup thaw");
            };
            // Non-owning scope guard: detach before the stack strip is destroyed.
            std::unique_ptr<Strip, decltype(detach)> membership(&strip, detach);
            Check(testNode.Thaw(), "Model test membership thaw");
            strip.Apply(channel, tag, false);
            if (!strip.RingModesForTest())
                throw std::runtime_error("Channel knob ring mode does not match its control semantics");
            ++checks;
            if (!strip.PanResetSwitchesForTest())
                throw std::runtime_error("Pan knob-top reset switches do not match mono/stereo pan cells");
            ++checks;
            if (!strip.UpperLinksForTest())
                throw std::runtime_error("Upper directory does not reference existing mix pages");
            ++checks;
            const auto insertsId = strip.PageIdForTest(ChannelFunction::Inserts);
            const auto inputId = strip.PageIdForTest(ChannelFunction::Input);
            if (inputId)
            {
                if (!strip.WrongUpperLinkRejectedForTest())
                    throw std::runtime_error("Input was accepted under the EQ directory entry");
                ++checks;
            }
            if (channel.recordPreEffects)
            {
                if (strip.RecIndexForTest() != 0)
                    throw std::runtime_error("UAD MON Rec LED must be off");
                channel.recordPreEffects->value = Json::Parse("false");
                strip.Apply(channel, tag, false);
                if (strip.RecIndexForTest() != 1)
                    throw std::runtime_error("UAD REC Rec LED must be on");
                checks += 2;
            }
            if (insertsId != strip.PageIdForTest(ChannelFunction::Inserts) ||
                inputId != strip.PageIdForTest(ChannelFunction::Input))
                throw std::runtime_error("Value feedback replaced a knob page");
            ++checks;
            if (!channel.preamps.empty() && channel.preamps[0].gain)
            {
                channel.preamps[0].gain->maximum = 64;
                strip.Apply(channel, tag, false);
                if (insertsId != strip.PageIdForTest(ChannelFunction::Inserts) ||
                    inputId == strip.PageIdForTest(ChannelFunction::Input))
                    throw std::runtime_error("Input metadata update disturbed unrelated insert page");
                ++checks;
            }
            strip.ApplyMonitor(model.monitors.front(), 0);
            if (!strip.RingModesForTest())
                throw std::runtime_error("Monitor knob ring mode does not match its control semantics");
            ++checks;
            if (!strip.ConfigMarkersForTest())
                throw std::runtime_error("Normal/config page markers diverged");
            ++checks;
            if (config)
            {
                auto preview = model;
                preview.connected = true;
                preview.globalConfig["SampleRate"] = "96 kHz";
                strip.ApplyGlobalConfig(preview);
                if (!strip.GlobalConfigReadbackForTest("SampleRate", L"96 KHZ"))
                    throw std::runtime_error("Global Config preview is missing or mutable");
                preview.globalConfig["SampleRate"] = "48 kHz";
                strip.ApplyGlobalConfig(preview);
                if (!strip.GlobalConfigReadbackForTest("SampleRate", L"48 KHZ"))
                    throw std::runtime_error("Global Config feedback did not update");
                preview.connected = false;
                strip.ApplyGlobalConfig(preview);
                if (!strip.GlobalConfigReadbackForTest("SampleRate", L"OFFLINE"))
                    throw std::runtime_error("Disconnected Config preview retained a value");
                checks += 3;
            }
            if (!strip.UpperMonitorForTest() || !strip.UpperLinksForTest())
                throw std::runtime_error("Monitor extension disturbed standard or quick-control pages");
            ++checks;
            auto monitorFeedback = model.monitors.front();
            monitorFeedback.dimAttenuation->value = ControlNumber(26);
            strip.ApplyMonitor(monitorFeedback, 0);
            strip.ApplyMonitor(std::nullopt, 0);
            if (strip.UpperMonitorForTest() || !strip.UpperLinksForTest())
                throw std::runtime_error("Monitor extension removal left stale references");
            ++checks;
            strip.ApplyMonitor(model.monitors.front(), 0);
            if (!strip.UpperMonitorForTest() || !strip.ConfigMarkersForTest())
                throw std::runtime_error("Monitor reattachment retained obsolete Config markers");
            ++checks;
            if (channel.path == "/devices/3/inputs/0")
            {
                AddUnisonFixture(fixture, channel.path);
                const auto unison = BuildSnapshot(fixture).channels.front();
                strip.Apply(unison, tag, false);
                if (!strip.UpperLinksForTest() || !strip.PageIdForTest(ChannelFunction::Unison))
                    throw std::runtime_error("UNISON extension missing from upper directory");
                strip.Apply(channel, tag, false);
                if (!strip.UpperLinksForTest())
                    throw std::runtime_error("UNISON removal left stale directory children");
                checks += 2;
            }
            if (config && channel.path == "/devices/3/inputs/0")
            {
                auto configFixture = fixture;
                AddConfigurationFixture(configFixture);
                auto configured = BuildSnapshot(configFixture);
                auto selected = configured.channels.front();
                strip.Apply(selected, tag, false);
                strip.ApplyMonitor(configured.monitors.front(), 0);
                if (!strip.ConfigMarkersForTest() || !strip.UpperLinksForTest() ||
                    !strip.ConfigValueForTest("/SampleRate", L"48 KHZ") ||
                    !strip.ConfigValueForTest(selected.path + "/effects/2/Preset", L"NEUTRAL") ||
                    !strip.ConfigValueForTest(selected.path + "/effects/0/EffectName", L"NONE"))
                    throw std::runtime_error("Expanded Config model/readback failed");
                const auto pluginPage = strip.PageIdForTest(ChannelFunction::Inserts);
                configFixture["/"].object["properties"].object["SampleRate"].object["value"] = Json::Parse("96000");
                configured = BuildSnapshot(configFixture);
                strip.Apply(configured.channels.front(), tag, false);
                if (!strip.ConfigValueForTest("/SampleRate", L"96 KHZ") ||
                    pluginPage != strip.PageIdForTest(ChannelFunction::Inserts))
                    throw std::runtime_error("Config feedback rebuilt unrelated plug-in page");
                checks += 5;
            }
            Check(testNode.Freeze(), "Model test removal freeze");
            Check(testNode.UnregisterProcessor(strip), "Model test processor unregister");
            membership.release();
            Check(testNode.Thaw(), "Model test removal thaw");
        }
    }
    catch (...)
    {
        CleanupResult(EuConManager::Destroy(), "Text test SDK cleanup");
        throw;
    }
    Check(EuConManager::Destroy(), "Text test SDK destroy");
    return "SDK text/model regression: " + std::to_string(checks) +
           " checks passed; no node registered, no Apollo/MIDI/audio access.\n";
}
} // namespace apollo
